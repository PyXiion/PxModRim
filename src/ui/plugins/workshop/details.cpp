#include "ui/plugins/workshop/details.hpp"

namespace px::modrim::workshop {
namespace {
QString joined(const std::vector<std::string>& ids) {
    QStringList values;
    values.reserve(static_cast<qsizetype>(ids.size()));
    for (const auto& id : ids) values.append(QString::fromStdString(id));
    return values.join(QStringLiteral(", "));
}
}

QCoro::Task<workshop::CatalogResult<Detail>> fetchDetail(
    workshop::WorkshopCatalog& catalog, QString itemId, QString kind) {
    using namespace workshop;
    if (kind == QStringLiteral("collection")) {
        auto result = co_await catalog.collection(itemId);
        if (!result) co_return std::unexpected(result.error());
        if (!*result) co_return std::unexpected(CatalogError{QStringLiteral("This collection is no longer available."), 404});
        auto& value = **result;
        QString warning;
        if (!value.is_complete)
            warning = QStringLiteral("This collection is incomplete. Some members are unavailable or not listed.");
        if (!value.unavailable_ids.empty()) warning += QStringLiteral(" Unavailable: ") + joined(value.unavailable_ids);
        co_return Detail{std::move(value.collection), std::move(value.members), warning.toStdString(), value.is_complete};
    }
    auto result = co_await catalog.mod(itemId);
    if (!result) co_return std::unexpected(result.error());
    if (!*result) co_return std::unexpected(CatalogError{QStringLiteral("This mod is no longer available."), 404});
    auto mod = std::move(**result);
    std::vector<CatalogEntry> members;
    QStringList missing;
    // Start every dependency request before waiting for the first one.
    std::vector<QCoro::Task<CatalogResult<std::optional<CatalogMod>>>> tasks;
    tasks.reserve(mod.dependencies.size());
    for (const auto& id : mod.dependencies) tasks.push_back(catalog.mod(QString::fromStdString(id)));
    for (std::size_t index = 0; index < tasks.size(); ++index) {
        auto dependency = co_await tasks[index];
        if (!dependency) co_return std::unexpected(dependency.error());
        if (*dependency) members.emplace_back(std::move(**dependency));
        else missing.append(QString::fromStdString(mod.dependencies[index]));
    }
    const auto warning = missing.isEmpty() ? QString{} : QStringLiteral("Unavailable dependencies: ") + missing.join(QStringLiteral(", "));
    co_return Detail{std::move(mod), std::move(members), warning.toStdString(), missing.isEmpty()};
}

QString incompletePlanNotice(const workshop::DownloadPlan& plan) {
    QStringList parts{QStringLiteral("The download plan is incomplete.")};
    if (!plan.unavailable_ids.isEmpty()) parts.append(QStringLiteral("Unavailable: ") + plan.unavailable_ids.join(QStringLiteral(", ")));
    if (!plan.incomplete_collection_ids.isEmpty()) parts.append(QStringLiteral("Incomplete collections: ") + plan.incomplete_collection_ids.join(QStringLiteral(", ")));
    return parts.join(QLatin1Char(' '));
}

QString queuedNotice(qsizetype count) {
    return QStringLiteral("Queued %1 mod%2. Progress is shown in the header and results in Downloads. Mods are not activated.")
        .arg(count).arg(count == 1 ? QString{} : QStringLiteral("s"));
}

}  // namespace px::modrim::workshop
