#include "ui/panels/mod_info_data.hpp"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringView>
#include <algorithm>
#include <array>

#include "core/downloads/manager.hpp"
#include "ui/components/unity_rich_text.hpp"
#include "ui/models/mod_list_model.hpp"

namespace px::modrim {
namespace {
template <class F>
QString substitute(const QString& text, const QRegularExpression& re,
                   F replacement) {
  QString out;
  out.reserve(text.size());
  qsizetype end = 0;
  auto matches = re.globalMatch(text);
  while (matches.hasNext()) {
    const auto match = matches.next();
    out += QStringView(text).sliced(end, match.capturedStart() - end);
    out += replacement(match);
    end = match.capturedEnd();
  }
  out += QStringView(text).sliced(end);
  return out;
}
QRegularExpression insensitive(const QString& pattern) {
  return QRegularExpression(pattern,
                            QRegularExpression::CaseInsensitiveOption |
                                QRegularExpression::DotMatchesEverythingOption);
}
}  // namespace
QString bbcodeImages(const QString& text) {
  static const auto re =
      insensitive(R"(\[img\]((https?)://[^\s\[<"]+)\[/img\])");
  return substitute(text, re, [](const auto& match) {
    const QString url = match.captured(1);
    return match.captured(2).compare("https", Qt::CaseInsensitive) == 0
               ? "<img src=\"" + url + "\" width=\"" + kImgWidthToken + "\">"
               : "<a href=\"" + url + "\">" + url + "</a>";
  });
}
QString restoreEntities(QString text) {
  static const QRegularExpression re(
      R"(&amp;(#\d{1,7}|#x[0-9a-fA-F]{1,6}|[a-zA-Z]{2,8});)");
  return text.replace(re, "&\\1;");
}
QString bbcodeMarkup(QString text) {
  static const auto namedUrl =
      insensitive(R"(\[url=(https?://[^\]\s"<]+)\](.*?)\[/url\])");
  static const auto bareUrl =
      insensitive(R"(\[url\](https?://[^\[\s"<]+)\[/url\])");
  static const auto quote = insensitive(R"(\[quote=([^\]]+)\])");
  static const auto table = insensitive(R"(\[table[^\]]*\])");
  static const auto strip = insensitive(R"(\[/?(?:spoiler|noparse)\])");
  static const auto hr = insensitive(R"(\[hr\])");
  static const auto paired = insensitive(
      R"(\[(/?)(b|i|u|s|strike|h1|h2|h3|code|quote|list|olist|table|tr|td|th)\])");
  static const QHash<QString, QString> names{{"strike", "s"},
                                             {"code", "pre"},
                                             {"quote", "blockquote"},
                                             {"list", "ul"},
                                             {"olist", "ol"}};
  text.replace(namedUrl, "<a href=\"\\1\">\\2</a>");
  text.replace(bareUrl, "<a href=\"\\1\">\\1</a>");
  text.replace(quote, "<blockquote><i>\\1 wrote:</i><br>");
  text.replace(table, "<table>");
  text.replace(strip, "");
  text.replace(hr, "<hr>");
  text.replace("[*]", "<li>");
  text = substitute(text, paired, [](const auto& match) {
    const QString tag = match.captured(2).toLower();
    return "<" + match.captured(1) + names.value(tag, tag) + ">";
  });
  static const QRegularExpression before(
      R"((?:<br>\s*)+(?=</?(?:ul|ol|li|table|tr|td|th|blockquote|pre|h[1-3])\b))");
  static const QRegularExpression after(
      R"((<(?:/h[1-3]|/?ul|/?ol|/?table|/?tr|/?blockquote|/?pre)>)(?:<br>\s*)+)");
  text.replace(before, "");
  return text.replace(after, "\\1");
}
QString autolink(const QString& text) {
  static const QRegularExpression tags("<[^>]+>");
  static const QRegularExpression urls(
      R"(https?://(?:(?!&(?:quot|lt|gt|#x27|#39);)[^\s<>"])+)");
  const auto linkText = [](const QString& part) {
    return substitute(part, urls, [](const auto& match) {
      const QString url = match.captured();
      QString stripped = url;
      while (!stripped.isEmpty() &&
             QStringLiteral(".,;:!?)").contains(stripped.back()))
        stripped.chop(1);
      return "<a href=\"" + stripped + "\">" + stripped + "</a>" +
             url.mid(stripped.size());
    });
  };
  QString out;
  qsizetype end = 0;
  bool anchor = false;
  auto matches = tags.globalMatch(text);
  while (matches.hasNext()) {
    const auto match = matches.next();
    const QString plain = text.mid(end, match.capturedStart() - end);
    out += anchor ? plain : linkText(plain);
    const QString tag = match.captured(), lowered = tag.toLower();
    if (lowered.startsWith("<a ") || lowered == "<a>")
      anchor = true;
    else if (lowered == "</a>")
      anchor = false;
    out += tag;
    end = match.capturedEnd();
  }
  const QString tail = text.mid(end);
  return out + (anchor ? tail : linkText(tail));
}
QString descriptionToHtml(const QString& text,
                          const QString& descriptionFormat) {
  if (descriptionFormat != "bbcode")
    return escapeHtml(text).replace('\n', "<br>");
  return autolink(
      bbcodeMarkup(bbcodeImages(restoreEntities(unityRichTextToHtml(text)))));
}
QString formatAge(double mtime, std::optional<double> now) {
  if (mtime <= 0) return {};
  const double delta =
      now.value_or(QDateTime::currentMSecsSinceEpoch() / 1000.0) - mtime;
  static constexpr std::array<std::pair<const char*, int>, 5> kUnits{
      {{"year", 365 * 86400},
       {"month", 30 * 86400},
       {"week", 7 * 86400},
       {"day", 86400},
       {"hour", 3600}}};
  for (const auto& [name, seconds] : kUnits)
    if (delta >= seconds) {
      const auto count = static_cast<qint64>(delta / seconds);
      return QString::number(count) + " " + name + (count == 1 ? "" : "s") +
             " ago";
    }
  return "just now";
}
ModInfo buildModInfo(CoreContext& ctx, const ListedMod& mod) {
  const auto* about = dynamic_cast<const AboutXmlMod*>(&mod);
  ModInfo data;
  QVariantList needs, neededBy, details;
  if (about) {
    QHash<QString, const AboutXmlMod*> index;
    for (const auto& entry : ctx.allMods())
      if (const auto* other = dynamic_cast<const AboutXmlMod*>(entry.get()))
        if (!index.contains(other->packageId.str()))
          index.insert(other->packageId.str(), other);
    for (const auto& dep : about->aboutRules.dependencies) {
      auto* found = index.value(dep.packageId.str(), nullptr);
      if (!found)
        for (const auto& alternative : dep.alternativePackageIds) {
          found = index.value(alternative.str(), nullptr);
          if (found) break;
        }
      needs.append(QVariantMap{
          {"name", dep.name.isEmpty() ? dep.packageId.str() : dep.name},
          {"uuid", found ? found->uuid() : QString{}}});
    }
    QList<const AboutXmlMod*> incompatible;
    for (const auto& pid : about->aboutRules.incompatibleWith)
      if (const auto* other = index.value(pid.str(), nullptr))
        incompatible.append(other);
    std::sort(incompatible.begin(), incompatible.end(), [](auto* a, auto* b) {
      return a->name.toLower() < b->name.toLower();
    });
    for (const auto* other : incompatible)
      data.conflictingMods.append(
          QVariantMap{{"name", other->name}, {"uuid", other->uuid()}});
    QList<const AboutXmlMod*> dependents;
    for (const auto& entry : ctx.allMods()) {
      const auto* other = dynamic_cast<const AboutXmlMod*>(entry.get());
      if (!other || other->uuid() == mod.uuid()) continue;
      if (std::any_of(
              other->aboutRules.dependencies.cbegin(),
              other->aboutRules.dependencies.cend(), [about](const auto& dep) {
                return dep.packageId == about->packageId ||
                       dep.alternativePackageIds.contains(about->packageId);
              }))
        dependents.append(other);
    }
    std::sort(dependents.begin(), dependents.end(), [](auto* a, auto* b) {
      return a->name.toLower() < b->name.toLower();
    });
    for (const auto* other : dependents)
      neededBy.append(
          QVariantMap{{"name", other->name}, {"uuid", other->uuid()}});
  }
  auto& downloads = ctx.downloads();
  auto* source = downloads.sourceOf(mod);
  const auto row = [&details](const QString& label, const QString& value,
                              bool copy = false) {
    QVariantMap item{{"label", label}, {"value", value}};
    if (copy) item.insert("copy", true);
    details.append(item);
  };
  const QString authors = about ? about->authors.join(", ") : QString{};
  QString provider;
  if (about) {
    if (!about->authors.isEmpty()) row("Author", authors);
    if (!about->modVersion.isEmpty()) row("Version", about->modVersion);
    provider = providerLabel(mod.providerId, about->packageId.str());
    if (!provider.isEmpty()) row("Source", provider);
    if (about->mtime > 0) row("Updated", formatAge(about->mtime));
    if (const auto synced = downloads.lastSynced(mod); synced && *synced != 0)
      row("Last synced", formatAge(*synced));
    row("Package ID", about->packageId.str(), true);
  }
  if (mod.modPath()) row("Path", *mod.modPath(), true);
  const QString target = ctx.targetVersion();
  const bool known = !mod.supportedVersions.isEmpty() && target != "Unknown";
  data.description = descriptionToHtml(mod.description);
  data.info = {
      {"name", mod.name},
      {"author", authors},
      {"provider", provider},
      {"status", QVariantMap{{"obsolete", mod.obsolete}, {"valid", mod.valid}}},
      {"version",
       QVariantMap{{"known", known},
                   {"ok", known && mod.supportedVersions.contains(target)},
                   {"target", target}}},
      {"needs", needs},
      {"conflicts", QStringList{}},
      {"neededBy", neededBy},
      {"details", details},
      {"canOpenFolder", mod.modPath() && QFileInfo::exists(*mod.modPath())},
      {"canUpdate", source != nullptr},
      {"updateSource", source ? source->label() : QString{}},
      {"url", about ? about->url : QString{}}};
  return data;
}

void updateActiveState(const QStringList& activeUuids, const QString& uuid,
                       ModInfo& data) {
  const auto position = activeUuids.indexOf(uuid);
  auto status = data.info.value("status").toMap();
  status.insert("active", position >= 0);
  status.insert("position", position >= 0 ? position + 1 : 0);
  status.insert("total", activeUuids.size());
  data.info.insert("status", status);
  auto needs = data.info.value("needs").toList();
  for (auto& entry : needs) {
    auto need = entry.toMap();
    const auto target = need.value("uuid").toString();
    need.insert("state", target.isEmpty()               ? "missing"
                         : activeUuids.contains(target) ? "active"
                                                        : "inactive");
    entry = need;
  }
  data.info.insert("needs", needs);
  QStringList conflicts;
  for (const auto& entry : data.conflictingMods) {
    const auto candidate = entry.toMap();
    if (activeUuids.contains(candidate.value("uuid").toString()))
      conflicts.append(candidate.value("name").toString());
  }
  data.info.insert("conflicts", conflicts);
}

void updateIssues(const QList<view::ModIssueView>& issues, QVariantMap& info) {
  QVariantList issueRows;
  int errors = 0;
  for (const auto& issue : issues) {
    errors += issue.isError;
    issueRows.append(QVariantMap{{"title", issue.categoryDisplayName},
                                 {"detail", issue.detail.value_or(QString{})},
                                 {"isError", issue.isError}});
  }
  const auto warnings = issues.size() - errors;
  const auto count = errors ? errors : warnings;
  auto status = info.value("status").toMap();
  status.insert("level", errors ? "error" : warnings ? "warning" : "ok");
  status.insert("title", count ? QString::number(count) +
                                     (errors ? " error" : " warning") +
                                     (count == 1 ? "" : "s")
                               : QStringLiteral("No issues"));
  status.insert("issues", issueRows);
  info.insert("status", status);
}
}  // namespace px::modrim
