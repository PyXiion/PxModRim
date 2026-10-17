#pragma once

#include <QList>
#include <QSet>
#include <QString>
#include <optional>
#include <utility>

#include "core/checker/models.hpp"

namespace px::modrim::checker {

class ModIssueChecker {
 public:
  virtual ~ModIssueChecker() = default;

  const QString& categoryDisplayName() const noexcept {
    return m_categoryDisplayName;
  }

  // Order-independent results are reused when only the load order changes.
  bool orderDependent() const noexcept { return m_orderDependent; }

  virtual bool shouldCheck(const AboutXmlMod&, const CheckContext&) const {
    return true;
  }

  // Mods whose results may change when `moved` changed index; nullopt means all
  // mods.
  virtual std::optional<QSet<PackageId>> affectedByMoves(
      const CheckContext&, const QSet<PackageId>&) const {
    return std::nullopt;
  }

  virtual QList<ModIssue> check(const AboutXmlMod& mod,
                                const CheckContext& ctx) const = 0;

 protected:
  ModIssueChecker(QString categoryDisplayName, bool orderDependent)
      : m_categoryDisplayName(std::move(categoryDisplayName)),
        m_orderDependent(orderDependent) {}

 private:
  QString m_categoryDisplayName;
  bool m_orderDependent;
};

// Missing or unsatisfied mod dependencies, honouring alternative package ids
// when enabled.
class DependencyIssueChecker final : public ModIssueChecker {
 public:
  DependencyIssueChecker()
      : ModIssueChecker(QStringLiteral("Missing Dependency"), false) {}

  bool shouldCheck(const AboutXmlMod& mod,
                   const CheckContext& ctx) const override;
  QList<ModIssue> check(const AboutXmlMod& mod,
                        const CheckContext& ctx) const override;
};

// Incompatibilities declared by the mod itself, and declared by other active
// mods against it.
class IncompatibilityIssueChecker final : public ModIssueChecker {
 public:
  IncompatibilityIssueChecker()
      : ModIssueChecker(QStringLiteral("Incompatibility"), false) {}

  bool shouldCheck(const AboutXmlMod& mod,
                   const CheckContext& ctx) const override;
  QList<ModIssue> check(const AboutXmlMod& mod,
                        const CheckContext& ctx) const override;
};

// Load-before and load-after constraints that the current order violates.
class LoadOrderIssueChecker final : public ModIssueChecker {
 public:
  LoadOrderIssueChecker()
      : ModIssueChecker(QStringLiteral("Load Order"), true) {}

  bool shouldCheck(const AboutXmlMod& mod,
                   const CheckContext& ctx) const override;
  std::optional<QSet<PackageId>> affectedByMoves(
      const CheckContext& ctx, const QSet<PackageId>& moved) const override;
  QList<ModIssue> check(const AboutXmlMod& mod,
                        const CheckContext& ctx) const override;
};

// Membership in a dependency cycle reported by the graph.
class CycleIssueChecker final : public ModIssueChecker {
 public:
  CycleIssueChecker() : ModIssueChecker(QStringLiteral("Cycle"), true) {}

  QList<ModIssue> check(const AboutXmlMod& mod,
                        const CheckContext& ctx) const override;
};

// Supported game versions that lack the target version.
class GameVersionIssueChecker final : public ModIssueChecker {
 public:
  GameVersionIssueChecker()
      : ModIssueChecker(QStringLiteral("Version Mismatch"), false) {}

  bool shouldCheck(const AboutXmlMod& mod,
                   const CheckContext& ctx) const override;
  QList<ModIssue> check(const AboutXmlMod& mod,
                        const CheckContext& ctx) const override;
};

// A known replacement for the mod's Workshop file id.
class ReplacementIssueChecker final : public ModIssueChecker {
 public:
  ReplacementIssueChecker()
      : ModIssueChecker(QStringLiteral("Replacement Available"), false) {}

  QList<ModIssue> check(const AboutXmlMod& mod,
                        const CheckContext& ctx) const override;
};

}  // namespace px::modrim::checker
