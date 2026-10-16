#pragma once
#include <QVariantMap>
#include <optional>
#include "core/context.h"
#include "core/models/view/diagnostics.h"
namespace px::modrim {
inline const QString IMG_WIDTH_TOKEN = QStringLiteral("__IMG_WIDTH__");
QString bbcode_images(QString text);
QString restore_entities(QString text);
QString bbcode_markup(QString text);
QString autolink(const QString& text);
QString description_to_html(const QString& text, const QString& description_format = QStringLiteral("bbcode"));
QString format_age(double mtime, std::optional<double> now = std::nullopt);
QVariantMap build_mod_info(CoreContext& ctx, const ListedMod& mod, const QList<view::ModIssueView>& issues);
}
