#pragma once
#include <QVariantMap>
#include <optional>
#include "core/context.h"
#include "core/models/view/diagnostics.h"
namespace px::ui {
inline const QString IMG_WIDTH_TOKEN = QStringLiteral("__IMG_WIDTH__");
QString bbcode_images(QString text);
QString restore_entities(QString text);
QString bbcode_markup(QString text);
QString autolink(const QString& text);
QString description_to_html(const QString& text, const QString& description_format = QStringLiteral("bbcode"));
QString format_age(double mtime, std::optional<double> now = std::nullopt);
QVariantMap build_mod_info(core::CoreContext& ctx, const core::ListedMod& mod, const QList<core::view::ModIssueView>& issues);
}
