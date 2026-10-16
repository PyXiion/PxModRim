#include "ui/panels/upload_report_dialog.hpp"

#include <QClipboard>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QSaveFile>
#include <QVBoxLayout>

#include "core/providers/base.hpp"
#include "core/support/report.hpp"
#include "ui/components/dialogs.hpp"
#include "ui/context.hpp"

namespace px::modrim {
UploadConfirmDialog::UploadConfirmDialog(QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("uploadConfirmDialog"));
    setWindowTitle(tr("Upload Log & System Info"));
    setModal(true);
    resize(520, 320);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    auto* description = new QLabel(tr(
        "This action generates and uploads an anonymized diagnostic report to help diagnose issues.\n\n"
        "The report includes:\n"
        "  • Recent application log contents (last ~2 MB)\n"
        "  • System details (OS, architecture, Qt)\n"
        "  • RimWorld version and configured directory paths\n"
        "  • Mod collection count and active mod list in load order\n\n"
        "Privacy & security:\n"
        "  • Your home directory path is replaced with '~'\n"
        "  • Your username in other user-directory paths is replaced with '<user>'\n\n"
        "Notice: The uploaded report will be publicly viewable by anyone with the link."), this);
    description->setWordWrap(true);
    layout->addWidget(description);
    layout->addStretch();
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(10);
    auto* save = new QPushButton(tr("Save to file instead…"), this);
    save->setObjectName(QStringLiteral("appBtn"));
    connect(save, &QPushButton::clicked, this, [this] { done(static_cast<int>(UploadChoice::SAVE_TO_FILE)); });
    buttons->addWidget(save);
    buttons->addStretch();
    auto* cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName(QStringLiteral("appBtn"));
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(cancel);
    auto* upload = new QPushButton(tr("Upload"), this);
    upload->setObjectName(QStringLiteral("primaryAction"));
    connect(upload, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(upload);
    layout->addLayout(buttons);
}

UploadFailedDialog::UploadFailedDialog(const QString& errorMessage, QWidget* parent) : QDialog(parent) {
    setObjectName(QStringLiteral("uploadFailedDialog"));
    setWindowTitle(tr("Upload Failed"));
    setModal(true);
    resize(460, 220);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(16);
    auto* title = new QLabel(tr("Couldn't upload the report"), this);
    title->setObjectName(QStringLiteral("dialogTitle"));
    layout->addWidget(title);
    auto* message = new QLabel(tr("An error occurred while uploading:\n\n%1\n\n"
                                  "Would you like to save the report to a file instead?").arg(errorMessage), this);
    message->setTextFormat(Qt::PlainText);
    message->setWordWrap(true);
    layout->addWidget(message);
    layout->addStretch();
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName(QStringLiteral("appBtn"));
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    buttons->addWidget(close);
    auto* save = new QPushButton(tr("Save to file…"), this);
    save->setObjectName(QStringLiteral("primaryAction"));
    connect(save, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(save);
    layout->addLayout(buttons);
}

namespace {
class SaveReportFileDialog : public QFileDialog {
public:
    explicit SaveReportFileDialog(QWidget* parent)
        : QFileDialog(parent, tr("Save Log & System Info"), QStringLiteral("pxmodrim-report.txt"),
                      tr("Text Files (*.txt);;All Files (*)")) {
        setAcceptMode(QFileDialog::AcceptSave);
    }
};

QCoro::Task<void> saveReport(QWidget* parent, QString report, Notifier& toast) {
    auto [result, dialog] = co_await awaitDialog<SaveReportFileDialog>(parent);
    const QStringList files = dialog->selectedFiles();
    if (result != QDialog::Accepted || files.isEmpty()) co_return;
    const QString path = files.first();
    const QString error = co_await providers::runOnPool<QString>(nullptr,
        [path, report = std::move(report)] {
            QSaveFile file(path);
            if (!file.open(QIODevice::WriteOnly)) return file.errorString();
            const QByteArray bytes = report.toUtf8();
            if (file.write(bytes) != bytes.size() || !file.commit()) return file.errorString();
            return QString{};
        });
    if (!error.isEmpty()) toast.error(QObject::tr("Failed to save file: %1").arg(error), 5000);
    else toast.success(QObject::tr("Report saved to %1").arg(QFileInfo(path).fileName()), 3000);
}
}  // namespace

QCoro::Task<std::optional<QString>> handle_upload_report(QWidget* parent, CoreContext& ctx,
                                                        Notifier& toast) {
    QPointer<QWidget> owner(parent);
    auto [choice, dialog] = co_await awaitDialog<UploadConfirmDialog>(parent);
    Q_UNUSED(dialog);
    if (choice == static_cast<int>(UploadChoice::CANCEL)) co_return std::nullopt;
    support::ReportSource source{ctx.gameVersion(), ctx.config().paths,
                                       ctx.allMods(), ctx.activeUuids()};
    const QString report = co_await providers::runOnPool<QString>(nullptr,
        [source = std::move(source)] { return support::build_report(source); });
    if (parent && !owner) co_return std::nullopt;
    if (choice == static_cast<int>(UploadChoice::SAVE_TO_FILE)) {
        co_await saveReport(owner, report, toast);
        co_return std::nullopt;
    }
    toast.info(QObject::tr("Uploading log & system info…"), 2000);
    QString endpoint = QString::fromStdString(ctx.config().log_upload_endpoint);
    if (endpoint.isEmpty()) endpoint = QString(support::DEFAULT_UPLOAD_ENDPOINT);
    support::PasteUploader uploader(ctx.network(), std::move(endpoint));
    auto uploaded = co_await uploader.upload(report);
    if (parent && !owner) co_return std::nullopt;
    if (!uploaded) {
        auto [result, failed] = co_await awaitDialog<UploadFailedDialog>(uploaded.error().message, owner.data());
        Q_UNUSED(failed);
        if (result == QDialog::Accepted) co_await saveReport(owner, report, toast);
        co_return std::nullopt;
    }
    QGuiApplication::clipboard()->setText(*uploaded);
    toast.success(QObject::tr("Link copied: %1").arg(*uploaded), 5000);
    co_return *uploaded;
}
}  // namespace px::modrim
