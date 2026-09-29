from __future__ import annotations

import pytest
from PySide6.QtGui import QGuiApplication, QImage

from pxmodrim.ui.components.procedural_preview import generate_preview, initials


@pytest.mark.parametrize(
    ("title", "expected"),
    [
        ("[AV] Framework - Russian Translation", "FR"),
        ("SF Grim Reality", "SG"),
        ("Vanilla Expanded Framework", "VE"),
        ("RimworldTweaks", "RT"),
        ("Combat", "CO"),
        ("[AV]", "AV"),
        ("[AV] Framework", "AF"),
        ("The Lord of Rings", "LR"),
        ("Русский перевод", "РП"),
        ("---", "--"),
    ],
)
def test_initials(title: str, expected: str) -> None:
    assert initials(title) == expected


def test_generate_preview_is_thread_safe_image() -> None:
    app = QGuiApplication.instance() or QGuiApplication([])
    assert app is not None
    image = generate_preview("[AV] Framework", 120, 60)
    assert isinstance(image, QImage)
    assert (image.width(), image.height()) == (120, 60)
    assert not image.isNull()
    assert generate_preview("[AV] Framework", 120, 60) is image
