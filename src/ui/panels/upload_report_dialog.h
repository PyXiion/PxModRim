#pragma once

#include <optional>
#include <QCoro/QCoroTask>
#include <QDialog>

namespace px::core { class CoreContext; }
namespace px::ui {
class Notifier;
enum class UploadChoice { CANCEL = QDialog::Rejected, UPLOAD = QDialog::Accepted, SAVE_TO_FILE = 2 };
class UploadConfirmDialog : public QDialog {
public:
    explicit UploadConfirmDialog(QWidget* parent = nullptr);
};
class UploadFailedDialog : public QDialog {
public:
    explicit UploadFailedDialog(const QString& errorMessage, QWidget* parent = nullptr);
};
QCoro::Task<std::optional<QString>> handle_upload_report(QWidget* parent, core::CoreContext& ctx,
                                                        Notifier& toast);
}  // namespace px::ui
