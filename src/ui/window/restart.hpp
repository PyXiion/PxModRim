#pragma once

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <algorithm>
#include <functional>
#include <utility>

namespace px::modrim {

struct RestartCommand {
  QString program;
  QStringList arguments;
  QProcessEnvironment environment;
};

// Compares whole path components, so /tmp/app does not contain /tmp/app2.
inline bool isWithinDirectory(const QString& path, const QString& directory) {
  const auto pathParts = path.split(u'/', Qt::SkipEmptyParts);
  const auto directoryParts = directory.split(u'/', Qt::SkipEmptyParts);
  return pathParts.size() > directoryParts.size() &&
         std::equal(directoryParts.begin(), directoryParts.end(),
                    pathParts.begin());
}

// The program to relaunch: the AppImage when this executable runs from inside
// one, otherwise the executable itself. An AppImage runs its executable from a
// mount or extraction directory (APPDIR) that the runtime removes once this
// process exits. APPIMAGE alone is not proof, because processes started by
// another AppImage inherit it. `canonicalPath` returns an empty string for a
// missing path.
inline QString restartProgram(
    const QProcessEnvironment& environment, const QString& executable,
    const std::function<bool(const QString&)>& isFile,
    const std::function<QString(const QString&)>& canonicalPath) {
  const QString appImage = environment.value(QStringLiteral("APPIMAGE"));
  const QString appDir = environment.value(QStringLiteral("APPDIR"));
  if (appImage.isEmpty() || appDir.isEmpty() || !isFile(appImage)) {
    return executable;
  }
  // A missing APPDIR canonicalises to "", which would contain every path.
  const QString canonicalAppDir = canonicalPath(appDir);
  const bool inside =
      !canonicalAppDir.isEmpty() &&
      isWithinDirectory(canonicalPath(executable), canonicalAppDir);
  return inside ? appImage : executable;
}

// `keepQtScale` keeps a QT_SCALE_FACTOR the user set; otherwise the one this
// process exported is dropped so the saved scale applies again.
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
