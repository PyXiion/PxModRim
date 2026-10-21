#include "core/organizer/defaults.hpp"

#include <initializer_list>

namespace px::modrim::organizer {

namespace {

// Package ids come from each mod's own About.xml. Multi-category authors
// (brrainz, unlimitedhugs, bs, mlie) are matched by exact id only, and name
// heuristics such as "framework" or "performance" are avoided: they catch
// translations and unrelated content mods. Order matters (first match wins):
// libraries and performance precede the family prefixes that would swallow VE
// Framework and Dubs Performance Analyzer.

QList<StandardRule> ids(const QString& folder, RuleOp op,
                        std::initializer_list<const char*> patterns) {
  QList<StandardRule> rules;
  for (const char* pattern : patterns) {
    rules.append(
        {folder, RuleField::kPackageId, op, QString::fromLatin1(pattern)});
  }
  return rules;
}

QList<StandardRule> buildStandardRules() {
  QList<StandardRule> rules;
  rules += ids("Official", RuleOp::kPrefix, {"ludeon.rimworld"});
  rules += ids("Frameworks & Libraries", RuleOp::kEquals,
               {
                   "brrainz.harmony",
                   "unlimitedhugs.hugslib",
                   "erdelf.humanoidalienraces",
                   "oskarpotocki.vanillafactionsexpanded.core",
                   "imranfish.xmlextensions",
                   "zetrith.prepatcher",
                   "jecrell.jecstools",
                   "bs.fishery",
                   "smashphil.vehicleframework",
               });
  rules += ids("Performance", RuleOp::kEquals,
               {
                   "bs.performance",
                   "krkr.rocketman",
                   "taranchuk.performanceoptimizer",
               });
  rules +=
      ids("Performance", RuleOp::kPrefix, {"dubwise.dubsperformanceanalyzer"});
  rules += ids("Quality of Life", RuleOp::kEquals,
               {
                   "brrainz.achtung",
                   "brrainz.cameraplus",
                   "unlimitedhugs.allowtool",
                   "jaxe.rimhud",
                   "mehni.pickupandhaul",
                   "avilmask.commonsense",
                   "dhultgren.smarterconstruction",
                   "roolo.searchanddestroy",
               });
  rules += ids("Vanilla Expanded", RuleOp::kPrefix,
               {
                   "vanillaexpanded.",
                   "oskarpotocki.",
                   "vanillaracesexpanded.",
                   "vanillaquestsexpanded.",
                   "vanillastorytellersexpanded.",
               });
  rules.append({"Vanilla Expanded", RuleField::kAuthor, RuleOp::kContains,
                "oskar potocki"});
  rules += ids("Alpha Mods", RuleOp::kPrefix, {"sarg."});
  rules += ids("Combat Extended", RuleOp::kPrefix, {"ceteam."});
  rules.append({"Combat Extended", RuleField::kName, RuleOp::kContains,
                "combat extended"});
  rules += ids("Dubs Mods", RuleOp::kPrefix, {"dubwise."});
  return rules;
}

}  // namespace

const QList<StandardRule>& standardRules() {
  static const QList<StandardRule> rules = buildStandardRules();
  return rules;
}

}  // namespace px::modrim::organizer
