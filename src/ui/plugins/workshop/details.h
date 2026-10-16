#pragma once

#include "core/workshop/catalog.h"

namespace px::modrim::workshop {

struct Detail {
    workshop::CatalogEntry item;
    std::vector<workshop::CatalogEntry> members;
    std::string warning;
    bool complete = false;

    bool operator==(const Detail&) const = default;
};

QCoro::Task<workshop::CatalogResult<Detail>> fetch_detail(
    workshop::WorkshopCatalog& catalog, QString item_id, QString kind);
QString incomplete_plan_notice(const workshop::DownloadPlan& plan);
QString queued_notice(qsizetype count);

}  // namespace px::modrim::workshop
