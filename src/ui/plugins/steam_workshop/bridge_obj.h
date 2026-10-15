#pragma once

#ifdef PX_HAS_STEAM

#include <QCoro/QCoroTask>
#include <QJsonValue>
#include <QObject>

namespace px::ui {

class SteamWorkshopUiPlugin;

class PxModRimBridge final : public QObject {
    Q_OBJECT
public:
    PxModRimBridge(SteamWorkshopUiPlugin& plugin, QObject* parent);
    Q_INVOKABLE void call(const QString& request_id, const QString& method, const QString& payload);
signals:
    void result_ready(const QString& request_id, const QString& payload);
private:
    QCoro::Task<void> handle(QString request_id, QString method, QString payload);
    void respond(const QString& request_id, bool ok, const QJsonValue& result = QJsonValue(QJsonValue::Null), const QString& error = {});
    SteamWorkshopUiPlugin& plugin_;
};

}  // namespace px::ui
#endif
