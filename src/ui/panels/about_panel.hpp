#pragma once

#include <QCoro/QCoroTask>
#include <QDialog>
#include <QStringList>

class QPushButton;
namespace px::modrim { class CoreContext; }
namespace px::modrim {
class Notifier;
class AboutPanel : public QDialog {
public:
    AboutPanel(QWidget* parent, CoreContext& ctx, Notifier& toast);
    static QStringList environment();
    QString systemInformation() const;
    static QString creditsHtml();

private:
    QCoro::Task<void> uploadReport();
    CoreContext& m_ctx;
    Notifier& m_toast;
    QPushButton* m_upload;
};
}  // namespace px::modrim
