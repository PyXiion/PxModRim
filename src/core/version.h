#pragma once

#include <QString>
#include <QCoro/QCoroTask>

namespace px::core {

QString appVersion();

// Smoke coroutine: proves QCoro runs on the Qt event loop.
QCoro::Task<QString> delayedVersion(int ms);

}  // namespace px::core
