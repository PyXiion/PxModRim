from __future__ import annotations

from collections.abc import Mapping
from typing import TYPE_CHECKING

import msgspec
from loguru import logger

from pxmodrim.core.events import Event

if TYPE_CHECKING:
    from pxmodrim.core.config import ConfigService


class PluginConfig[T: msgspec.Struct]:
    """A plugin's own settings, kept in ``plugins/<name>.json``.

    *legacy* maps keys of the old shared ``config.json`` to fields of *settings_type*;
    they seed the file the first time it is created. Without a *service* the
    settings live in memory only.
    """

    __slots__ = ("_filename", "_service", "_value", "changed")

    def __init__(
        self,
        service: ConfigService | None,
        name: str,
        settings_type: type[T],
        legacy: Mapping[str, str] | None = None,
    ) -> None:
        self.changed: Event[T] = Event()
        self._service = service
        self._filename = f"plugins/{name}.json"
        stored = (
            None
            if service is None
            else service.load_existing(self._filename, settings_type)
        )
        if stored is None:
            stored = self._seed(settings_type, legacy or {})
        self._value: T = stored

    def _seed(self, settings_type: type[T], legacy: Mapping[str, str]) -> T:
        if self._service is None or not legacy:
            return settings_type()
        raw = self._service.load_raw("config.json")
        fields = {new: raw[old] for old, new in legacy.items() if old in raw}
        try:
            value = msgspec.convert(fields, settings_type)
        except msgspec.ValidationError as exc:
            logger.warning("Ignoring old settings for {}: {}", self._filename, exc)
            return settings_type()
        if fields:
            self._store(value)
        return value

    @property
    def value(self) -> T:
        return self._value

    def update(self, value: T) -> None:
        if value == self._value:
            return
        self._value = value
        self._store(value)
        self.changed.emit(value)

    def _store(self, value: T) -> None:
        if self._service is None:
            return
        try:
            self._service.save(self._filename, value)
        except OSError as exc:
            logger.warning("Cannot save {}: {}", self._filename, exc)
