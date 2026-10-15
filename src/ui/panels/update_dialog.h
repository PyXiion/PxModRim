#pragma once

#include <QDialog>
#include "core/services/update_service.h"

namespace px::ui {
inline constexpr int SKIP_RESULT = 2;

class UpdateDialog : public QDialog {
public:
    UpdateDialog(const core::services::ReleaseInfo& release, const QString& currentVersion,
                 QWidget* parent = nullptr);
};
}  // namespace px::ui
