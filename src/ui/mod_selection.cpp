#include "ui/mod_selection.h"

#include <QSet>

#include "core/services/diagnostics_service.h"

namespace px::modrim {

ModSelectionPresenter::ModSelectionPresenter(CoreContext& ctx, ModInfoSink& sink)
    : ctx_(ctx), sink_(sink) {}

QCoro::Task<void> ModSelectionPresenter::show(QString uuid) {
    const std::uint64_t generation = ++generation_;

    if (uuid.isEmpty()) {
        sink_.clear();
        co_return;
    }

    const auto& mods = ctx_.allMods();
    const auto found = mods.constFind(uuid);
    if (found == mods.cend()) {
        sink_.clear();
        co_return;
    }

    const ListedMod& mod = *found.value();
    sink_.showMod(mod, ctx_.diagnosticsService().issues_for(uuid));

    std::optional<QString> packageId;
    if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod)) {
        packageId = about->package_id.str();
    }
    const QStringList activePackageIds = resolveActivePackageIds();

    if (generation != generation_) {
        co_return;
    }
    co_await sink_.setTimeAnalytics(packageId, activePackageIds);
}

void ModSelectionPresenter::clear() {
    ++generation_;
    sink_.clear();
}

QStringList ModSelectionPresenter::resolveActivePackageIds() const {
    const QSet<QString> active(ctx_.activeUuids().cbegin(), ctx_.activeUuids().cend());
    QStringList packageIds;
    for (const ListedModPtr& mod : ctx_.allMods()) {
        const auto* about = dynamic_cast<const AboutXmlMod*>(mod.get());
        if (about && active.contains(about->uuid())) {
            packageIds.append(about->package_id.str());
        }
    }
    return packageIds;
}

}  // namespace px::modrim
