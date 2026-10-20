#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <utility>

namespace px::modrim {

struct RestartCommand {
  QString program;
  QStringList arguments;
  QProcessEnvironment environment;
};

inline RestartCommand restartCommand(QString program,
                                     const QStringList& originalArguments,
                                     QProcessEnvironment environment,
                                     bool keepQtScale) {
  if (!keepQtScale) {
    environment.remove(QStringLiteral("QT_SCALE_FACTOR"));
  }
  return {std::move(program), originalArguments.sliced(1),
          std::move(environment)};
}

}  // namespace px::modrim
