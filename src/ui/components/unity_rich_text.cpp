#include "ui/components/unity_rich_text.h"
#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <cmath>

namespace px::ui {
QString escape_html(const QString& text) {
    QString out = text.toHtmlEscaped();
    return out.replace('\'', QStringLiteral("&#x27;"));
}
namespace {
QString unquote(QString value) {
    while (!value.isEmpty() && (value.front() == '\'' || value.front() == '"')) value.remove(0, 1);
    while (!value.isEmpty() && (value.back() == '\'' || value.back() == '"')) value.chop(1);
    return value;
}
QString color(QString value) {
    if (value.startsWith('#')) {
        const QString hex = value.mid(1);
        if (hex.size() == 3 || hex.size() == 4) {
            QString expanded = "#";
            for (QChar c : hex) { expanded += c; expanded += c; }
            return expanded;
        }
        if (hex.size() == 6) return value;
        if (hex.size() == 8) return value.left(7);
    }
    static const QHash<QString, QString> names{{"red", "#ff0000"}, {"green", "#00ff00"},
        {"blue", "#0000ff"}, {"white", "#ffffff"}, {"black", "#000000"},
        {"yellow", "#ffff00"}, {"cyan", "#00ffff"}, {"magenta", "#ff00ff"},
        {"gray", "#808080"}, {"grey", "#808080"}, {"orange", "#ffa500"}, {"purple", "#800080"}};
    return names.value(value.toLower(), "#ffffff");
}
QString close_tag(const QString& tag) {
    if (tag == "b" || tag == "bold") return "</b>";
    if (tag == "i" || tag == "italic") return "</i>";
    if (tag == "u" || tag == "underline" || tag == "size") return "</span>";
    if (tag == "s" || tag == "strike" || tag == "strikethrough") return "</s>";
    if (tag == "color") return "</font>";
    if (tag == "align" || tag == "indent") return "</div>";
    if (tag == "a" || tag == "link") return "</a>";
    if (tag == "mark" || tag == "sub" || tag == "sup") return "</" + tag + ">";
    return {};
}
QString open_tag(const QString& tag, QString attr) {
    attr = unquote(attr);
    if (tag == "b" || tag == "bold") return "<b>";
    if (tag == "i" || tag == "italic") return "<i>";
    if (tag == "u" || tag == "underline") return "<span style=\"text-decoration: underline;\">";
    if (tag == "s" || tag == "strike" || tag == "strikethrough") return "<s>";
    if (tag == "br" || tag == "sub" || tag == "sup") return "<" + tag + ">";
    if (tag == "color") {
        static const QRegularExpression valid("^(#?[0-9a-fA-F]{3,8}|[a-zA-Z]+)$");
        return "<font color=\"" + (valid.match(attr).hasMatch() ? color(attr) : "#ffffff") + "\">";
    }
    if (tag == "size" || tag == "indent") {
        static const QRegularExpression number("^([+-]?\\d+(?:\\.\\d+)?)(px|em|%)?$");
        auto match = number.match(attr);
        double value = tag == "size" ? 13.0 : 20.0;
        if (match.hasMatch()) {
            bool ok = false;
            const double raw = match.captured(1).toDouble(&ok);
            if (ok && std::isfinite(raw)) {
                const QString unit = match.captured(2);
                if (tag == "size") {
                    if (unit == "%") value = 12 * raw / 100;
                    else if (unit == "em") value = 12 * raw;
                    else if (attr.startsWith('+') || attr.startsWith('-')) value = 12 + raw;
                    else value = raw;
                    if (!std::isfinite(value)) value = 13;
                } else if (unit == "%") value = 4 * raw;
                else if (unit == "em") value = 16 * raw;
                else value = raw;
            }
        }
        if (tag == "size") return "<span style=\"font-size: " + QString::number(std::clamp(value, 1.0, 200.0), 'g', 15) + "pt;\">";
        return "<div style=\"margin-left: " + QString::number(static_cast<int>(std::clamp(value, -10000.0, 10000.0))) + "px;\">";
    }
    if (tag == "align") {
        attr = attr.toLower();
        if (attr == "justified" || attr == "flush") attr = "justify";
        if (attr != "left" && attr != "right" && attr != "center" && attr != "justify") attr = "left";
        return "<div style=\"text-align: " + attr + ";\">";
    }
    if (tag == "mark") {
        QString background = attr.isEmpty() ? QStringLiteral("#ffff00") : color(attr);
        if (attr.size() == 9 && attr.startsWith('#')) {
            bool ok;
            int alpha = attr.right(2).toInt(&ok, 16);
            if (ok) background += QString::number(alpha / 255.0, 'f', 2);
        }
        return "<mark style=\"background-color: " + background + ";\">";
    }
    if (tag == "a" || tag == "link") {
        const QUrl url(attr);
        const QString scheme = url.scheme().toLower();
        if ((scheme == "http" || scheme == "https") && !url.authority().isEmpty())
            return "<a href=\"" + escape_html(attr) + "\">";
    }
    return {};
}
}
QString unity_rich_text_to_html(const QString& text) {
    static const QRegularExpression tags("</?([a-zA-Z]+)(?:=([^>\\s]+))?\\s*>");
    QString result;
    result.reserve(text.size());
    QStringList stack;
    qsizetype end = 0;
    auto matches = tags.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        result += escape_html(text.mid(end, match.capturedStart() - end));
        const QString tag = match.captured(1).toLower();
        if (match.captured().startsWith("</")) {
            const auto index = stack.lastIndexOf(tag);
            if (index >= 0) {
                while (stack.size() > index) stack.removeLast();
                result += close_tag(tag);
            }
        } else {
            const QString html = open_tag(tag, match.captured(2));
            if (!html.isEmpty()) { stack.append(tag); result += html; }
        }
        end = match.capturedEnd();
    }
    result += escape_html(text.mid(end));
    result.replace('\n', "<br>");
    result.replace("\\n", "<br>");
    for (auto it = stack.crbegin(); it != stack.crend(); ++it) result += close_tag(*it);
    return result;
}
}
