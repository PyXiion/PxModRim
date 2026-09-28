from __future__ import annotations

from collections.abc import Iterator
from pathlib import Path
from unittest.mock import AsyncMock

import pytest
from PySide6.QtWidgets import (
    QAbstractButton,
    QApplication,
    QDialog,
    QListWidget,
    QMessageBox,
)

from pxmodrim.core.config import AppConfig, ConfigService
from pxmodrim.core.context import CoreContext
from pxmodrim.core.mod_service import ModService
from pxmodrim.core.models.metadata.structures import CaseInsensitiveStr, ModsConfig
from pxmodrim.core.mods_config import parse_mods_config, write_mods_config
from pxmodrim.ui.panels.restore_snapshot_dialog import (
    ConfirmRestoreDialog,
    RestoreSnapshotDialog,
)


@pytest.fixture
def qapp() -> Iterator[QApplication]:
    app = QApplication.instance()
    if app is None:
        app = QApplication([])
    assert isinstance(app, QApplication)
    yield app


def _write_snapshot(path: Path, *package_ids: str) -> None:
    write_mods_config(
        path,
        ModsConfig(
            version="1.5",
            activeMods=[CaseInsensitiveStr(value) for value in package_ids],
            knownExpansions=[],
        ),
        path.parent,
    )


def test_restore_dialog_offscreen_selection_and_accept_reject(
    qapp: QApplication, tmp_path: Path
) -> None:
    valid_snap = tmp_path / "ModsConfig_20260922_120000.xml"
    _write_snapshot(valid_snap, "mod.foo", "mod.bar")
    corrupt_snap = tmp_path / "ModsConfig_20260922_130000.xml"
    corrupt_snap.write_text("<<<INVALID XML>>>", encoding="utf-8")

    dialog = RestoreSnapshotDialog([valid_snap, corrupt_snap])
    list_widget = dialog.findChild(QListWidget)
    assert list_widget is not None
    assert list_widget.count() == 2

    # Initial selection is valid_snap
    assert dialog.selected_snapshot == valid_snap
    restore_btn = next(
        b for b in dialog.findChildren(QAbstractButton) if b.text() == "Restore"
    )
    cancel_btn = next(
        b for b in dialog.findChildren(QAbstractButton) if b.text() == "Cancel"
    )
    assert restore_btn.isEnabled() is True

    # Change selection to corrupt item
    list_widget.setCurrentRow(1)
    assert dialog.selected_snapshot is None
    assert restore_btn.isEnabled() is False

    # Switch back to valid item
    list_widget.setCurrentRow(0)
    assert dialog.selected_snapshot == valid_snap
    assert restore_btn.isEnabled() is True

    # Cancel button rejects dialog
    cancel_btn.click()
    assert dialog.result() == QDialog.DialogCode.Rejected

    # Re-instantiate and test double click accept
    dialog2 = RestoreSnapshotDialog([valid_snap])
    list_widget2 = dialog2.findChild(QListWidget)
    assert list_widget2 is not None
    dialog2._on_item_double_clicked(list_widget2.item(0))
    assert dialog2.result() == QDialog.DialogCode.Accepted


def test_confirm_restore_dialog_paths(qapp: QApplication) -> None:
    confirm = ConfirmRestoreDialog("ModsConfig_20260922_120000.xml")
    assert confirm.windowTitle() == "Restore Mod List"
    assert "ModsConfig_20260922_120000.xml" in confirm.text()
    assert confirm.defaultButton() == confirm.button(QMessageBox.StandardButton.Cancel)

    restore_btn = confirm.button(QMessageBox.StandardButton.Yes)
    assert restore_btn is not None
    assert restore_btn.text() == "Restore"
    cancel_btn = confirm.button(QMessageBox.StandardButton.Cancel)
    assert cancel_btn is not None
    assert cancel_btn.text() == "Cancel"


@pytest.mark.asyncio
async def test_mod_service_restore_snapshot(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config_dir = tmp_path / "px_config"
    config_dir.mkdir()
    game_config_dir = tmp_path / "game_config"
    game_config_dir.mkdir()

    app_cfg = AppConfig()
    app_cfg.paths.config_folder = str(game_config_dir)

    cfg_svc = ConfigService(config_dir)
    ctx = CoreContext(app_cfg, cfg_svc)
    service = ModService(ctx, [])

    mock_reload = AsyncMock()
    monkeypatch.setattr(ModService, "reload", mock_reload)

    current_cfg_path = game_config_dir / "ModsConfig.xml"
    write_mods_config(
        current_cfg_path,
        ModsConfig(
            version="1.5",
            activeMods=[CaseInsensitiveStr("current.mod")],
            knownExpansions=[],
        ),
        config_dir / "snapshots",
    )

    snapshots_dir = config_dir / "snapshots"
    snapshots_dir.mkdir(parents=True, exist_ok=True)
    snapshot_path = snapshots_dir / "ModsConfig_20260922_100000.xml"
    _write_snapshot(snapshot_path, "restored.mod")

    assert await service.restore_snapshot(snapshot_path) is True
    mock_reload.assert_awaited_once()

    restored = parse_mods_config(current_cfg_path)
    assert restored is not None
    assert restored.activeMods == [CaseInsensitiveStr("restored.mod")]

    snapshots = service.get_snapshots()
    assert len(snapshots) == 2
    current_backup = next(path for path in snapshots if path != snapshot_path)
    backup = parse_mods_config(current_backup)
    assert backup is not None
    assert backup.activeMods == [CaseInsensitiveStr("current.mod")]


@pytest.mark.asyncio
async def test_mod_service_restore_invalid_snapshot(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config_dir = tmp_path / "px_config"
    config_dir.mkdir()
    game_config_dir = tmp_path / "game_config"
    game_config_dir.mkdir()

    app_cfg = AppConfig()
    app_cfg.paths.config_folder = str(game_config_dir)

    cfg_svc = ConfigService(config_dir)
    ctx = CoreContext(app_cfg, cfg_svc)
    service = ModService(ctx, [])
    mock_reload = AsyncMock()
    monkeypatch.setattr(ModService, "reload", mock_reload)
    current_cfg_path = game_config_dir / "ModsConfig.xml"
    write_mods_config(
        current_cfg_path,
        ModsConfig(
            version="1.5",
            activeMods=[CaseInsensitiveStr("safe.mod")],
            knownExpansions=[],
        ),
        config_dir / "snapshots",
    )
    original = current_cfg_path.read_bytes()

    corrupt = config_dir / "snapshots" / "ModsConfig_corrupt.xml"
    corrupt.parent.mkdir(parents=True, exist_ok=True)
    corrupt.write_text("NOT XML", encoding="utf-8")

    assert await service.restore_snapshot(corrupt) is False
    mock_reload.assert_not_called()
    assert current_cfg_path.read_bytes() == original


@pytest.mark.asyncio
async def test_mod_service_initialize_snapshots_existing_config(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config_dir = tmp_path / "px_config"
    config_dir.mkdir()
    game_config_dir = tmp_path / "game_config"
    game_config_dir.mkdir()

    app_cfg = AppConfig()
    app_cfg.paths.config_folder = str(game_config_dir)

    cfg_svc = ConfigService(config_dir)
    ctx = CoreContext(app_cfg, cfg_svc)
    service = ModService(ctx, [])

    mock_refresh = AsyncMock()
    monkeypatch.setattr(ModService, "_run_refresh_pipeline", mock_refresh)

    current_cfg_path = game_config_dir / "ModsConfig.xml"
    initial_mods = ModsConfig(
        version="1.5",
        activeMods=[CaseInsensitiveStr("current.mod")],
        knownExpansions=[],
    )
    write_mods_config(current_cfg_path, initial_mods, config_dir / "snapshots")
    original_bytes = current_cfg_path.read_bytes()

    for snap in (config_dir / "snapshots").glob("*.xml"):
        snap.unlink()
    assert service.get_snapshots() == []

    await service.initialize()
    mock_refresh.assert_awaited_once_with("initialize")

    snapshots = service.get_snapshots()
    assert len(snapshots) == 1
    snap_data = parse_mods_config(snapshots[0])
    assert snap_data is not None
    assert snap_data.activeMods == [CaseInsensitiveStr("current.mod")]
    assert current_cfg_path.read_bytes() == original_bytes

    saved = await service.save_active_layout([])
    assert saved is True
    assert len(service.get_snapshots()) == 1


@pytest.mark.asyncio
async def test_mod_service_configurable_retention_honored(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    config_dir = tmp_path / "px_config"
    config_dir.mkdir()
    game_config_dir = tmp_path / "game_config"
    game_config_dir.mkdir()

    app_cfg = AppConfig(max_snapshots=3)
    app_cfg.paths.config_folder = str(game_config_dir)

    cfg_svc = ConfigService(config_dir)
    ctx = CoreContext(app_cfg, cfg_svc)
    service = ModService(ctx, [])

    current_cfg_path = game_config_dir / "ModsConfig.xml"
    for i in range(5):
        write_mods_config(
            current_cfg_path,
            ModsConfig(
                version="1.5",
                activeMods=[CaseInsensitiveStr(f"mod.{i}")],
                knownExpansions=[],
            ),
            config_dir / "snapshots",
            max_snapshots=ctx.config.max_snapshots,
        )

    assert len(service.get_snapshots()) == 3


def test_settings_panel_save_preserves_max_snapshots(qapp: QApplication) -> None:
    from pxmodrim.ui.panels.settings_panel import SettingsPanel

    cfg = AppConfig(max_snapshots=7)
    ctx = CoreContext(cfg)
    panel = SettingsPanel(ctx)
    panel._save()
    assert panel.get_config().max_snapshots == 7
