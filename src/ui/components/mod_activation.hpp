#pragma once
#include <QCoro/QCoroTask>
#include <QMessageBox>
#include <QStringList>
namespace px::modrim { class CoreContext; }
namespace px::modrim {
class DependentModsDialog : public QMessageBox {
public:
    DependentModsDialog(const QStringList& dependents, QWidget* parent);
};
QCoro::Task<bool> applyActivation(CoreContext& ctx, QWidget* parent, const QStringList& enable = {}, const QStringList& disable = {});
QCoro::Task<bool> toggleMods(CoreContext& ctx, QWidget* parent, const QStringList& uuids);
}
