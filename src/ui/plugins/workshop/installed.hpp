#pragma once

#include <QString>
#include <cstddef>
#include <vector>

#include "core/workshop/types.hpp"

namespace px::modrim::workshop {

inline constexpr int kPage = 48;

class InstalledList {
 public:
  const std::vector<workshop::CatalogMod>& mods() const { return m_mods; }
  bool loaded() const { return !m_mods.empty(); }
  int total() const { return static_cast<int>(m_matches.size()); }
  bool hasMore() const { return m_shown < m_matches.size(); }

  void replace(std::vector<workshop::CatalogMod> mods);
  void search(QString query);
  void resetPaging();
  std::vector<workshop::CatalogMod> visible() const;
  std::vector<workshop::CatalogMod> nextPage();

 private:
  void rematch();
  std::vector<workshop::CatalogMod> slice(std::size_t start,
                                          std::size_t end) const;

  std::vector<workshop::CatalogMod> m_mods;
  std::vector<std::size_t> m_matches;
  QString m_query;
  std::size_t m_shown = kPage;
};

}  // namespace px::modrim::workshop
