#include "core/services/startup_impact_service/parser.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <spdlog/spdlog.h>

namespace px::core::startup_impact {

namespace {

constexpr QLatin1StringView kReportFilename{"StartupImpactData.json"};

// The game writes milliseconds; the rest of the app works in seconds.
double ms_to_s(double ms) { return ms / 1000.0; }

// Python's bool is an int subclass, so the legacy parser accepts true/false as 1/0 ms.
bool is_numeric(const QJsonValue& value) { return value.isDouble() || value.isBool(); }

double numeric_ms(const QJsonValue& value) {
    return value.isBool() ? static_cast<double>(value.toBool()) : value.toDouble();
}

double as_seconds(const QJsonValue& value) {
    if (is_numeric(value)) {
        return ms_to_s(numeric_ms(value));
    }
    if (value.isString()) {
        bool ok = false;
        const double ms = value.toString().toDouble(&ok);
        return ok ? ms_to_s(ms) : 0.0;
    }
    return 0.0;
}

// Non-numeric values are skipped, as the legacy parser skips them.
QMap<QString, double> as_seconds_map(const QJsonValue& value) {
    QMap<QString, double> result;
    if (!value.isObject()) {
        return result;
    }
    const QJsonObject object = value.toObject();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        if (is_numeric(it.value())) {
            result.insert(it.key(), ms_to_s(numeric_ms(it.value())));
        }
    }
    return result;
}

std::optional<StartupImpactMod> parse_mod(const QJsonValue& value) {
    if (!value.isObject()) {
        return std::nullopt;
    }
    const QJsonObject entry = value.toObject();
    const QJsonValue mod_name = entry.value(QStringLiteral("modName"));
    if (!mod_name.isString() || mod_name.toString().isEmpty()) {
        return std::nullopt;
    }

    const QJsonValue raw_package_id = entry.value(QStringLiteral("modPackageId"));
    std::optional<QString> package_id;
    if (raw_package_id.isString() && !raw_package_id.toString().isEmpty()) {
        package_id = normalize_package_id(raw_package_id.toString());
    }

    return StartupImpactMod{
        .mod_name = mod_name.toString(),
        .package_id = package_id,
        .total_impact_s = as_seconds(entry.value(QStringLiteral("totalImpact"))),
        .metrics = as_seconds_map(entry.value(QStringLiteral("metrics"))),
        .off_thread_metrics = as_seconds_map(entry.value(QStringLiteral("offThreadMetrics"))),
        .off_thread_total_impact_s =
            as_seconds(entry.value(QStringLiteral("offThreadTotalImpact"))),
    };
}

}  // namespace

QString get_startup_impact_path(const QString& config_folder) {
    // Mirrors Path(config_folder).parent: a trailing separator is ignored and a bare name
    // has the current directory as its parent.
    const QString clean = QDir::cleanPath(config_folder);
    const qsizetype slash = clean.lastIndexOf(u'/');
    const QString parent = slash < 0 ? QStringLiteral(".")
                           : slash == 0 ? QStringLiteral("/")
                                        : clean.left(slash);
    return QDir::cleanPath(QDir(parent).filePath(kReportFilename.toString()));
}

std::optional<StartupImpactReport> parse_startup_impact(const QString& path) {
    if (!QFileInfo::exists(path)) {
        spdlog::debug("Startup impact report not found at {}", path.toStdString());
        return std::nullopt;
    }

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        spdlog::warn("Failed to parse startup impact report at {}: {}", path.toStdString(),
                     file.errorString().toStdString());
        return std::nullopt;
    }

    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError) {
        spdlog::warn("Failed to parse startup impact report at {}: {}", path.toStdString(),
                     error.errorString().toStdString());
        return std::nullopt;
    }
    if (!document.isObject()) {
        spdlog::warn("Startup impact report is not a JSON object: {}", path.toStdString());
        return std::nullopt;
    }

    const QJsonObject data = document.object();
    StartupImpactReport report;
    report.path = QDir::cleanPath(path);
    report.loading_time_s = as_seconds(data.value(QStringLiteral("loadingTime")));
    if (const QJsonValue timestamp = data.value(QStringLiteral("timestamp"));
        timestamp.isString()) {
        report.timestamp = timestamp.toString();
    }
    report.metrics = as_seconds_map(data.value(QStringLiteral("metrics")));
    report.total_impact = as_seconds(data.value(QStringLiteral("totalImpact")));
    report.off_thread_metrics = as_seconds_map(data.value(QStringLiteral("offThreadMetrics")));
    report.off_thread_total_impact =
        as_seconds(data.value(QStringLiteral("offThreadTotalImpact")));

    // A non-array "mods" yields no entries, as iterating the legacy value did.
    for (const QJsonValue& entry : data.value(QStringLiteral("mods")).toArray()) {
        if (auto mod = parse_mod(entry)) {
            report.mods.append(std::move(*mod));
        }
    }
    return report;
}

}  // namespace px::core::startup_impact
