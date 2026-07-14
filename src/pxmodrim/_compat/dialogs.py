from __future__ import annotations

import asyncio
from typing import Any, TypeVar

from PySide6.QtWidgets import QDialog

T = TypeVar("T", bound=QDialog)


async def await_dialog[T: QDialog](
    cls: type[T],
    *args: Any,
    **kwargs: Any,
) -> tuple[int, T]:
    """Show a modal QDialog async and await its result via an asyncio future."""
    dialog = cls(*args, **kwargs)
    dialog.setModal(True)

    future: asyncio.Future[int] = asyncio.get_running_loop().create_future()

    def handle_finished(result_code: int) -> None:
        if not future.done():
            future.set_result(result_code)

    dialog.finished.connect(handle_finished)
    dialog.show()

    result_code = await future
    return result_code, dialog
