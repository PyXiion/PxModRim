#include "ui/plugins/workshop/installed.h"

#include <algorithm>
#include <utility>

#include <QStringList>

namespace px::ui::workshop {

void InstalledList::replace(std::vector<core::workshop::CatalogMod> mods) {
    mods_ = std::move(mods);
    rematch();
}

void InstalledList::search(QString query) {
    query_ = std::move(query);
    reset_paging();
    rematch();
}

void InstalledList::reset_paging() {
    shown_ = PAGE;
}

std::vector<core::workshop::CatalogMod> InstalledList::visible() const {
    return slice(0, std::min(shown_, matches_.size()));
}

std::vector<core::workshop::CatalogMod> InstalledList::next_page() {
    const auto start = shown_;
    shown_ = std::min(start + PAGE, matches_.size());
    return slice(start, shown_);
}

std::vector<core::workshop::CatalogMod> InstalledList::slice(std::size_t start, std::size_t end) const {
    std::vector<core::workshop::CatalogMod> result;
    if (start >= end) return result;
    result.reserve(end - start);
    for (auto i = start; i < end; ++i) result.push_back(mods_[matches_[i]]);
    return result;
}

void InstalledList::rematch() {
    matches_.clear();
    const QString needle = query_.toCaseFolded();
    for (std::size_t i = 0; i < mods_.size(); ++i) {
        const auto& mod = mods_[i];
        if (needle.isEmpty()) {
            matches_.push_back(i);
            continue;
        }
        QStringList fields{QString::fromStdString(mod.title),
                           mod.author.name ? QString::fromStdString(*mod.author.name) : QString{},
                           QString::fromStdString(mod.id)};
        for (const auto& tag : mod.tags) fields.append(QString::fromStdString(tag));
        if (fields.join(' ').toCaseFolded().contains(needle)) matches_.push_back(i);
    }
}

}  // namespace px::ui::workshop
