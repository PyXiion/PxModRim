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

QList<StandardRule> exact(const QString& folder,
                          std::initializer_list<const char*> ids) {
  QList<StandardRule> rules;
  for (const char* id : ids) {
    rules.append({folder, RuleField::kPackageId, RuleOp::kEquals,
                  QString::fromLatin1(id)});
  }
  return rules;
}

QList<StandardRule> prefixes(const QString& folder,
                             std::initializer_list<const char*> prefixes) {
  QList<StandardRule> rules;
  for (const char* prefix : prefixes) {
    rules.append({folder, RuleField::kPackageId, RuleOp::kPrefix,
                  QString::fromLatin1(prefix)});
  }
  return rules;
}

QList<StandardRule> buildStandardRules() {
  QList<StandardRule> rules;
  const auto append = [&rules](const QList<StandardRule>& more) {
    rules.append(more);
  };
  const auto add = [&rules](StandardRule rule) {
    rules.append(std::move(rule));
  };

  append(prefixes("Official", {"ludeon.rimworld"}));
  append(exact("Frameworks & Libraries",
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
               }));
  append(exact("Performance", {
                                  "bs.performance",
                                  "krkr.rocketman",
                                  "taranchuk.performanceoptimizer",
                              }));
  append(prefixes("Performance", {"dubwise.dubsperformanceanalyzer"}));
  append(exact("Quality of Life", {
                                      "brrainz.achtung",
                                      "brrainz.cameraplus",
                                      "unlimitedhugs.allowtool",
                                      "jaxe.rimhud",
                                      "mehni.pickupandhaul",
                                      "avilmask.commonsense",
                                      "dhultgren.smarterconstruction",
                                      "roolo.searchanddestroy",
                                  }));
  append(prefixes("Vanilla Expanded", {
                                          "vanillaexpanded.",
                                          "oskarpotocki.",
                                          "vanillaracesexpanded.",
                                          "vanillaquestsexpanded.",
                                          "vanillastorytellersexpanded.",
                                      }));
  add({"Vanilla Expanded", RuleField::kAuthor, RuleOp::kContains,
       "oskar potocki"});
  append(prefixes("Alpha Mods", {"sarg."}));
  append(prefixes("Combat Extended", {"ceteam."}));
  add({"Combat Extended", RuleField::kName, RuleOp::kContains,
       "combat extended"});
  append(prefixes("Dubs Mods", {"dubwise."}));
  return rules;
}

}  // namespace

const QList<StandardRule>& standardRules() {
  static const QList<StandardRule> rules = buildStandardRules();
  return rules;
}

}  // namespace px::modrim::organizer
