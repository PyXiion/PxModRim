#pragma once

#include <QDialog>
#include <optional>

#include <QCoro/QCoroTask>

namespace px::modrim {
class CoreContext;
}
namespace px::modrim {
class Notifier;
enum class UploadChoice {
  kCancel = QDialog::Rejected,
  kUpload = QDialog::Accepted,
  kSaveToFile = 2
};
class UploadConfirmDialog : public QDialog {
 public:
  explicit UploadConfirmDialog(QWidget* parent = nullptr);
};
class UploadFailedDialog : public QDialog {
 public:
  explicit UploadFailedDialog(const QString& errorMessage,
                              QWidget* parent = nullptr);
};
QCoro::Task<std::optional<QString>> handleUploadReport(QWidget* parent,
                                                       CoreContext& ctx,
                                                       Notifier& toast);
}  // namespace px::modrim
