#include "ui/components/dialog_chrome.h"

#include <QAbstractButton>
#include <QDialog>
#include <QEnterEvent>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLayout>
#include <QMargins>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QSizeGrip>
#include <QStringView>
#include <QWindow>

#include "ui/components/icons.h"
#include "ui/theme/palette.h"

namespace px::ui {

namespace {

constexpr int kTitleBarHeight = 36;
constexpr int kBorder = 1;
constexpr int kGripSize = 16;
constexpr int kCloseSize = 26;

// Every icon name used here is in the icon table; a miss is a programming error.
QIcon requireIcon(QStringView name, int size, QStringView color) {
    auto found = icon(name, size, color);
    Q_ASSERT(found.has_value());
    return found.value_or(QIcon());
}

// Soft-red close button that fills solid on hover, like the main window's.
class CloseButton : public QPushButton {
public:
    explicit CloseButton(QWidget* parent) : QPushButton(parent) {
        setObjectName(QStringLiteral("dialogCloseBtn"));
        setFixedSize(kCloseSize, kCloseSize);
        // A global QPushButton min-height would otherwise beat setFixedSize.
        constexpr int inner = kCloseSize - 2;  // the 1px QSS border sits outside the content box
        setStyleSheet(QStringLiteral("min-width: %1px; max-width: %1px; min-height: %1px; max-height: %1px;")
                          .arg(inner));
        setToolTip(QStringLiteral("Close"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        normal_icon_ = requireIcon(u"close", 12, paletteValue(u"DANGER"));
        hover_icon_ = requireIcon(u"close", 12, paletteValue(u"TEXT_ON_ACCENT"));
        setIcon(normal_icon_);
    }

protected:
    void enterEvent(QEnterEvent* event) override {
        setIcon(hover_icon_);
        QPushButton::enterEvent(event);
    }

    void leaveEvent(QEvent* event) override {
        setIcon(normal_icon_);
        QPushButton::leaveEvent(event);
    }

private:
    QIcon normal_icon_;
    QIcon hover_icon_;
};

// Closes the dialog the way Escape does: a message box clicks its escape button
// (or Cancel), anything else rejects.
void closeLikeEscape(QDialog* dialog) {
    if (auto* box = qobject_cast<QMessageBox*>(dialog)) {
        if (QAbstractButton* button = box->escapeButton()) {
            button->click();
            return;
        }
        if (box->button(QMessageBox::Cancel) != nullptr) {
            box->done(static_cast<int>(QMessageBox::Cancel));
            return;
        }
    }
    dialog->reject();
}

class TitleBar : public QWidget {
public:
    TitleBar(QDialog* dialog, bool closable) : QWidget(dialog) {
        setObjectName(QStringLiteral("dialogTitleBar"));
        setAttribute(Qt::WA_StyledBackground, true);
        auto* layout = new QHBoxLayout(this);
        layout->setContentsMargins(14, 0, 6, 0);
        layout->setSpacing(6);

        auto* title = new QLabel(dialog->windowTitle(), this);
        title->setObjectName(QStringLiteral("dialogTitleText"));
        layout->addWidget(title, 1);
        connect(dialog, &QWidget::windowTitleChanged, title, &QLabel::setText);

        if (closable) {
            auto* close = new CloseButton(this);
            connect(close, &QPushButton::clicked, dialog, [dialog] { closeLikeEscape(dialog); });
            layout->addWidget(close);
        }
    }

protected:
    void mousePressEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton) {
            if (QWindow* handle = window()->windowHandle()) {
                handle->startSystemMove();
                event->accept();
                return;
            }
        }
        QWidget::mousePressEvent(event);
    }
};

// Keeps the overlay title bar and size grip pinned to the dialog frame.
class Chrome : public QObject {
public:
    Chrome(QDialog* dialog, bool closable, bool resizable) : QObject(dialog), dialog_(dialog) {
        bar_ = new TitleBar(dialog, closable);
        grip_ = resizable ? new QSizeGrip(dialog) : nullptr;
        dialog->installEventFilter(this);
        place();
    }

protected:
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (watched == dialog_ && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            place();
        }
        return false;
    }

private:
    void place() {
        const int width = dialog_->width();
        bar_->setGeometry(kBorder, kBorder, width - 2 * kBorder, kTitleBarHeight);
        bar_->raise();
        if (grip_) {
            grip_->setGeometry(width - kGripSize - kBorder, dialog_->height() - kGripSize - kBorder,
                               kGripSize, kGripSize);
            grip_->raise();
        }
    }

    QDialog* dialog_;
    TitleBar* bar_ = nullptr;
    QSizeGrip* grip_ = nullptr;
};

}  // namespace

void installDialogChrome(QDialog* dialog, bool closable, std::optional<bool> resizable) {
    if (qobject_cast<QFileDialog*>(dialog) || dialog->property("chromed").toBool()) {
        return;
    }
    const bool grip = resizable.value_or(
        !(qobject_cast<QMessageBox*>(dialog) || dialog->isSizeGripEnabled()));

    const Qt::WindowFlags keep = dialog->windowFlags() & Qt::WindowStaysOnTopHint;
    dialog->setWindowFlags(Qt::Dialog | Qt::FramelessWindowHint | keep);
    dialog->setProperty("chromed", true);

    if (QLayout* layout = dialog->layout()) {
        const QMargins m = layout->contentsMargins();
        layout->setContentsMargins(m.left() + kBorder, m.top() + kTitleBarHeight + kBorder,
                                   m.right() + kBorder, m.bottom() + kBorder);
    }
    new Chrome(dialog, closable, grip);
}

}  // namespace px::ui
