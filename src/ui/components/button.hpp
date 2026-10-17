#pragma once

#include <QPushButton>
#include <QString>

class QWidget;

namespace px::modrim {

// Push button in the application's standard look (QSS `appBtn`) with a
// pointing-hand cursor.
class AppButton : public QPushButton {
 public:
  explicit AppButton(const QString& text = {}, QWidget* parent = nullptr);
};

}  // namespace px::modrim
