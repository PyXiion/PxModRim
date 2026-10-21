#pragma once
#include <QMessageBox>
#include <QStringList>
#include <functional>

#include <QCoro/QCoroTask>
class QMenu;
class QAction;
namespace px::modrim {
class CoreContext;
}
namespace px::modrim {
inline constexpr int kConfirmThreshold = 25;
struct UpdateState {
  QString label;
  bool enabled;
  QString tooltip;
};
QStringList updatableIds(CoreContext& ctx, const QStringList& uuids);
UpdateState updateState(CoreContext& ctx, const QStringList& uuids);
QAction* addUpdateAction(QMenu* menu, CoreContext& ctx,
                         const QStringList& uuids,
                         std::function<void()> handler);
class ConfirmUpdateDialog : public QMessageBox {
 public:
  ConfirmUpdateDialog(int count, QWidget* parent);
};
QCoro::Task<bool> confirmUpdate(int count, QWidget* parent);
QCoro::Task<void> updateMods(CoreContext& ctx, const QStringList& uuids,
                             QWidget* parent);
}  // namespace px::modrim
