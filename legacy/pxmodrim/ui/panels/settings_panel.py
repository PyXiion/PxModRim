from __future__ import annotations

import dataclasses
from collections.abc import Sequence
from pathlib import Path
from typing import Any

import msgspec
from PySide6.QtCore import Property, QObject, Qt, QTimer, QUrl, Signal, Slot
from PySide6.QtGui import QColor
from PySide6.QtQml import QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QDialog, QFileDialog, QVBoxLayout, QWidget
from qasync import asyncSlot

from pxmodrim.core.config import (
    AppConfig,
    community_rules_file,
    detect_game_paths,
)
from pxmodrim.core.constants import AfterLaunch
from pxmodrim.core.context import CoreContext
from pxmodrim.core.loading import LoadingState
from pxmodrim.core.sort.community_service import CommunityRulesService
from pxmodrim.ui.components.dialogs import await_dialog
from pxmodrim.ui.components.progress_dialog import ProgressDialog
from pxmodrim.ui.settings_section import SettingsSection, SettingsSectionFactory
from pxmodrim.ui.theme.palette import PALETTE

_QML = Path(__file__).parent / "Settings.qml"

_FOLDER_TITLES = {
    "game": "Select RimWorld Game Folder",
    "local": "Select Local Mods Folder",
    "workshop": "Select Workshop Mods Folder",
    "config": "Select RimWorld Config Folder",
}


class _FolderDialog(QFileDialog):
    def __init__(self, parent: QWidget, title: str) -> None:
        super().__init__(parent, title)
        self.setFileMode(QFileDialog.FileMode.Directory)
        self.setOption(QFileDialog.Option.ShowDirsOnly)


class _SettingsBackend(QObject):
    """State and actions behind Settings.qml; the dialog owns accept/reject."""

    pathPicked = Signal(str, str)
    saveRequested = Signal(dict)
    cancelRequested = Signal()
    communityChanged = Signal()
    cacheChanged = Signal()

    def __init__(
        self, ctx: CoreContext, dialog: QDialog, sections: Sequence[SettingsSection]
    ) -> None:
        super().__init__(dialog)
        self._ctx = ctx
        self._dialog = dialog
        self._sections = [
            {
                "title": section.title,
                "source": QUrl.fromLocalFile(str(section.source)).toString(),
                "section": section,
            }
            for section in sections
        ]
        cfg = ctx.config
        self._initial: dict[str, Any] = {
            "game": cfg.paths.game,
            "local": cfg.paths.local,
            "workshop": cfg.paths.workshop,
            "config": cfg.paths.config_folder,
            "compact": cfg.compact_mod_list,
            "launchArgs": cfg.launch_args,
            "launchWrapper": cfg.launch_wrapper,
            "afterLaunch": int(cfg.after_launch),
            "confirmErrors": cfg.launch_confirm_errors,
            "confirmUnsaved": cfg.launch_confirm_unsaved,
            "confirmRunning": cfg.launch_confirm_running,
            "useAltIds": cfg.sort.use_alternative_package_ids,
            "checkMissing": cfg.sort.check_missing_dependencies,
            "useCommunity": cfg.sort.use_community_rules,
        }
        rules = community_rules_file()
        self._community_status = (
            f"Found: {rules}" if rules.exists() else "Not downloaded"
        )
        self._community_busy = False
        self._cache_available = bool(cfg.paths.config_folder)
        self._cache_status = (
            "Cache ready"
            if self._cache_available
            else "No config folder — not available"
        )
        self._cache_busy = False

    @Property(dict, constant=True)  # type: ignore[arg-type]
    def initial(self) -> dict[str, Any]:
        return self._initial

    @Property(list, constant=True)  # type: ignore[arg-type]
    def sections(self) -> list[dict[str, Any]]:
        return self._sections

    @Property(str, notify=communityChanged)  # type: ignore[arg-type]
    def communityStatus(self) -> str:
        return self._community_status

    @Property(bool, notify=communityChanged)  # type: ignore[arg-type]
    def communityBusy(self) -> bool:
        return self._community_busy

    @Property(str, notify=cacheChanged)  # type: ignore[arg-type]
    def cacheStatus(self) -> str:
        return self._cache_status

    @Property(bool, notify=cacheChanged)  # type: ignore[arg-type]
    def cacheAvailable(self) -> bool:
        return self._cache_available

    @Property(bool, notify=cacheChanged)  # type: ignore[arg-type]
    def cacheBusy(self) -> bool:
        return self._cache_busy

    def _set_community(self, status: str, busy: bool) -> None:
        self._community_status = status
        self._community_busy = busy
        self.communityChanged.emit()

    def _set_cache(self, status: str, busy: bool) -> None:
        self._cache_status = status
        self._cache_busy = busy
        self.cacheChanged.emit()

    @asyncSlot(str)
    async def browse(self, key: str) -> None:
        title = _FOLDER_TITLES.get(key)
        if title is None:
            return
        result, dialog = await await_dialog(_FolderDialog, self._dialog, title)
        files = dialog.selectedFiles()
        if result == QDialog.DialogCode.Accepted and files:
            self.pathPicked.emit(key, files[0])

    @Slot(str, result=str)
    def localCandidate(self, game_path: str) -> str:
        candidate = Path(game_path) / "Mods"
        return str(candidate) if candidate.is_dir() else ""

    @Slot()
    def autoDetect(self) -> None:
        detected = detect_game_paths()
        for key, value in (
            ("game", detected.game),
            ("local", detected.local),
            ("workshop", detected.workshop),
            ("config", detected.config_folder),
        ):
            if value:
                self.pathPicked.emit(key, value)

    @asyncSlot()
    async def downloadCommunityRules(self) -> None:
        self._set_community("Downloading…", True)
        status = "Download failed"
        try:
            async with ProgressDialog(LoadingState(self), self._dialog) as dialog:
                service = CommunityRulesService(self._ctx.config_service)
                path = await service.ensure_rules(dialog.loading, force=True)
            QTimer.singleShot(1000, dialog, dialog.deleteLater)
            if path:
                status = f"Downloaded: {path}"
        finally:
            self._set_community(status, False)

    @asyncSlot()
    async def clearCache(self) -> None:
        self._set_cache("Clearing…", True)
        status = "Cache cleared"
        try:
            await self._ctx.mod_service.startup_impact.clear()
        except OSError as exc:
            status = f"Error: {exc}"
        finally:
            self._set_cache(status, False)

    @Slot("QVariantMap")
    def save(self, values: dict[str, Any]) -> None:
        self.saveRequested.emit(values)

    @Slot()
    def cancel(self) -> None:
        self.cancelRequested.emit()


class SettingsPanel(QDialog):
    def __init__(
        self,
        ctx: CoreContext,
        qml_engine: QQmlEngine,
        parent: QWidget,
        sections: Sequence[SettingsSectionFactory] = (),
    ) -> None:
        super().__init__(parent)
        self.setObjectName("settingsPanel")
        self.setWindowTitle("Settings")
        self.setModal(True)
        self.resize(700, 560)

        self._ctx = ctx
        self._config = ctx.config

        self._sections = [factory(self) for factory in sections]
        self._backend = _SettingsBackend(ctx, self, self._sections)
        self._backend.saveRequested.connect(self._save)
        self._backend.cancelRequested.connect(self.reject)

        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self._qml = QQuickWidget(qml_engine, self)  # type: ignore[arg-type]
        self._qml.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
        self._qml.setAttribute(Qt.WidgetAttribute.WA_AlwaysStackOnTop, False)
        self._qml.setClearColor(QColor(PALETTE["ELEVATE_2"]))
        self._qml.rootContext().setContextProperty("settings", self._backend)
        self._qml.setSource(QUrl.fromLocalFile(str(_QML)))
        layout.addWidget(self._qml)
        # Unload before the backend and sections (children of this dialog) are
        # destroyed, or every binding to them re-evaluates against null. Queued:
        # Save/Cancel finish the dialog from inside a QML click handler.
        self.finished.connect(self._unload, Qt.ConnectionType.QueuedConnection)

    def _unload(self) -> None:
        self._qml.setSource(QUrl())

    def _save(self, values: dict[str, Any]) -> None:
        self._config = msgspec.structs.replace(
            self._config,
            paths=msgspec.structs.replace(
                self._config.paths,
                game=values["game"].strip(),
                local=values["local"].strip(),
                workshop=values["workshop"].strip(),
                config_folder=values["config"].strip(),
            ),
            sort=dataclasses.replace(
                self._config.sort,
                use_alternative_package_ids=bool(values["useAltIds"]),
                check_missing_dependencies=bool(values["checkMissing"]),
                use_community_rules=bool(values["useCommunity"]),
            ),
            compact_mod_list=bool(values["compact"]),
            launch_args=values["launchArgs"].strip(),
            launch_wrapper=values["launchWrapper"].strip(),
            after_launch=AfterLaunch(int(values["afterLaunch"])),
            launch_confirm_errors=bool(values["confirmErrors"]),
            launch_confirm_unsaved=bool(values["confirmUnsaved"]),
            launch_confirm_running=bool(values["confirmRunning"]),
        )
        for section in self._sections:
            section.apply()
        self.accept()

    def get_config(self) -> AppConfig:
        return self._config
