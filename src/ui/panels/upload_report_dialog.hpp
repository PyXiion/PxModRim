#pragma once

#include <QString>
#include <QWidget>
#include <optional>

#include <QCoro/QCoroTask>

namespace px::modrim {
class CoreContext;
class Notifier;
QCoro::Task<std::optional<QString>> handleUploadReport(QWidget* parent,
                                                       CoreContext& ctx,
                                                       Notifier& toast);
}  // namespace px::modrim
