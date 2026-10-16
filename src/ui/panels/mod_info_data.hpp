#pragma once
#include <QVariantMap>
#include <optional>
#include "core/context.hpp"
#include "core/models/view/diagnostics.hpp"
namespace px::modrim {
inline const QString kImgWidthToken = QStringLiteral("__IMG_WIDTH__");
QString bbcodeImages(QString text);
QString restoreEntities(QString text);
QString bbcodeMarkup(QString text);
QString autolink(const QString& text);
QString descriptionToHtml(const QString& text, const QString& descriptionFormat = QStringLiteral("bbcode"));
QString formatAge(double mtime, std::optional<double> now = std::nullopt);
QVariantMap buildModInfo(CoreContext& ctx, const ListedMod& mod, const QList<view::ModIssueView>& issues);
}
