#include "core/file_io.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace px::modrim {

Result<std::string> readFileBytes(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return std::unexpected(Error{file.errorString()});
    }
    const QByteArray bytes = file.readAll();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

Result<void> writeFileAtomically(const QString& path, std::string_view data) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        return std::unexpected(Error{QStringLiteral("Cannot create directory for %1").arg(path)});
    }
    // QSaveFile writes to a temporary file and renames it over the target on commit;
    // it discards the temporary file when the commit does not happen.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return std::unexpected(Error{file.errorString()});
    }
    if (file.write(data.data(), static_cast<qint64>(data.size())) != static_cast<qint64>(data.size())) {
        return std::unexpected(Error{file.errorString()});
    }
    if (!file.commit()) {
        return std::unexpected(Error{file.errorString()});
    }
    return {};
}

Result<void> copyFileReplacing(const QString& source, const QString& target) {
    QFile::remove(target);
    if (!QFile::copy(source, target)) {
        return std::unexpected(Error{QStringLiteral("Cannot copy %1 to %2").arg(source, target)});
    }
    return {};
}

}  // namespace px::modrim
