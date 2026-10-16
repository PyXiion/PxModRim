#include "core/checker/models.hpp"

namespace px::modrim::checker {

QString ModDiagnostics::errorTooltip() const {
    if (errors.isEmpty()) {
        return {};
    }
    if (errors.size() == 1) {
        return errors.front().shortMessage;
    }
    return QStringLiteral("%1 errors").arg(errors.size());
}

QString ModDiagnostics::warningTooltip() const {
    if (warnings.isEmpty()) {
        return {};
    }
    if (warnings.size() == 1) {
        return warnings.front().shortMessage;
    }
    return QStringLiteral("%1 warnings").arg(warnings.size());
}

}  // namespace px::modrim::checker
