from __future__ import annotations

from collections.abc import Callable
from pathlib import Path
from typing import Any, ClassVar

import msgspec
from PySide6.QtCore import Property, QObject, Slot

from pxmodrim.core.plugin_config import PluginConfig


class SettingsSection(QObject):
    """A plugin's group on the Settings dialog.

    ``source`` is loaded into a group titled ``title`` with this object as its
    required ``section`` property. Edits stay on the object until the dialog is
    saved, when ``apply()`` commits them; a cancelled dialog just drops it.
    """

    title: ClassVar[str]
    source: ClassVar[Path]

    def apply(self) -> None: ...


type SettingsSectionFactory = Callable[[QObject], SettingsSection]


class PluginConfigSection(SettingsSection):
    """Edits a ``PluginConfig``: QML reads ``initial`` and reports edits via ``set``."""

    def __init__(self, config: PluginConfig[Any], parent: QObject) -> None:
        super().__init__(parent)
        self._config = config
        self._initial: dict[str, Any] = msgspec.to_builtins(config.value)
        self._draft: dict[str, Any] = {}

    @Property(dict, constant=True)  # type: ignore[arg-type]
    def initial(self) -> dict[str, Any]:
        return self._initial

    @Slot(str, "QVariant")
    def set(self, key: str, value: Any) -> None:
        if key not in self._initial:
            raise KeyError(key)
        self._draft[key] = value

    def apply(self) -> None:
        if self._draft:
            self._config.update(
                msgspec.convert(
                    {**self._initial, **self._draft},
                    type(self._config.value),
                    strict=False,
                )
            )
