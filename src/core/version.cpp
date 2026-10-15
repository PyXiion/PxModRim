#include "core/version.h"

#include <QCoro/QCoroTimer>
#include <QTimer>

namespace px::core {

QString appVersion() { return QStringLiteral("0.3.1"); }

QCoro::Task<QString> delayedVersion(int ms) {
    QTimer timer;
    timer.setSingleShot(true);
    timer.start(ms);
    co_await timer;
    co_return appVersion();
}

}  // namespace px::core
