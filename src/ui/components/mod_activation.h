#pragma once
#include <QCoro/QCoroTask>
#include <QMessageBox>
#include <QStringList>
namespace px::core { class CoreContext; }
namespace px::ui {
class DependentModsDialog : public QMessageBox {
public:
    DependentModsDialog(const QStringList& dependents, QWidget* parent);
};
QCoro::Task<bool> apply_activation(core::CoreContext& ctx, QWidget* parent, const QStringList& enable = {}, const QStringList& disable = {});
QCoro::Task<bool> toggle_mods(core::CoreContext& ctx, QWidget* parent, const QStringList& uuids);
}
