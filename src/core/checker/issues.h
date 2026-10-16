#pragma once

#include <optional>
#include <utility>

#include <QList>
#include <QSet>
#include <QString>

#include "core/checker/models.h"

namespace px::modrim::checker {

class ModIssueChecker {
public:
    virtual ~ModIssueChecker() = default;

    const QString& category_display_name() const noexcept { return category_display_name_; }

    // Order-independent results are reused when only the load order changes.
    bool order_dependent() const noexcept { return order_dependent_; }

    virtual bool should_check(const AboutXmlMod&, const CheckContext&) const { return true; }

    // Mods whose results may change when `moved` changed index; nullopt means all mods.
    virtual std::optional<QSet<PackageId>> affected_by_moves(const CheckContext&,
                                                             const QSet<PackageId>&) const {
        return std::nullopt;
    }

    virtual QList<ModIssue> check(const AboutXmlMod& mod, const CheckContext& ctx) const = 0;

protected:
    ModIssueChecker(QString category_display_name, bool order_dependent)
        : category_display_name_(std::move(category_display_name)),
          order_dependent_(order_dependent) {}

private:
    QString category_display_name_;
    bool order_dependent_;
};

// Missing or unsatisfied mod dependencies, honouring alternative package ids when enabled.
class DependencyIssueChecker final : public ModIssueChecker {
public:
    DependencyIssueChecker() : ModIssueChecker(QStringLiteral("Missing Dependency"), false) {}

    bool should_check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
    QList<ModIssue> check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
};

// Incompatibilities declared by the mod itself, and declared by other active mods against it.
class IncompatibilityIssueChecker final : public ModIssueChecker {
public:
    IncompatibilityIssueChecker() : ModIssueChecker(QStringLiteral("Incompatibility"), false) {}

    bool should_check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
    QList<ModIssue> check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
};

// Load-before and load-after constraints that the current order violates.
class LoadOrderIssueChecker final : public ModIssueChecker {
public:
    LoadOrderIssueChecker() : ModIssueChecker(QStringLiteral("Load Order"), true) {}

    bool should_check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
    std::optional<QSet<PackageId>> affected_by_moves(const CheckContext& ctx,
                                                     const QSet<PackageId>& moved) const override;
    QList<ModIssue> check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
};

// Membership in a dependency cycle reported by the graph.
class CycleIssueChecker final : public ModIssueChecker {
public:
    CycleIssueChecker() : ModIssueChecker(QStringLiteral("Cycle"), true) {}

    QList<ModIssue> check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
};

// Supported game versions that lack the target version.
class GameVersionIssueChecker final : public ModIssueChecker {
public:
    GameVersionIssueChecker() : ModIssueChecker(QStringLiteral("Version Mismatch"), false) {}

    bool should_check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
    QList<ModIssue> check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
};

// A known replacement for the mod's Workshop file id.
class ReplacementIssueChecker final : public ModIssueChecker {
public:
    ReplacementIssueChecker() : ModIssueChecker(QStringLiteral("Replacement Available"), false) {}

    QList<ModIssue> check(const AboutXmlMod& mod, const CheckContext& ctx) const override;
};

}  // namespace px::modrim::checker
