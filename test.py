import random
import string
import sys

from PySide6.QtCore import QTimer
from PySide6.QtWidgets import QApplication, QTextEdit

app = QApplication(sys.argv)

w = QTextEdit()
w.setReadOnly(True)
w.resize(500, 300)
w.show()


def random_text():
    return "\n".join(
        "".join(random.choice(string.printable) for _ in range(80))
        for _ in range(random.randint(20, 150))
    )


def update():
    w.setHtml(random_text().replace("\n", "<br>"))
    w.document().adjustSize()
    h = w.document().size().height()
    w.setFixedHeight(min(int(h) + 16, 200))


timer = QTimer()
timer.timeout.connect(update)
timer.start(1)

sys.exit(app.exec())