#include "ui/plugins/workshop/installed.hpp"

#include <QStringList>
#include <algorithm>
#include <utility>

namespace px::modrim::workshop {

void InstalledList::replace(std::vector<workshop::CatalogMod> mods) {
  m_mods = std::move(mods);
  rematch();
}

void InstalledList::search(QString query) {
  m_query = std::move(query);
  resetPaging();
  rematch();
}

void InstalledList::resetPaging() { m_shown = kPage; }

std::vector<workshop::CatalogMod> InstalledList::visible() const {
  return slice(0, std::min(m_shown, m_matches.size()));
}

std::vector<workshop::CatalogMod> InstalledList::nextPage() {
  const auto start = m_shown;
  m_shown = std::min(start + kPage, m_matches.size());
  return slice(start, m_shown);
}

std::vector<workshop::CatalogMod> InstalledList::slice(std::size_t start,
                                                       std::size_t end) const {
  std::vector<workshop::CatalogMod> result;
  if (start >= end) return result;
  result.reserve(end - start);
  for (auto i = start; i < end; ++i) result.push_back(m_mods[m_matches[i]]);
  return result;
}

void InstalledList::rematch() {
  m_matches.clear();
  const QString needle = m_query.toCaseFolded();
  for (std::size_t i = 0; i < m_mods.size(); ++i) {
    const auto& mod = m_mods[i];
    if (needle.isEmpty()) {
      m_matches.push_back(i);
      continue;
    }
    QStringList fields{
        QString::fromStdString(mod.title),
        mod.author.name ? QString::fromStdString(*mod.author.name) : QString{},
        QString::fromStdString(mod.id)};
    for (const auto& tag : mod.tags) fields.append(QString::fromStdString(tag));
    if (fields.join(' ').toCaseFolded().contains(needle))
      m_matches.push_back(i);
  }
}

}  // namespace px::modrim::workshop
