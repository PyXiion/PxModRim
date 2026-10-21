#include "core/services/startup_impact_service/parser.hpp"

#include <QByteArrayView>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <string_view>
#include <utility>

#include <glaze/glaze.hpp>
#include <spdlog/spdlog.h>

namespace px::modrim::startup_impact {
namespace {

const glz::json_t& field(const glz::json_t& object, std::string_view name) {
  static const glz::json_t kMissing;
  const auto& members = object.get_object();
  const auto it = members.find(name);
  return it == members.end() ? kMissing : it->second;
}

double durationSeconds(const glz::json_t& value) {
  if (value.is_number()) {
    return value.get_number() / 1000.0;
  }
  if (value.is_string()) {
    bool ok = false;
    const double number =
        QString::fromStdString(value.get_string()).toDouble(&ok);
    return ok ? number / 1000.0 : 0.0;
  }
  return 0.0;
}

QMap<QString, double> durationMap(const glz::json_t& value) {
  QMap<QString, double> result;
  if (value.is_object()) {
    for (const auto& [name, duration] : value.get_object()) {
      if (duration.is_number()) {
        result.insert(QString::fromStdString(name),
                      duration.get_number() / 1000.0);
      }
    }
  }
  return result;
}

}  // namespace

QString getStartupImpactPath(const QString& configFolder) {
  const QString folder = QDir::cleanPath(configFolder);
  return QDir(QFileInfo(folder).path())
      .filePath(QStringLiteral("StartupImpactData.json"));
}

std::optional<StartupImpactReport> parseStartupImpact(const QString& path) {
  QFile input(path);
  if (!input.open(QIODevice::ReadOnly)) {
    if (!input.exists()) {
      spdlog::debug("Startup impact report missing: {}", path.toStdString());
    } else {
      spdlog::warn("Cannot open startup impact report {}: {}",
                   path.toStdString(), input.errorString().toStdString());
    }
    return std::nullopt;
  }
  const QByteArray bytes = input.readAll();
  if (input.error() != QFileDevice::NoError) {
    spdlog::warn("Cannot read startup impact report {}: {}", path.toStdString(),
                 input.errorString().toStdString());
    return std::nullopt;
  }
  if (!QByteArrayView(bytes).isValidUtf8()) {
    spdlog::warn("Startup impact report is not UTF-8: {}", path.toStdString());
    return std::nullopt;
  }
  const std::string_view text(bytes.constData(),
                              static_cast<std::size_t>(bytes.size()));
  glz::json_t root;
  if (auto error = glz::read_json(root, text)) {
    spdlog::warn("Cannot parse startup impact report {}: {}",
                 path.toStdString(), glz::format_error(error, text));
    return std::nullopt;
  }
  if (!root.is_object()) {
    spdlog::warn("Startup impact report must be an object: {}",
                 path.toStdString());
    return std::nullopt;
  }

  StartupImpactReport report;
  report.path = path;
  report.loadingTimeS = durationSeconds(field(root, "loadingTime"));
  report.metrics = durationMap(field(root, "metrics"));
  report.offThreadMetrics = durationMap(field(root, "offThreadMetrics"));
  const auto& timestamp = field(root, "timestamp");
  if (timestamp.is_string()) {
    report.timestamp = QString::fromStdString(timestamp.get_string());
  }
  const auto& entries = field(root, "mods");
  if (entries.is_array()) {
    report.mods.reserve(static_cast<qsizetype>(entries.get_array().size()));
    for (const auto& entry : entries.get_array()) {
      if (!entry.is_object()) {
        continue;
      }
      const auto& name = field(entry, "modName");
      if (!name.is_string() || name.get_string().empty()) {
        continue;
      }
      StartupImpactMod mod;
      mod.modName = QString::fromStdString(name.get_string());
      const auto& packageId = field(entry, "modPackageId");
      if (packageId.is_string() && !packageId.get_string().empty()) {
        mod.packageId =
            normalizePackageId(QString::fromStdString(packageId.get_string()));
      }
      mod.totalImpactS = durationSeconds(field(entry, "totalImpact"));
      mod.metrics = durationMap(field(entry, "metrics"));
      mod.offThreadMetrics = durationMap(field(entry, "offThreadMetrics"));
      mod.offThreadTotalImpactS =
          durationSeconds(field(entry, "offThreadTotalImpact"));
      report.mods.append(std::move(mod));
    }
  }
  return report;
}

}  // namespace px::modrim::startup_impact
