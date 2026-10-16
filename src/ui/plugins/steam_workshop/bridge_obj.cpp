#include "ui/plugins/steam_workshop/bridge_obj.hpp"

#ifdef PX_HAS_STEAM

#include <algorithm>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QPointer>

#include "ui/plugins/steam_workshop/plugin.hpp"

namespace px::modrim {
namespace {
bool isPublishedFileId(const QJsonValue& value) {
    if (!value.isString()) return false;
    const auto text = value.toString();
    return !text.isEmpty() && std::ranges::all_of(text, [](QChar c) { return c >= QLatin1Char('0') && c <= QLatin1Char('9'); });
}
}

PxModRimBridge::PxModRimBridge(SteamWorkshopUiPlugin& plugin, QObject* parent)
    : QObject(parent), m_plugin(plugin) {}

void PxModRimBridge::call(const QString& requestId, const QString& method, const QString& payload) {
    handle(requestId, method, payload);
}

void PxModRimBridge::respond(const QString& requestId, bool ok, const QJsonValue& result, const QString& error) {
    QJsonObject response{{QStringLiteral("ok"), ok}};
    if (ok) response.insert(QStringLiteral("result"), result);
    else response.insert(QStringLiteral("error"), error);
    emit resultReady(requestId, QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact)));
}

QCoro::Task<void> PxModRimBridge::handle(QString requestId, QString method, QString payload) {
    QJsonParseError parseError;
    const auto document = QJsonDocument::fromJson(payload.isEmpty() ? QByteArray("{}") : payload.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        respond(requestId, false, {}, QStringLiteral("Invalid request payload"));
        co_return;
    }
    const auto params = document.object();
    const auto modId = params.value(QStringLiteral("mod_id"));
    if (method == QLatin1String("init_ready")) {
        m_plugin.syncAll();
    } else if (method == QLatin1String("toggle_download_checked")) {
        if (isPublishedFileId(modId)) m_plugin.toggleDownloadChecked(modId.toString(), params.value(QStringLiteral("title")).toString(), params.value(QStringLiteral("checked")).toBool());
    } else if (method == QLatin1String("batch_toggle_download_checked")) {
        const auto ids = params.value(QStringLiteral("mod_ids"));
        const auto titles = params.value(QStringLiteral("titles"));
        if (!ids.isArray() || !titles.isArray() || ids.toArray().size() != titles.toArray().size()) {
            respond(requestId, false, {}, QStringLiteral("mod_ids and titles must have equal lengths"));
            co_return;
        }
        const auto idArray = ids.toArray();
        const auto titleArray = titles.toArray();
        QStringList validIds;
        QStringList validTitles;
        for (qsizetype pos = 0; pos < idArray.size(); ++pos) {
            if (!isPublishedFileId(idArray[pos])) continue;
            validIds.append(idArray[pos].toString());
            validTitles.append(titleArray[pos].toString());
        }
        if (!validIds.isEmpty()) m_plugin.batchToggleDownloadChecked(validIds, validTitles, params.value(QStringLiteral("checked")).toBool());
    } else if (method == QLatin1String("toggle_active")) {
        if (isPublishedFileId(modId)) m_plugin.toggleActive(modId.toString(), params.value(QStringLiteral("active")).toBool());
    } else if (method == QLatin1String("batch_toggle_active")) {
        const auto ids = params.value(QStringLiteral("mod_ids"));
        if (!ids.isArray()) {
            respond(requestId, false, {}, QStringLiteral("mod_ids must be an array"));
            co_return;
        }
        QStringList validIds;
        for (const auto& id : ids.toArray()) {
            if (isPublishedFileId(id)) validIds.append(id.toString());
        }
        if (!validIds.isEmpty()) m_plugin.batchToggleActive(validIds, params.value(QStringLiteral("active")).toBool());
    } else if (method == QLatin1String("fetch_mod_deps")) {
        if (isPublishedFileId(modId)) {
            QPointer<PxModRimBridge> guard(this);
            auto result = co_await m_plugin.fetchModDeps(modId.toString());
            if (guard) {
                if (result) guard->respond(requestId, true, *result);
                else guard->respond(requestId, false, {}, result.error().message);
            }
            co_return;
        }
    } else {
        respond(requestId, false, {}, QStringLiteral("Unknown method: %1").arg(method));
        co_return;
    }
    respond(requestId, true);
}

}  // namespace px::modrim
#endif
