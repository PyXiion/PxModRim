#pragma once

#include <expected>
#include <map>
#include <utility>
#include <vector>

#include <QSet>
#include <QString>
#include <QStringList>

#include "core/error.hpp"

namespace px::modrim {

// Kahn's algorithm, layered the way the `toposort` package lays levels: level 0
// holds nodes without dependencies, and each later level holds nodes whose
// dependencies all sit in earlier levels. Names within a level are ascending,
// so the result is deterministic. Self-dependencies and dependencies that are
// not keys of `graph` are ignored.
inline Result<std::vector<QStringList>> toposort_levels(
    const std::map<QString, QSet<QString>>& graph) {
    std::map<QString, QSet<QString>> pending;
    for (const auto& [node, deps] : graph) {
        QSet<QString> edges;
        for (const auto& dep : deps) {
            if (dep != node && graph.contains(dep)) {
                edges.insert(dep);
            }
        }
        pending.emplace(node, std::move(edges));
    }

    std::vector<QStringList> levels;
    while (!pending.empty()) {
        QStringList level;
        for (const auto& [node, deps] : pending) {
            if (deps.isEmpty()) {
                level.append(node);
            }
        }
        if (level.isEmpty()) {
            QStringList stuck;
            for (const auto& [node, deps] : pending) {
                stuck.append(node);
            }
            return std::unexpected(Error{QStringLiteral("Circular dependencies exist among these plugins: %1")
                                             .arg(stuck.join(QStringLiteral(", ")))});
        }
        for (const auto& node : level) {
            pending.erase(node);
        }
        for (auto& [node, deps] : pending) {
            for (const auto& done : level) {
                deps.remove(done);
            }
        }
        levels.push_back(std::move(level));
    }
    return levels;
}

}  // namespace px::modrim
