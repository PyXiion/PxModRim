#pragma once

#include <QObject>

class QEvent;

namespace px::modrim {

// Gives every QQuickWidget's offscreen window its top-level window as transient
// parent when shown. Without it Wayland refuses Popup.Window popups (menus,
// combo box lists, tooltips): "Failed to create popup … transientParent".
// Install on the QApplication, parented to it.
class QuickPopupParenting : public QObject {
  Q_OBJECT

 public:
  explicit QuickPopupParenting(QObject* parent);

 protected:
  bool eventFilter(QObject* watched, QEvent* event) override;
};

}  // namespace px::modrim
