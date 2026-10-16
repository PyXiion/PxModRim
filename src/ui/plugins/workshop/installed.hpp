#pragma once

#include <cstddef>
#include <vector>

#include <QString>

#include "core/workshop/types.hpp"

namespace px::modrim::workshop {

inline constexpr int PAGE = 48;

class InstalledList {
public:
    const std::vector<workshop::CatalogMod>& mods() const { return mods_; }
    bool loaded() const { return !mods_.empty(); }
    int total() const { return static_cast<int>(matches_.size()); }
    bool has_more() const { return shown_ < matches_.size(); }

    void replace(std::vector<workshop::CatalogMod> mods);
    void search(QString query);
    void reset_paging();
    std::vector<workshop::CatalogMod> visible() const;
    std::vector<workshop::CatalogMod> next_page();

private:
    void rematch();
    std::vector<workshop::CatalogMod> slice(std::size_t start, std::size_t end) const;

    std::vector<workshop::CatalogMod> mods_;
    std::vector<std::size_t> matches_;
    QString query_;
    std::size_t shown_ = PAGE;
};

}  // namespace px::modrim::workshop
