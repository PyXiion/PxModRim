#pragma once

#include "core/workshop/catalog.h"

namespace px::ui::workshop {

struct Detail {
    core::workshop::CatalogEntry item;
    std::vector<core::workshop::CatalogEntry> members;
    std::string warning;
    bool complete = false;

    bool operator==(const Detail&) const = default;
};

QCoro::Task<core::workshop::CatalogResult<Detail>> fetch_detail(
    core::workshop::WorkshopCatalog& catalog, QString item_id, QString kind);
QString incomplete_plan_notice(const core::workshop::DownloadPlan& plan);
QString queued_notice(qsizetype count);

}  // namespace px::ui::workshop
