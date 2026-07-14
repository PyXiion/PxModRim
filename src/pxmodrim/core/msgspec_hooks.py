from __future__ import annotations

from typing import Any

from pxmodrim.models.metadata.structures import CaseInsensitiveStr


def enc_hook(obj: Any) -> Any:
    if isinstance(obj, CaseInsensitiveStr):
        return str(obj)
    raise NotImplementedError(
        f"Object of type {type(obj).__name__} is not JSON serializable"
    )


def dec_hook(type_: type, obj: Any) -> Any:
    if type_ is CaseInsensitiveStr and isinstance(obj, str):
        return CaseInsensitiveStr(obj)
    raise NotImplementedError(f"Cannot decode {type_}")
