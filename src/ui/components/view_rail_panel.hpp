#pragma once

#include <QQuickWidget>
#include <QString>
#include <QVariantList>

class QQmlEngine;
class QWidget;

namespace px::modrim {

// Hosts IconRail.qml, the left navigation rail. `tabs` are maps with `icon` and
// `label` keys; `helpEntries` are maps with `id` and `label` keys for the Help
// menu.
class ViewRailPanel : public QQuickWidget {
  Q_OBJECT

 public:
  ViewRailPanel(const QVariantList& tabs, const QVariantList& helpEntries,
                QQmlEngine* engine, QWidget* parent = nullptr);

  // Moves the highlighted tab without emitting currentChanged.
  void setCurrent(int index);

 signals:
  void currentChanged(int index);
  void hovered(int index);
  void settingsRequested();
  void helpActionRequested(const QString& actionId);
};

}  // namespace px::modrim
