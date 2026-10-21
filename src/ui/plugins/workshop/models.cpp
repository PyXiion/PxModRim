#include "ui/plugins/workshop/models.hpp"

#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>
#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

#include "ui/panels/mod_info_data.hpp"

namespace px::modrim::workshop {
namespace {
using namespace workshop;

constexpr std::array<const char*, 25> kRoles = {
    "itemId",      "kind",        "title",       "author",       "previewUrl",
    "collage",     "sourceLabel", "versions",    "tags",         "state",
    "active",      "stateLabel",  "actionLabel", "incompatible", "memberCount",
    "description", "workshopUrl", "fileSize",    "votes",        "votesUp",
    "votesDown",   "queued",      "favourite",   "summary",      "compatLabel"};

QString sizeLabel(const std::optional<std::string>& bytes) {
  return bytes ? QString::number(
                     QString::fromStdString(*bytes).toDouble() / 1048576.0, 'f',
                     1) +
                     " MB"
               : QStringLiteral("Unknown");
}

QString compatLabel(const QSet<QString>& versions, const QString& game,
                    bool incompatible, bool noCommon) {
  if (game.isEmpty()) return {};
  if (noCommon) return QStringLiteral("No common version");
  if (incompatible) return "Not for " + game;
  if (versions.isEmpty()) return QStringLiteral("Version not stated");
  return "Works with " + game;
}
}  // namespace

QString safeUrl(const QString& value) {
  for (const QChar ch : value) {
    if (ch.isSpace() || ch.unicode() < 32) return {};
  }
  const QUrl url(value);
  const QString scheme = url.scheme().toLower();
  return url.isValid() && (scheme == "http" || scheme == "https") &&
                 !url.authority().isEmpty()
             ? value
             : QString{};
}

QString summarize(const QString& text, const QString& format) {
  static const QRegularExpression media(
      QStringLiteral(R"(\[(img|video|previewimage)[^\]]*\].*?\[/\1\])"),
      QRegularExpression::CaseInsensitiveOption |
          QRegularExpression::DotMatchesEverythingOption);
  static const QRegularExpression tags(
      QStringLiteral(R"(\[/?[a-zA-Z*][^\]]*\])"));
  QString result = text;
  if (format == "bbcode") result.replace(media, " ").replace(tags, " ");
  result = result.simplified();
  const auto codepoints = result.toUcs4();
  if (codepoints.size() <= 140) return result;
  result = QString::fromUcs4(
      reinterpret_cast<const char32_t*>(codepoints.constData()), 140);
  const auto space = result.lastIndexOf(' ');
  if (space >= 0) result.truncate(space);
  const QString punctuation = QStringLiteral(".,;:-–— ");
  while (!result.isEmpty() && punctuation.contains(result.back()))
    result.chop(1);
  return result + QChar(0x2026);
}

QVariantMap itemRow(
    const CatalogEntry& entry,
    const std::function<InstallState(const CatalogMod&)>& installState,
    const QString& gameVersion, bool detail) {
  return std::visit(
      [&](const auto& item) {
        using Item = std::decay_t<decltype(item)>;
        constexpr bool kCollection = std::is_same_v<Item, CatalogCollection>;
        QSet<QString> versions;
        for (const auto& version : item.supported_versions)
          versions.insert(QString::fromStdString(version));
        if constexpr (kCollection) {
          static const QRegularExpression versionTag(
              QStringLiteral(R"(^\d+\.\d+(?:\.\d+)?$)"));
          for (const auto& tag : item.tags) {
            const QString value = QString::fromStdString(tag);
            if (versionTag.match(value).hasMatch()) versions.insert(value);
          }
        }
        bool incompatible = !gameVersion.isEmpty() && !versions.isEmpty() &&
                            !versions.contains(gameVersion);
        bool noCommon = false;
        InstallState state = InstallState::kMissing;
        QString source, action, size, votes = QStringLiteral("Unrated"),
                                      up = "0", down = "0";
        QString workshopUrl, format = QStringLiteral("bbcode");
        QStringList collage;
        qlonglong memberCount = 0;
        if constexpr (kCollection) {
          source = item.source == CollectionSource::kPicked
                       ? "PxModRim pick"
                       : "Steam collection";
          action = QStringLiteral("Download all");
          memberCount = item.member_count;
          size = sizeLabel(item.total_size);
          noCommon = item.no_common_version && versions.isEmpty();
          incompatible = incompatible || noCommon;
          if (item.workshop_url)
            workshopUrl = QString::fromStdString(*item.workshop_url);
          if (item.description_format == CollectionDescriptionFormat::kText)
            format = QStringLiteral("text");
          for (const auto& preview : item.member_previews) {
            const QString url = safeUrl(QString::fromStdString(preview));
            if (!url.isEmpty()) collage.append(url);
          }
        } else {
          state = installState(item);
          incompatible = incompatible || item.incompatible;
          source = QStringLiteral("Steam Workshop");
          switch (state) {
            case InstallState::kMissing:
              action = QStringLiteral("Download");
              break;
            case InstallState::kInstalled:
              action = QStringLiteral("Installed");
              break;
            case InstallState::kOutdated:
              action = QStringLiteral("Update");
              break;
          }
          size = sizeLabel(item.file_size);
          workshopUrl = QString::fromStdString(item.workshop_url);
          if (item.votes) {
            const QLocale locale(QLocale::English, QLocale::UnitedStates);
            up = locale.toString(static_cast<qlonglong>(item.votes->up));
            down = locale.toString(static_cast<qlonglong>(item.votes->down));
            const double total = static_cast<double>(item.votes->up) +
                                 static_cast<double>(item.votes->down);
            if (total != 0)
              votes =
                  QString::number(
                      std::nearbyint(100.0 * item.votes->up / total), 'f', 0) +
                  '%';
          }
        }
        QString stateName, stateLabel;
        switch (state) {
          case InstallState::kMissing:
            stateName = "missing";
            stateLabel = "Not installed";
            break;
          case InstallState::kInstalled:
            stateName = "installed";
            stateLabel = "Installed";
            break;
          case InstallState::kOutdated:
            stateName = "outdated";
            stateLabel = "Update available";
            break;
        }
        QString preview = item.preview_url
                              ? QString::fromStdString(*item.preview_url)
                              : QString{};
        if (preview.isEmpty()) {
          for (const auto& image : item.previews) {
            if (image.type == PreviewType::kImage) {
              preview = QString::fromStdString(image.url);
              break;
            }
          }
        }
        QString author;
        if (item.author.name)
          author = QString::fromStdString(*item.author.name);
        if (author.isEmpty() && item.author.id)
          author = QString::fromStdString(*item.author.id);
        if (author.isEmpty()) author = QStringLiteral("Unknown author");
        std::vector<std::pair<std::vector<int>, QString>> sorted;
        sorted.reserve(versions.size());
        for (const auto& version : versions) {
          std::vector<int> parts;
          for (const auto& part : version.split('.'))
            parts.push_back(part.toInt());
          sorted.emplace_back(std::move(parts), version);
        }
        std::sort(sorted.begin(), sorted.end());
        QStringList versionLabels, tagLabels;
        for (const auto& version : sorted) versionLabels.append(version.second);
        for (const auto& tag : item.tags) {
          const QString value = QString::fromStdString(tag);
          if (!versions.contains(value)) tagLabels.append(value);
        }
        const QString description = QString::fromStdString(item.description);
        return QVariantMap{
            {"itemId", QString::fromStdString(item.id)},
            {"kind", kCollection ? "collection" : "mod"},
            {"title", QString::fromStdString(item.title)},
            {"author", author},
            {"previewUrl", safeUrl(preview)},
            {"collage", collage},
            {"sourceLabel", source},
            {"versions", versionLabels.isEmpty()
                             ? QStringLiteral("Not specified")
                             : versionLabels.join(", ")},
            {"tags", tagLabels.join(QStringLiteral(" · "))},
            {"state", stateName},
            {"active", false},
            {"queued", false},
            {"favourite", false},
            {"stateLabel", stateLabel},
            {"actionLabel", action},
            {"incompatible", incompatible},
            {"memberCount", memberCount},
            {"description",
             detail ? px::modrim::descriptionToHtml(description, format)
                    : QString{}},
            {"workshopUrl", safeUrl(workshopUrl)},
            {"fileSize", size},
            {"votes", votes},
            {"votesUp", up},
            {"votesDown", down},
            {"summary", summarize(description, format)},
            {"compatLabel",
             compatLabel(versions, gameVersion, incompatible, noCommon)}};
      },
      entry);
}

CatalogListModel::CatalogListModel(QObject* parent)
    : QAbstractListModel(parent) {}

void CatalogListModel::setPage(QList<QVariantMap> rows, int total,
                               std::optional<QString> cursor, bool append) {
  if (append) {
    QSet<QString> known;
    for (const auto& row : m_rows) known.insert(row.value("itemId").toString());
    QList<QVariantMap> additions;
    for (auto& row : rows) {
      const QString id = row.value("itemId").toString();
      if (!known.contains(id)) {
        known.insert(id);
        additions.append(std::move(row));
      }
    }
    if (!additions.isEmpty()) {
      beginInsertRows({}, count(),
                      count() + static_cast<int>(additions.size()) - 1);
      m_rows.append(std::move(additions));
      endInsertRows();
    }
  } else if (rows.size() == m_rows.size() &&
             std::equal(rows.cbegin(), rows.cend(), m_rows.cbegin(),
                        [](const auto& a, const auto& b) {
                          return a.value("itemId") == b.value("itemId");
                        })) {
    for (qsizetype i = 0; i < rows.size(); ++i) {
      if (rows.at(i) != m_rows.at(i)) {
        m_rows[i] = std::move(rows[i]);
        emit dataChanged(index(static_cast<int>(i)),
                         index(static_cast<int>(i)));
      }
    }
  } else {
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
  }
  m_total = total;
  m_cursor = std::move(cursor);
  emit changed();
}

void CatalogListModel::refreshRows(
    const std::function<QVariantMap(const QVariantMap&)>& convert) {
  for (auto& row : m_rows) row = convert(row);
  if (!m_rows.isEmpty()) emit dataChanged(index(0), index(count() - 1));
}

int CatalogListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : count();
}

QVariant CatalogListModel::data(const QModelIndex& index, int role) const {
  const int offset = role - Qt::UserRole - 1;
  if (!index.isValid() || index.row() < 0 || index.row() >= count() ||
      offset < 0 || offset >= static_cast<int>(kRoles.size()))
    return {};
  return m_rows.at(index.row()).value(QString::fromLatin1(kRoles[offset]));
}

QHash<int, QByteArray> CatalogListModel::roleNames() const {
  QHash<int, QByteArray> names;
  for (std::size_t i = 0; i < kRoles.size(); ++i)
    names.insert(Qt::UserRole + static_cast<int>(i) + 1, kRoles[i]);
  return names;
}

}  // namespace px::modrim::workshop
