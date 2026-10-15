#pragma once

#include <memory>

#include <QTemporaryDir>

#include "core/config.h"
#include "core/context.h"

namespace px::ui_test {

// CoreContext services read their cache/config files through a ConfigService, so tests
// give every context one rooted in a directory that lives for the whole test run.
inline std::unique_ptr<px::core::CoreContext> makeCore(px::core::AppConfig config = {}) {
    static QTemporaryDir configDir;
    static px::core::ConfigService configService(configDir.path());
    return px::core::CoreContext::create(std::move(config), &configService);
}

}  // namespace px::ui_test
