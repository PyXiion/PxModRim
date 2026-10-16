#include "ui/mod_selection.hpp"

#include <QSet>

#include "core/services/diagnostics_service.hpp"

namespace px::modrim {

ModSelectionPresenter::ModSelectionPresenter(CoreContext& ctx, ModInfoSink& sink)
    : m_ctx(ctx), m_sink(sink) {}

QCoro::Task<void> ModSelectionPresenter::show(QString uuid) {
    const std::uint64_t generation = ++m_generation;

    if (uuid.isEmpty()) {
        m_sink.clear();
        co_return;
    }

    const auto& mods = m_ctx.allMods();
    const auto found = mods.constFind(uuid);
    if (found == mods.cend()) {
        m_sink.clear();
        co_return;
    }

    const ListedMod& mod = *found.value();
    m_sink.showMod(mod, m_ctx.diagnosticsService().issuesFor(uuid));

    std::optional<QString> packageId;
    if (const auto* about = dynamic_cast<const AboutXmlMod*>(&mod)) {
        packageId = about->packageId.str();
    }
    const QStringList activePackageIds = resolveActivePackageIds();

    if (generation != m_generation) {
        co_return;
    }
    co_await m_sink.setTimeAnalytics(packageId, activePackageIds);
}

void ModSelectionPresenter::clear() {
    ++m_generation;
    m_sink.clear();
}

QStringList ModSelectionPresenter::resolveActivePackageIds() const {
    const QSet<QString> active(m_ctx.activeUuids().cbegin(), m_ctx.activeUuids().cend());
    QStringList packageIds;
    for (const ListedModPtr& mod : m_ctx.allMods()) {
        const auto* about = dynamic_cast<const AboutXmlMod*>(mod.get());
        if (about && active.contains(about->uuid())) {
            packageIds.append(about->packageId.str());
        }
    }
    return packageIds;
}

}  // namespace px::modrim
