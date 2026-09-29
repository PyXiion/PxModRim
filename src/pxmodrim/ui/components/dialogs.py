from __future__ import annotations

import asyncio
from typing import Any

from PySide6.QtWidgets import QDialog

from pxmodrim.ui.components.dialog_chrome import install_dialog_chrome


async def await_dialog[T: QDialog](
    cls: type[T],
    *args: Any,
    **kwargs: Any,
) -> tuple[int, T]:
    """Show a modal QDialog async and await its result via an asyncio future.

    The dialog is scheduled for deletion on the next loop iteration, so callers
    must read what they need from it synchronously after the await.
    """
    dialog = cls(*args, **kwargs)
    install_dialog_chrome(dialog)
    dialog.setModal(True)

    future: asyncio.Future[int] = asyncio.get_running_loop().create_future()

    def handle_finished(result_code: int) -> None:
        if not future.done():
            future.set_result(result_code)

    dialog.finished.connect(handle_finished)
    dialog.show()

    result_code = await future
    asyncio.get_running_loop().call_soon(dialog.deleteLater)
    return result_code, dialog
