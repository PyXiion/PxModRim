#include "core/version.hpp"

#include <QTimer>

#include <QCoro/QCoroTimer>

namespace px::modrim {

QString appVersion() { return QStringLiteral("0.3.1"); }

QCoro::Task<QString> delayedVersion(int ms) {
  QTimer timer;
  timer.setSingleShot(true);
  timer.start(ms);
  co_await timer;
  co_return appVersion();
}

}  // namespace px::modrim
