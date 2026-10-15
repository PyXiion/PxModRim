#pragma once
#include <QCoro/QCoroTask>
#include <QMessageBox>
#include <QStringList>
#include <functional>
class QMenu;
class QAction;
namespace px::core { class CoreContext; }
namespace px::ui {
inline constexpr int CONFIRM_THRESHOLD = 25;
struct UpdateState { QString label; bool enabled; QString tooltip; };
QStringList updatable_ids(core::CoreContext& ctx, const QStringList& uuids);
UpdateState update_state(core::CoreContext& ctx, const QStringList& uuids);
QAction* add_update_action(QMenu* menu, core::CoreContext& ctx, const QStringList& uuids, std::function<void()> handler);
class ConfirmUpdateDialog : public QMessageBox {
public:
    ConfirmUpdateDialog(int count, QWidget* parent);
};
QCoro::Task<bool> confirm_update(int count, QWidget* parent);
QCoro::Task<void> update_mods(core::CoreContext& ctx, const QStringList& uuids, QWidget* parent);
}
