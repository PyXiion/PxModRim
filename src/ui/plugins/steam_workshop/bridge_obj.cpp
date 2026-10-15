#include "ui/plugins/steam_workshop/bridge_obj.h"

#ifdef PX_HAS_STEAM

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>

#include "ui/plugins/steam_workshop/plugin.h"

namespace px::ui {
namespace {
bool is_published_file_id(const QJsonValue& value) {
    if (!value.isString()) return false;
    const auto text = value.toString();
    return !text.isEmpty() && std::ranges::all_of(text, [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); });
}
}

PxModRimBridge::PxModRimBridge(SteamWorkshopUiPlugin& plugin, QObject* parent)
    : QObject(parent), plugin_(plugin) {}

void PxModRimBridge::call(const QString& request_id, const QString& method, const QString& payload) {
    handle(request_id, method, payload);
}

void PxModRimBridge::respond(const QString& request_id, bool ok, const QJsonValue& result, const QString& error) {
    QJsonObject response{{QStringLiteral("ok"), ok}};
    if (ok) response.insert(QStringLiteral("result"), result);
    else response.insert(QStringLiteral("error"), error);
    emit result_ready(request_id, QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)));
}

QCoro::Task<void> PxModRimBridge::handle(QString request_id, QString method, QString payload) {
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(payload.isEmpty() ? QByteArray("{}") : payload.toUtf8(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
        respond(request_id, false, {}, QStringLiteral("Invalid request payload"));
        co_return;
    }
    const auto params = document.object();
    const auto mod_id = params.value(QStringLiteral("mod_id"));
    if (method == QLatin1String("init_ready")) {
        plugin_.sync_all();
    } else if (method == QLatin1String("toggle_download_checked")) {
        if (is_published_file_id(mod_id)) plugin_.toggle_download_checked(mod_id.toString(), params.value(QStringLiteral("title")).toString(), params.value(QStringLiteral("checked")).toBool());
    } else if (method == QLatin1String("batch_toggle_download_checked")) {
        const auto ids = params.value(QStringLiteral("mod_ids"));
        const auto titles = params.value(QStringLiteral("titles"));
        if (!ids.isArray() || !titles.isArray() || ids.toArray().size() != titles.toArray().size()) {
            respond(request_id, false, {}, QStringLiteral("mod_ids and titles must have equal lengths"));
            co_return;
        }
        const auto id_array = ids.toArray();
        const auto title_array = titles.toArray();
        QStringList valid_ids;
        QStringList valid_titles;
        for (qsizetype pos = 0; pos < id_array.size(); ++pos) {
            if (!is_published_file_id(id_array[pos])) continue;
            valid_ids.append(id_array[pos].toString());
            valid_titles.append(title_array[pos].toString());
        }
        if (!valid_ids.isEmpty()) plugin_.batch_toggle_download_checked(valid_ids, valid_titles, params.value(QStringLiteral("checked")).toBool());
    } else if (method == QLatin1String("toggle_active")) {
        if (is_published_file_id(mod_id)) plugin_.toggle_active(mod_id.toString(), params.value(QStringLiteral("active")).toBool());
    } else if (method == QLatin1String("batch_toggle_active")) {
        const auto ids = params.value(QStringLiteral("mod_ids"));
        if (!ids.isArray()) {
            respond(request_id, false, {}, QStringLiteral("mod_ids must be an array"));
            co_return;
        }
        QStringList valid_ids;
        for (const auto& id : ids.toArray()) {
            if (is_published_file_id(id)) valid_ids.append(id.toString());
        }
        if (!valid_ids.isEmpty()) plugin_.batch_toggle_active(valid_ids, params.value(QStringLiteral("active")).toBool());
    } else if (method == QLatin1String("fetch_mod_deps")) {
        if (is_published_file_id(mod_id)) {
            QPointer<PxModRimBridge> guard(this);
            auto result = co_await plugin_.fetch_mod_deps(mod_id.toString());
            if (guard) {
                if (result) guard->respond(request_id, true, *result);
                else guard->respond(request_id, false, {}, result.error().message);
            }
            co_return;
        }
    } else {
        respond(request_id, false, {}, QStringLiteral("Unknown method: %1").arg(method));
        co_return;
    }
    respond(request_id, true);
}

}  // namespace px::ui
#endif
