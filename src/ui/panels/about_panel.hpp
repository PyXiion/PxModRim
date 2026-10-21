#pragma once

#include <QDialog>
#include <QLatin1StringView>
#include <QStringList>

#include <QCoro/QCoroTask>

class QPushButton;
namespace px::modrim {
class CoreContext;
}
namespace px::modrim {
class Notifier;
inline constexpr QLatin1StringView kIssuesUrl{
    "https://github.com/PyXiion/PxModRim/issues"};
class AboutPanel : public QDialog {
 public:
  AboutPanel(QWidget* parent, CoreContext& ctx, Notifier& toast);
  QString systemInformation() const;

 private:
  static QStringList environment();
  static QString creditsHtml();
  QCoro::Task<void> uploadReport();
  CoreContext& m_ctx;
  Notifier& m_toast;
  QPushButton* m_upload;
};
}  // namespace px::modrim
