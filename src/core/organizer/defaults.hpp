#pragma once

#include <QList>
#include <QString>

#include "core/organizer/models.hpp"

namespace px::modrim::organizer {

// A built-in auto-folder rule targeting a top-level folder by name.
struct StandardRule {
  QString folder;
  RuleField field = RuleField::kPackageId;
  RuleOp op = RuleOp::kEquals;
  QString pattern;
};

// Built-in rules in priority order: the first match wins.
const QList<StandardRule>& standardRules();

}  // namespace px::modrim::organizer
