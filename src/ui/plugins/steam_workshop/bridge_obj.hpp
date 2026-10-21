#pragma once

#ifdef PX_HAS_STEAM

#include <QJsonValue>
#include <QObject>
#include <QPointer>

#include <QCoro/QCoroTask>

namespace px::modrim {

class SteamWorkshopUiPlugin;

class PxModRimBridge final : public QObject {
  Q_OBJECT
 public:
  PxModRimBridge(SteamWorkshopUiPlugin& plugin, QObject* parent);
  void setPage(QObject* page);
  Q_INVOKABLE void call(const QString& requestId, const QString& method,
                        const QString& payload);
 signals:
  void resultReady(const QString& requestId, const QString& payload);

 private:
  QCoro::Task<void> handle(QString requestId, QString method, QString payload);
  void respond(const QString& requestId, bool ok,
               const QJsonValue& result = QJsonValue(QJsonValue::Null),
               const QString& error = {});
  SteamWorkshopUiPlugin& m_plugin;
  QPointer<QObject> m_page;
};

}  // namespace px::modrim
#endif
