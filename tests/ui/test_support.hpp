#pragma once

#include <memory>

#include <QTemporaryDir>

#include "core/config.hpp"
#include "core/context.hpp"

namespace px::ui_test {

// CoreContext services read their cache/config files through a ConfigService, so tests
// give every context one rooted in a directory that lives for the whole test run.
inline std::unique_ptr<px::modrim::CoreContext> makeCore(px::modrim::AppConfig config = {}) {
    static QTemporaryDir configDir;
    static px::modrim::ConfigService configService(configDir.path());
    return px::modrim::CoreContext::create(std::move(config), &configService);
}

}  // namespace px::ui_test
