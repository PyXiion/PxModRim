#include "core/checker/models.h"

namespace px::core::checker {

QString ModDiagnostics::error_tooltip() const {
    if (errors.isEmpty()) {
        return {};
    }
    if (errors.size() == 1) {
        return errors.front().short_message;
    }
    return QStringLiteral("%1 errors").arg(errors.size());
}

QString ModDiagnostics::warning_tooltip() const {
    if (warnings.isEmpty()) {
        return {};
    }
    if (warnings.size() == 1) {
        return warnings.front().short_message;
    }
    return QStringLiteral("%1 warnings").arg(warnings.size());
}

}  // namespace px::core::checker
