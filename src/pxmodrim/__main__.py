from __future__ import annotations

import sys

from pxmodrim._app import App


def main() -> None:
    app = App()
    sys.exit(app.run())


if __name__ == "__main__":
    main()
