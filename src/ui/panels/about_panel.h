#pragma once

#include <QCoro/QCoroTask>
#include <QDialog>
#include <QStringList>

class QPushButton;
namespace px::core { class CoreContext; }
namespace px::ui {
class Notifier;
class AboutPanel : public QDialog {
public:
    AboutPanel(QWidget* parent, core::CoreContext& ctx, Notifier& toast);
    static QStringList environment();
    QString systemInformation() const;
    static QString creditsHtml();

private:
    QCoro::Task<void> uploadReport();
    core::CoreContext& ctx_;
    Notifier& toast_;
    QPushButton* upload_;
};
}  // namespace px::ui
