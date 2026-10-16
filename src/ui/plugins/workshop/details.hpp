#pragma once

#include "core/workshop/catalog.hpp"

namespace px::modrim::workshop {

struct Detail {
  workshop::CatalogEntry item;
  std::vector<workshop::CatalogEntry> members;
  std::string warning;
  bool complete = false;

  bool operator==(const Detail&) const = default;
};

QCoro::Task<workshop::CatalogResult<Detail>> fetchDetail(
    workshop::WorkshopCatalog& catalog, QString itemId, QString kind);
QString incompletePlanNotice(const workshop::DownloadPlan& plan);
QString queuedNotice(qsizetype count);

}  // namespace px::modrim::workshop
