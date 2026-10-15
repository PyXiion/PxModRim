from __future__ import annotations

import json
from pathlib import Path

from loguru import logger

from pxmodrim._app import _configure_file_logging


def _write_record(tmp_path: Path, monkeypatch, level: str, message: str) -> dict:
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.setenv("LOGURU_LEVEL", level)
    sink_id = _configure_file_logging()
    try:
        logger.log(level, message)
    finally:
        logger.remove(sink_id)

    log_path = tmp_path / "pxmodrim" / "logs" / "pxmodrim.log"
    return json.loads(log_path.read_text(encoding="utf-8").splitlines()[-1])


def test_file_sink_writes_parseable_info_record(tmp_path: Path, monkeypatch) -> None:
    event = _write_record(tmp_path, monkeypatch, "INFO", "startup context")
    record = event["record"]

    assert record["level"]["name"] == "INFO"
    assert record["message"] == "startup context"
    assert record["process"]["id"] > 0
    assert record["time"]["timestamp"] > 0
    assert isinstance(record["line"], int)


def test_file_sink_defaults_to_info(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setenv("XDG_CONFIG_HOME", str(tmp_path))
    monkeypatch.delenv("LOGURU_LEVEL", raising=False)
    sink_id = _configure_file_logging()
    try:
        logger.debug("hidden debug context")
        logger.info("visible info context")
    finally:
        logger.remove(sink_id)

    log_path = tmp_path / "pxmodrim" / "logs" / "pxmodrim.log"
    levels = [
        json.loads(line)["record"]["level"]["name"]
        for line in log_path.read_text(encoding="utf-8").splitlines()
    ]
    assert levels == ["INFO"]


def test_file_sink_includes_debug_when_requested(tmp_path: Path, monkeypatch) -> None:
    event = _write_record(tmp_path, monkeypatch, "DEBUG", "debug context")

    assert event["record"]["level"]["name"] == "DEBUG"
