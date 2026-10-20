from __future__ import annotations

import sys
import time
from pathlib import Path
from types import SimpleNamespace
from typing import Any

import pytest
from PySide6.QtWidgets import QApplication

from pxmodrim._app import App


class _Toasts:
    def __init__(self) -> None:
        self.errors: list[str] = []

    def error(self, message: str) -> None:
        self.errors.append(message)


def _app_stub(
    qapp: QApplication, arguments: list[str], override: str | None
) -> tuple[Any, _Toasts]:
    toasts = _Toasts()
    stub = SimpleNamespace(
        qt_app=qapp,
        _original_arguments=arguments,
        _qt_scale_factor_override=override,
        _app_ctx=SimpleNamespace(toasts=toasts),
    )
    return stub, toasts


def _wait_for(path: Path) -> str:
    deadline = time.monotonic() + 10
    while time.monotonic() < deadline:
        if path.exists() and path.read_text(encoding="utf-8"):
            return path.read_text(encoding="utf-8")
        time.sleep(0.05)
    raise AssertionError(f"{path} was not written")


@pytest.mark.parametrize(("override", "expected"), [(None, "unset"), ("1.5", "1.5")])
def test_restart_relaunches_with_original_arguments_and_clean_scale_env(
    qapp: QApplication,
    tmp_path: Path,
    monkeypatch: pytest.MonkeyPatch,
    override: str | None,
    expected: str,
) -> None:
    marker = tmp_path / "marker"
    code = (
        "import os, sys; "
        f"open({str(marker)!r}, 'w').write(os.environ.get('QT_SCALE_FACTOR', 'unset')"
        " + ' ' + sys.argv[1])"
    )
    app, toasts = _app_stub(qapp, [sys.executable, "-c", code, "--flag"], override)
    # An app-generated factor lives in the parent's environment only.
    monkeypatch.setenv("QT_SCALE_FACTOR", "1.5")

    assert App._start_restart_process(app) is True

    assert _wait_for(marker) == f"{expected} --flag"
    assert toasts.errors == []


def test_restart_reports_launch_failure_and_keeps_running(
    qapp: QApplication, monkeypatch: pytest.MonkeyPatch, tmp_path: Path
) -> None:
    monkeypatch.setattr(sys, "executable", str(tmp_path / "missing-python"))
    app, toasts = _app_stub(qapp, ["python"], None)

    assert App._start_restart_process(app) is False

    assert len(toasts.errors) == 1
