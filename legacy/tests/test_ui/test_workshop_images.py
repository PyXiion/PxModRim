from __future__ import annotations

import json
from pathlib import Path

import pytest
from PySide6.QtCore import QUrl
from PySide6.QtGui import QImage
from PySide6.QtQml import QJSEngine, QJSValue, QQmlEngine
from PySide6.QtQuickWidgets import QQuickWidget
from PySide6.QtWidgets import QApplication, QWidget
from pytestqt.qtbot import QtBot

from pxmodrim.ui.plugins.workshop import view


@pytest.fixture
def thumbnails(qapp: QApplication) -> QJSValue:
    path = Path(view.__file__).with_name("thumbs.js")
    engine = QJSEngine(qapp)
    engine.evaluate(path.read_text().removeprefix(".pragma library"), str(path))
    return engine.globalObject().property("sized")


@pytest.mark.parametrize(
    "url",
    [
        "https://example.com/image.png",
        "https://notsteamstatic.com/image.png",
        "https://steamusercontent.com.example.com/image.png",
        "https://user@steamstatic.com/image.png",
        "",
    ],
)
def test_thumbnail_leaves_other_urls_unchanged(thumbnails: QJSValue, url: str) -> None:
    assert (
        thumbnails.call([QJSValue(url), QJSValue(256), QJSValue(128)]).toString() == url
    )


@pytest.mark.parametrize(
    "host", ["images.steamusercontent.com", "steamstatic.com", "cdn.steamstatic.com"]
)
def test_thumbnail_replaces_resize_query_before_fragment(
    thumbnails: QJSValue, host: str
) -> None:
    url = f"https://{host}/image.png?token=abc&imw=720&imh=360&ima=fit&impolicy=Letterbox#preview"
    expected = f"https://{host}/image.png?token=abc&imw=256&imh=128&ima=fit&impolicy=Letterbox#preview"
    resized = thumbnails.call([QJSValue(url), QJSValue(256), QJSValue(128)]).toString()
    assert resized == expected
    assert (
        thumbnails.call([QJSValue(resized), QJSValue(256), QJSValue(128)]).toString()
        == expected
    )


def test_hidden_thumbnail_unloads_and_can_load_again(
    qapp: QApplication, tmp_path: Path, qtbot: QtBot
) -> None:
    image = QImage(64, 32, QImage.Format.Format_RGB32)
    image.fill(0xFF00FF00)
    image_path = tmp_path / "preview.png"
    assert image.save(str(image_path))
    directory = QUrl.fromLocalFile(str(Path(view.__file__).parent)).toString()
    source = tmp_path / "Image.qml"
    source.write_text(
        f'import QtQuick\nimport "{directory}" as Workshop\n'
        f"Workshop.CatalogImage {{ previewUrl: {json.dumps(image_path.as_uri())} }}",
    )
    owner = QWidget()
    engine = QQmlEngine(owner)
    warnings: list[str] = []
    engine.warnings.connect(
        lambda errors: warnings.extend(error.toString() for error in errors)
    )
    widget = QQuickWidget(engine, owner)
    widget.setResizeMode(QQuickWidget.ResizeMode.SizeRootObjectToView)
    widget.resize(200, 110)
    widget.setSource(QUrl.fromLocalFile(str(source)))
    owner.show()
    widget.show()
    root = widget.rootObject()
    assert root is not None
    handle = engine.newQObject(root)

    def wait_for_status(expected: int) -> None:
        qtbot.waitUntil(lambda: handle.property("status").toInt() == expected)

    try:
        wait_for_status(1)
        root.setVisible(False)
        wait_for_status(0)
        root.setVisible(True)
        widget.resize(300, 150)
        wait_for_status(1)
        assert widget.grabFramebuffer().pixelColor(150, 75).name() == "#00ff00"
        assert not warnings, warnings
    finally:
        widget.setSource(QUrl())
        owner.close()
        owner.deleteLater()
