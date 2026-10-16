#include "ui/components/mod_updates.h"
#include "ui/components/dialogs.h"
#include "core/context.h"
#include "core/downloads/manager.h"
#include <QAbstractButton>
#include <QAction>
#include <QDebug>
#include <QMenu>
namespace px::modrim {
QStringList updatable_ids(CoreContext& ctx, const QStringList& uuids) {
    QList<ListedModPtr> mods;
    mods.reserve(uuids.size());
    for (const auto& uuid : uuids) {
        const auto it = ctx.allMods().constFind(uuid);
        if (it != ctx.allMods().cend()) mods.append(it.value());
    }
    return ctx.downloads().updatable_ids(mods);
}
UpdateState update_state(CoreContext& ctx, const QStringList& uuids) {
    const auto ids = updatable_ids(ctx, uuids);
    QString label = QStringLiteral("Update");
    if (uuids.size() > 1) label += QStringLiteral(" (%1 of %2)").arg(ids.size()).arg(uuids.size());
    const auto& downloads = ctx.downloads();
    if (!downloads.available()) return {label, false, QStringLiteral("No download sources are enabled")};
    if (downloads.is_downloading()) return {label, false, QStringLiteral("A download is in progress")};
    if (ids.isEmpty()) return {label, false, QStringLiteral("None of these mods were downloaded by PxModRim")};
    return {label, true, {}};
}
QAction* add_update_action(QMenu* menu, CoreContext& ctx, const QStringList& uuids, std::function<void()> handler) {
    if (!ctx.downloads().available()) return nullptr;
    const auto state = update_state(ctx, uuids);
    auto* action = menu->addAction(state.label);
    QObject::connect(action, &QAction::triggered, menu, [handler = std::move(handler)] { handler(); });
    action->setEnabled(state.enabled); action->setToolTip(state.tooltip);
    return action;
}
ConfirmUpdateDialog::ConfirmUpdateDialog(int count, QWidget* parent) : QMessageBox(parent) {
    setIcon(QMessageBox::Question); setWindowTitle(QStringLiteral("Update Mods"));
    setText(QStringLiteral("Update %1 mods?").arg(count));
    setInformativeText(QStringLiteral("Every mod downloaded by PxModRim is checked against its source and changed files are downloaded. Mods managed by other tools, such as Steam's own Workshop folder, are not included. This can take a long time."));
    setStandardButtons(QMessageBox::Yes | QMessageBox::Cancel);
    setDefaultButton(QMessageBox::Cancel);
    button(QMessageBox::Yes)->setText(QStringLiteral("Update"));
    button(QMessageBox::Yes)->setObjectName(QStringLiteral("primaryAction"));
}
QCoro::Task<bool> confirm_update(int count, QWidget* parent) {
    if (count < CONFIRM_THRESHOLD) co_return true;
    const auto [result, dialog] = co_await awaitDialog<ConfirmUpdateDialog>(count, parent);
    co_return result == QMessageBox::Yes;
}
QCoro::Task<void> update_mods(CoreContext& ctx, const QStringList& uuids, QWidget* parent) {
    const auto ids = updatable_ids(ctx, uuids);
    if (ids.isEmpty() || !(co_await confirm_update(static_cast<int>(ids.size()), parent))) co_return;
    const auto result = co_await ctx.downloads().download_mods(ids);
    if (!result) qWarning() << "Update not started:" << result.error().message;
}
}
