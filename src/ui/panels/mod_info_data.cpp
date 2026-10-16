#include "ui/panels/mod_info_data.hpp"
#include "ui/components/unity_rich_text.hpp"
#include "ui/models/mod_list_model.hpp"
#include "core/downloads/manager.hpp"
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <array>

namespace px::modrim {
namespace {
template<class F> QString substitute(const QString& text, const QRegularExpression& re, F replacement) {
    QString out;
    out.reserve(text.size());
    qsizetype end = 0;
    auto matches = re.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        out += text.mid(end, match.capturedStart() - end);
        out += replacement(match);
        end = match.capturedEnd();
    }
    out += text.mid(end);
    return out;
}
QRegularExpression insensitive(const QString& pattern) {
    return QRegularExpression(pattern, QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
}
}
QString bbcode_images(QString text) {
    static const auto re = insensitive(R"(\[img\]((https?)://[^\s\[<"]+)\[/img\])");
    return substitute(text, re, [](const auto& match) {
        const QString url = match.captured(1);
        return match.captured(2).compare("https", Qt::CaseInsensitive) == 0
            ? "<img src=\"" + url + "\" width=\"" + IMG_WIDTH_TOKEN + "\">"
            : "<a href=\"" + url + "\">" + url + "</a>";
    });
}
QString restore_entities(QString text) {
    static const QRegularExpression re(R"(&amp;(#\d{1,7}|#x[0-9a-fA-F]{1,6}|[a-zA-Z]{2,8});)");
    return text.replace(re, "&\\1;");
}
QString bbcode_markup(QString text) {
    static const auto named_url = insensitive(R"(\[url=(https?://[^\]\s"<]+)\](.*?)\[/url\])");
    static const auto bare_url = insensitive(R"(\[url\](https?://[^\[\s"<]+)\[/url\])");
    static const auto quote = insensitive(R"(\[quote=([^\]]+)\])");
    static const auto table = insensitive(R"(\[table[^\]]*\])");
    static const auto strip = insensitive(R"(\[/?(?:spoiler|noparse)\])");
    static const auto hr = insensitive(R"(\[hr\])");
    static const auto paired = insensitive(R"(\[(/?)(b|i|u|s|strike|h1|h2|h3|code|quote|list|olist|table|tr|td|th)\])");
    static const QHash<QString, QString> names{{"strike", "s"}, {"code", "pre"}, {"quote", "blockquote"}, {"list", "ul"}, {"olist", "ol"}};
    text.replace(named_url, "<a href=\"\\1\">\\2</a>");
    text.replace(bare_url, "<a href=\"\\1\">\\1</a>");
    text.replace(quote, "<blockquote><i>\\1 wrote:</i><br>");
    text.replace(table, "<table>"); text.replace(strip, ""); text.replace(hr, "<hr>"); text.replace("[*]", "<li>");
    text = substitute(text, paired, [](const auto& match) {
        const QString tag = match.captured(2).toLower();
        return "<" + match.captured(1) + names.value(tag, tag) + ">";
    });
    static const QRegularExpression before(R"((?:<br>\s*)+(?=</?(?:ul|ol|li|table|tr|td|th|blockquote|pre|h[1-3])\b))");
    static const QRegularExpression after(R"((<(?:/h[1-3]|/?ul|/?ol|/?table|/?tr|/?blockquote|/?pre)>)(?:<br>\s*)+)");
    text.replace(before, "");
    return text.replace(after, "\\1");
}
QString autolink(const QString& text) {
    static const QRegularExpression tags("<[^>]+>");
    static const QRegularExpression urls(R"(https?://(?:(?!&(?:quot|lt|gt|#x27|#39);)[^\s<>"])+)");
    const auto link_text = [](const QString& part) {
        return substitute(part, urls, [](const auto& match) {
            const QString url = match.captured();
            QString stripped = url;
            while (!stripped.isEmpty() && QStringLiteral(".,;:!?)").contains(stripped.back())) stripped.chop(1);
            return "<a href=\"" + stripped + "\">" + stripped + "</a>" + url.mid(stripped.size());
        });
    };
    QString out;
    qsizetype end = 0;
    bool anchor = false;
    auto matches = tags.globalMatch(text);
    while (matches.hasNext()) {
        const auto match = matches.next();
        const QString plain = text.mid(end, match.capturedStart() - end);
        out += anchor ? plain : link_text(plain);
        const QString tag = match.captured(), lowered = tag.toLower();
        if (lowered.startsWith("<a ") || lowered == "<a>") anchor = true;
        else if (lowered == "</a>") anchor = false;
        out += tag;
        end = match.capturedEnd();
    }
    const QString tail = text.mid(end);
    return out + (anchor ? tail : link_text(tail));
}
QString description_to_html(const QString& text, const QString& description_format) {
    if (description_format != "bbcode") return escape_html(text).replace('\n', "<br>");
    return autolink(bbcode_markup(bbcode_images(restore_entities(unity_rich_text_to_html(text)))));
}
QString format_age(double mtime, std::optional<double> now) {
    if (mtime <= 0) return {};
    const double delta = now.value_or(QDateTime::currentMSecsSinceEpoch() / 1000.0) - mtime;
    static constexpr std::array<std::pair<const char*, int>, 5> units{{{"year", 365 * 86400}, {"month", 30 * 86400}, {"week", 7 * 86400}, {"day", 86400}, {"hour", 3600}}};
    for (const auto& [name, seconds] : units) if (delta >= seconds) {
        const auto count = static_cast<qint64>(delta / seconds);
        return QString::number(count) + " " + name + (count == 1 ? "" : "s") + " ago";
    }
    return "just now";
}
QVariantMap build_mod_info(CoreContext& ctx, const ListedMod& mod, const QList<view::ModIssueView>& issues) {
    const auto* about = dynamic_cast<const AboutXmlMod*>(&mod);
    const auto& active = ctx.activeUuids();
    const auto position = active.indexOf(mod.uuid());
    QVariantList issue_rows, needs, needed_by, details;
    QStringList conflicts;
    int errors = 0;
    for (const auto& issue : issues) {
        errors += issue.is_error;
        issue_rows.append(QVariantMap{{"title", issue.category_display_name}, {"detail", issue.detail.value_or(QString{})}, {"isError", issue.is_error}});
    }
    const auto warnings = issues.size() - errors;
    const QString level = errors ? "error" : warnings ? "warning" : "ok";
    const auto count = errors ? errors : warnings;
    const QString title = count ? QString::number(count) + (errors ? " error" : " warning") + (count == 1 ? "" : "s") : QStringLiteral("No issues");
    if (about) {
        QHash<QString, const AboutXmlMod*> index;
        for (const auto& entry : ctx.allMods()) if (const auto* other = dynamic_cast<const AboutXmlMod*>(entry.get()))
            if (!index.contains(other->package_id.str())) index.insert(other->package_id.str(), other);
        for (const auto& dep : about->about_rules.dependencies) {
            auto* found = index.value(dep.package_id.str(), nullptr);
            if (!found) for (const auto& alternative : dep.alternative_package_ids) {
                found = index.value(alternative.str(), nullptr);
                if (found) break;
            }
            needs.append(QVariantMap{{"name", dep.name.isEmpty() ? dep.package_id.str() : dep.name},
                {"state", !found ? "missing" : active.contains(found->uuid()) ? "active" : "inactive"}, {"uuid", found ? found->uuid() : QString{}}});
        }
        for (const auto& pid : about->about_rules.incompatible_with) {
            const auto* other = index.value(pid.str(), nullptr);
            if (other && active.contains(other->uuid())) conflicts.append(other->name);
        }
        std::sort(conflicts.begin(), conflicts.end(), [](const auto& a, const auto& b) { return a.toLower() < b.toLower(); });
        QList<const AboutXmlMod*> dependents;
        for (const auto& entry : ctx.allMods()) if (const auto* other = dynamic_cast<const AboutXmlMod*>(entry.get()); other && other->uuid() != mod.uuid()) {
            for (const auto& dep : other->about_rules.dependencies)
                if (dep.package_id == about->package_id || dep.alternative_package_ids.contains(about->package_id)) { dependents.append(other); break; }
        }
        std::sort(dependents.begin(), dependents.end(), [](auto* a, auto* b) { return a->name.toLower() < b->name.toLower(); });
        for (const auto* other : dependents) needed_by.append(QVariantMap{{"name", other->name}, {"uuid", other->uuid()}});
    }
    auto& downloads = ctx.downloads();
    auto* source = downloads.source_of(mod);
    const auto row = [&details](const QString& label, const QString& value, bool copy = false) {
        QVariantMap item{{"label", label}, {"value", value}};
        if (copy) item.insert("copy", true);
        details.append(item);
    };
    QString provider;
    if (about) {
        if (!about->authors.isEmpty()) row("Author", about->authors.join(", "));
        if (!about->mod_version.isEmpty()) row("Version", about->mod_version);
        provider = provider_label(mod.provider_id, about->package_id.str());
        if (!provider.isEmpty()) row("Source", provider);
        if (about->mtime > 0) row("Updated", format_age(about->mtime));
        if (const auto synced = downloads.last_synced(mod); synced && *synced != 0) row("Last synced", format_age(*synced));
        row("Package ID", about->package_id.str(), true);
    }
    if (mod.mod_path()) row("Path", *mod.mod_path(), true);
    const QString target = ctx.targetVersion();
    const bool known = !mod.supported_versions.isEmpty() && target != "Unknown";
    return {{"name", mod.name}, {"author", about ? about->authors.join(", ") : QString{}}, {"provider", provider},
        {"status", QVariantMap{{"level", level}, {"title", title}, {"active", position >= 0}, {"position", position >= 0 ? position + 1 : 0}, {"total", active.size()}, {"issues", issue_rows}, {"obsolete", mod.obsolete}, {"valid", mod.valid}}},
        {"version", QVariantMap{{"known", known}, {"ok", known && mod.supported_versions.contains(target)}, {"target", target}}},
        {"needs", needs}, {"conflicts", conflicts}, {"neededBy", needed_by}, {"description", description_to_html(mod.description)}, {"details", details},
        {"canOpenFolder", mod.mod_path() && QFileInfo::exists(*mod.mod_path())}, {"canUpdate", source != nullptr}, {"updateSource", source ? source->label() : QString{}}, {"url", about ? about->url : QString{}}};
}
}
