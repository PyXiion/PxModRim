from __future__ import annotations

import pytest

from pxmodrim.ui.components.procedural_preview import initials


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
