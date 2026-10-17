#pragma once

#include <QDialog>

#include "core/services/update_service.hpp"

namespace px::modrim {
inline constexpr int kSkipResult = 2;

class UpdateDialog : public QDialog {
 public:
  UpdateDialog(const services::ReleaseInfo& release,
               const QString& currentVersion, QWidget* parent = nullptr);
};
}  // namespace px::modrim
