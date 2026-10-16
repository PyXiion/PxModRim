#pragma once
#include <QCoro/QCoroTask>
#include <QMessageBox>
#include <QStringList>
#include <functional>
class QMenu;
class QAction;
namespace px::modrim { class CoreContext; }
namespace px::modrim {
inline constexpr int CONFIRM_THRESHOLD = 25;
struct UpdateState { QString label; bool enabled; QString tooltip; };
QStringList updatable_ids(CoreContext& ctx, const QStringList& uuids);
UpdateState update_state(CoreContext& ctx, const QStringList& uuids);
QAction* add_update_action(QMenu* menu, CoreContext& ctx, const QStringList& uuids, std::function<void()> handler);
class ConfirmUpdateDialog : public QMessageBox {
public:
    ConfirmUpdateDialog(int count, QWidget* parent);
};
QCoro::Task<bool> confirm_update(int count, QWidget* parent);
QCoro::Task<void> update_mods(CoreContext& ctx, const QStringList& uuids, QWidget* parent);
}
