#include "ui/components/mod_activation.hpp"
#include "ui/components/dialogs.hpp"
#include "core/context.hpp"
#include "core/services/activation_service.hpp"
#include <QAbstractButton>
namespace px::modrim {
DependentModsDialog::DependentModsDialog(const QStringList& dependents, QWidget* parent) : QMessageBox(parent) {
    setIcon(QMessageBox::Warning);
    setWindowTitle(QStringLiteral("Disable Dependent Mods"));
    QStringList details;
    for (const auto& dependent : dependents) details.append(QStringLiteral("• %1").arg(dependent));
    setText(QStringLiteral("These active mods depend on the mod you are disabling:\n\n") + details.join('\n'));
    setInformativeText(QStringLiteral("Choose whether to disable only the selected mod or these dependents too."));
    setStandardButtons(QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
    setDefaultButton(QMessageBox::No);
    button(QMessageBox::Yes)->setText(QStringLiteral("Disable selected + dependents"));
    button(QMessageBox::No)->setText(QStringLiteral("Disable selected only"));
    button(QMessageBox::Yes)->setObjectName(QStringLiteral("primaryAction"));
}
QCoro::Task<bool> apply_activation(CoreContext& ctx, QWidget* parent, const QStringList& enable, const QStringList& disable) {
    // Copy before suspension: callers often pass temporary batches.
    const QStringList enabling = enable;
    QStringList disabling = disable;
    const auto dependents = ctx.activation().dependents_of(disabling, enabling);
    if (!dependents.isEmpty()) {
        QStringList details;
        for (const auto& uuid : dependents) {
            const auto& mod = ctx.allMods().value(uuid);
            const auto* about = dynamic_cast<const AboutXmlMod*>(mod.get());
            details.append(about ? QStringLiteral("%1 (%2)").arg(mod->name, about->package_id.str()) : mod->name);
        }
        const auto [result, dialog] = co_await awaitDialog<DependentModsDialog>(details, parent);
        if (result == QMessageBox::Cancel) co_return false;
        if (result == QMessageBox::Yes) disabling.append(dependents);
    }
    co_return ctx.activation().apply(enabling, disabling);
}
QCoro::Task<bool> toggle_mods(CoreContext& ctx, QWidget* parent, const QStringList& uuids) {
    QStringList enable, disable;
    for (const auto& uuid : uuids) (ctx.activeUuids().contains(uuid) ? disable : enable).append(uuid);
    co_return co_await apply_activation(ctx, parent, enable, disable);
}
}
