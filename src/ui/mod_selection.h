#pragma once

#include <cstdint>
#include <optional>

#include <QCoro/QCoroTask>
#include <QList>
#include <QString>
#include <QStringList>

#include "core/context.h"
#include "core/models/metadata/structures.h"
#include "core/models/view/diagnostics.h"

namespace px::modrim {

// The mod-details surface the presenter drives. The mod info panel implements it.
class ModInfoSink {
public:
    virtual ~ModInfoSink() = default;

    virtual void clear() = 0;
    virtual void showMod(const ListedMod& mod, const QList<view::ModIssueView>& issues) = 0;
    // `packageId` is nullopt for mods that have no package id (non-About.xml mods).
    virtual QCoro::Task<void> setTimeAnalytics(const std::optional<QString>& packageId,
                                               const QStringList& activePackageIds) = 0;
};

// Loads a selected mod's details into a ModInfoSink for any view.
class ModSelectionPresenter {
public:
    ModSelectionPresenter(CoreContext& ctx, ModInfoSink& sink);

    // A stale show() (superseded by a later one or clear()) stops before its analytics await.
    QCoro::Task<void> show(QString uuid);
    void clear();

private:
    QStringList resolveActivePackageIds() const;

    CoreContext& ctx_;
    ModInfoSink& sink_;
    std::uint64_t generation_ = 0;
};

}  // namespace px::modrim
