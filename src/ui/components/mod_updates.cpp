#include "ui/components/mod_updates.hpp"
#include "ui/components/dialogs.hpp"
#include "core/context.hpp"
#include "core/downloads/manager.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QDebug>
#include <QMenu>
namespace px::modrim {
QStringList updatableIds(CoreContext& ctx, const QStringList& uuids) {
    QList<ListedModPtr> mods;
    mods.reserve(uuids.size());
    for (const auto& uuid : uuids) {
        const auto it = ctx.allMods().constFind(uuid);
        if (it != ctx.allMods().cend()) mods.append(it.value());
    }
    return ctx.downloads().updatableIds(mods);
}
UpdateState updateState(CoreContext& ctx, const QStringList& uuids) {
    const auto ids = updatableIds(ctx, uuids);
    QString label = QStringLiteral("Update");
    if (uuids.size() > 1) label += QStringLiteral(" (%1 of %2)").arg(ids.size()).arg(uuids.size());
    const auto& downloads = ctx.downloads();
    if (!downloads.available()) return {label, false, QStringLiteral("No download sources are enabled")};
    if (downloads.isDownloading()) return {label, false, QStringLiteral("A download is in progress")};
    if (ids.isEmpty()) return {label, false, QStringLiteral("None of these mods were downloaded by PxModRim")};
    return {label, true, {}};
}
QAction* addUpdateAction(QMenu* menu, CoreContext& ctx, const QStringList& uuids, std::function<void()> handler) {
    if (!ctx.downloads().available()) return nullptr;
    const auto state = updateState(ctx, uuids);
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
QCoro::Task<bool> confirmUpdate(int count, QWidget* parent) {
    if (count < kConfirmThreshold) co_return true;
    const auto [result, dialog] = co_await awaitDialog<ConfirmUpdateDialog>(count, parent);
    co_return result == QMessageBox::Yes;
}
QCoro::Task<void> updateMods(CoreContext& ctx, const QStringList& uuids, QWidget* parent) {
    const auto ids = updatableIds(ctx, uuids);
    if (ids.isEmpty() || !(co_await confirmUpdate(static_cast<int>(ids.size()), parent))) co_return;
    const auto result = co_await ctx.downloads().downloadMods(ids);
    if (!result) qWarning() << "Update not started:" << result.error().message;
}
}
