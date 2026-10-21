#include "ui/plugins/workshop/models.hpp"

#include <QLocale>
#include <QRegularExpression>
#include <QUrl>
#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>
#include <utility>

#include "ui/panels/mod_info_data.hpp"

namespace px::modrim::workshop {
namespace {
using namespace workshop;

enum Role {
  kItemIdRole = Qt::UserRole + 1,
  kKindRole,
  kTitleRole,
  kAuthorRole,
  kPreviewUrlRole,
  kCollageRole,
  kSourceLabelRole,
  kVersionsRole,
  kTagsRole,
  kStateRole,
  kActiveRole,
  kStateLabelRole,
  kActionLabelRole,
  kIncompatibleRole,
  kMemberCountRole,
  kDescriptionRole,
  kWorkshopUrlRole,
  kFileSizeRole,
  kVotesRole,
  kVotesUpRole,
  kVotesDownRole,
  kQueuedRole,
  kFavouriteRole,
  kSummaryRole,
  kCompatLabelRole,
};

constexpr std::array<std::pair<int, const char*>, 25> kRoles{{
    {kItemIdRole, "itemId"},
    {kKindRole, "kind"},
    {kTitleRole, "title"},
    {kAuthorRole, "author"},
    {kPreviewUrlRole, "previewUrl"},
    {kCollageRole, "collage"},
    {kSourceLabelRole, "sourceLabel"},
    {kVersionsRole, "versions"},
    {kTagsRole, "tags"},
    {kStateRole, "state"},
    {kActiveRole, "active"},
    {kStateLabelRole, "stateLabel"},
    {kActionLabelRole, "actionLabel"},
    {kIncompatibleRole, "incompatible"},
    {kMemberCountRole, "memberCount"},
    {kDescriptionRole, "description"},
    {kWorkshopUrlRole, "workshopUrl"},
    {kFileSizeRole, "fileSize"},
    {kVotesRole, "votes"},
    {kVotesUpRole, "votesUp"},
    {kVotesDownRole, "votesDown"},
    {kQueuedRole, "queued"},
    {kFavouriteRole, "favourite"},
    {kSummaryRole, "summary"},
    {kCompatLabelRole, "compatLabel"},
}};

QString kindName(CardKind kind) {
  return kind == CardKind::kCollection ? QStringLiteral("collection")
                                       : QStringLiteral("mod");
}

QString stateName(CardState state) {
  switch (state) {
    case CardState::kMissing:
      return QStringLiteral("missing");
    case CardState::kInstalled:
      return QStringLiteral("installed");
    case CardState::kOutdated:
      return QStringLiteral("outdated");
  }
  return {};
}

QString stateLabel(CardState state) {
  switch (state) {
    case CardState::kMissing:
      return QStringLiteral("Not installed");
    case CardState::kInstalled:
      return QStringLiteral("Installed");
    case CardState::kOutdated:
      return QStringLiteral("Update available");
  }
  return {};
}

QString actionLabel(CardAction action) {
  switch (action) {
    case CardAction::kDownload:
      return QStringLiteral("Download");
    case CardAction::kInstalled:
      return QStringLiteral("Installed");
    case CardAction::kUpdate:
      return QStringLiteral("Update");
    case CardAction::kDownloadAll:
      return QStringLiteral("Download all");
    case CardAction::kCompleteDownload:
      return QStringLiteral("Complete download");
    case CardAction::kPreparing:
      return QStringLiteral("Preparing…");
    case CardAction::kDownloading:
      return QStringLiteral("Downloading");
    case CardAction::kQueued:
      return QStringLiteral("Queued");
  }
  return {};
}

QVariant roleValue(const CatalogCard& card, int role) {
  switch (role) {
    case kItemIdRole:
      return card.itemId;
    case kKindRole:
      return kindName(card.kind);
    case kTitleRole:
      return card.title;
    case kAuthorRole:
      return card.author;
    case kPreviewUrlRole:
      return card.previewUrl;
    case kCollageRole:
      return card.collage;
    case kSourceLabelRole:
      return card.sourceLabel;
    case kVersionsRole:
      return card.versions;
    case kTagsRole:
      return card.tags;
    case kStateRole:
      return stateName(card.state);
    case kActiveRole:
      return card.active;
    case kStateLabelRole:
      return stateLabel(card.state);
    case kActionLabelRole:
      return actionLabel(card.action);
    case kIncompatibleRole:
      return card.incompatible;
    case kMemberCountRole:
      return card.memberCount;
    case kDescriptionRole:
      return QString{};
    case kWorkshopUrlRole:
      return card.workshopUrl;
    case kFileSizeRole:
      return card.fileSize;
    case kVotesRole:
      return card.votes;
    case kVotesUpRole:
      return card.votesUp;
    case kVotesDownRole:
      return card.votesDown;
    case kQueuedRole:
      return card.queued;
    case kFavouriteRole:
      return card.favourite;
    case kSummaryRole:
      return card.summary;
    case kCompatLabelRole:
      return card.compatLabel;
  }
  return {};
}

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

QSet<QString> idsOf(const std::vector<std::string>& ids) {
  QSet<QString> result;
  for (const auto& id : ids) result.insert(QString::fromStdString(id));
  return result;
}

CardState cardState(InstallState state) {
  switch (state) {
    case InstallState::kMissing:
      return CardState::kMissing;
    case InstallState::kInstalled:
      return CardState::kInstalled;
    case InstallState::kOutdated:
      return CardState::kOutdated;
  }
  return CardState::kMissing;
}

QString descriptionHtml(const CatalogEntry& entry) {
  return std::visit(
      [](const auto& item) {
        using Item = std::decay_t<decltype(item)>;
        QString format = QStringLiteral("bbcode");
        if constexpr (std::is_same_v<Item, CatalogCollection>)
          if (item.description_format == CollectionDescriptionFormat::kText)
            format = QStringLiteral("text");
        return px::modrim::descriptionToHtml(
            QString::fromStdString(item.description), format);
      },
      entry);
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

CatalogCard makeCard(const CatalogEntry& entry, const CardContext& context) {
  return std::visit(
      [&](const auto& item) {
        using Item = std::decay_t<decltype(item)>;
        constexpr bool kCollection = std::is_same_v<Item, CatalogCollection>;
        CatalogCard card;
        card.itemId = QString::fromStdString(item.id);
        card.kind = kCollection ? CardKind::kCollection : CardKind::kMod;
        card.title = QString::fromStdString(item.title);
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
        bool incompatible = !context.gameVersion.isEmpty() &&
                            !versions.isEmpty() &&
                            !versions.contains(context.gameVersion);
        bool noCommon = false;
        QString votes = QStringLiteral("Unrated"), up = "0", down = "0";
        QString workshopUrl, format = QStringLiteral("bbcode");
        if constexpr (kCollection) {
          card.sourceLabel = item.source == CollectionSource::kPicked
                                 ? "PxModRim pick"
                                 : "Steam collection";
          card.action = CardAction::kDownloadAll;
          card.memberCount = static_cast<int>(item.member_count);
          card.fileSize = sizeLabel(item.total_size);
          noCommon = item.no_common_version && versions.isEmpty();
          incompatible = incompatible || noCommon;
          if (item.workshop_url)
            workshopUrl = QString::fromStdString(*item.workshop_url);
          if (item.description_format == CollectionDescriptionFormat::kText)
            format = QStringLiteral("text");
          for (const auto& preview : item.member_previews) {
            const QString url = safeUrl(QString::fromStdString(preview));
            if (!url.isEmpty()) card.collage.append(url);
          }
        } else {
          card.state = cardState(context.installState(item));
          incompatible = incompatible || item.incompatible;
          card.sourceLabel = QStringLiteral("Steam Workshop");
          switch (card.state) {
            case CardState::kMissing:
              card.action = CardAction::kDownload;
              break;
            case CardState::kInstalled:
              card.action = CardAction::kInstalled;
              break;
            case CardState::kOutdated:
              card.action = CardAction::kUpdate;
              break;
          }
          card.fileSize = sizeLabel(item.file_size);
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
        card.author = author;
        card.previewUrl = safeUrl(preview);
        card.versions = versionLabels.isEmpty()
                            ? QStringLiteral("Not specified")
                            : versionLabels.join(", ");
        card.tags = tagLabels.join(QStringLiteral(" · "));
        card.workshopUrl = safeUrl(workshopUrl);
        card.votes = votes;
        card.votesUp = up;
        card.votesDown = down;
        card.summary = summarize(description, format);
        card.compatLabel =
            compatLabel(versions, context.gameVersion, incompatible, noCommon);
        card.incompatible = incompatible;

        // Installation, activation and queue state, resolved exactly once from
        // the refresh-wide snapshot.
        if constexpr (kCollection) {
          if (!context.detail) {
            qsizetype installed = 0;
            for (const auto& member : item.member_ids)
              installed +=
                  context.installedIds.contains(QString::fromStdString(member));
            if (!item.member_ids.empty() &&
                installed == static_cast<qsizetype>(item.member_ids.size())) {
              card.state = CardState::kInstalled;
              card.action = CardAction::kInstalled;
            } else if (installed) {
              card.action = CardAction::kCompleteDownload;
            }
          }
          card.favourite =
              context.favouriteIds.contains(QString::fromStdString(item.id));
        } else {
          card.active = context.activeIds.contains(card.itemId);
        }
        if constexpr (kCollection) {
          if (context.preparingIds.contains(card.itemId)) {
            card.queued = true;
            card.action = CardAction::kPreparing;
          } else {
            QSet<QString> candidates = idsOf(item.member_ids);
            candidates.unite(context.plannedMembers.value(card.itemId));
            if (context.packId == card.itemId)
              candidates.unite(context.packIds);
            if (candidates.intersects(context.downloadingIds)) {
              card.queued = true;
              card.action = CardAction::kDownloading;
            } else if (candidates.intersects(context.queuedIds)) {
              card.queued = true;
              card.action = CardAction::kQueued;
            }
          }
        } else if (card.state != CardState::kInstalled) {
          if (context.preparingIds.contains(card.itemId)) {
            card.queued = true;
            card.action = CardAction::kPreparing;
          } else if (context.downloadingIds.contains(card.itemId)) {
            card.queued = true;
            card.action = CardAction::kDownloading;
          } else if (context.queuedIds.contains(card.itemId)) {
            card.queued = true;
            card.action = CardAction::kQueued;
          }
        }
        return card;
      },
      entry);
}

QVariantMap cardDetail(const CatalogCard& card, const CatalogEntry& item) {
  QVariantMap result;
  for (const auto& [role, name] : kRoles)
    result.insert(QString::fromLatin1(name), roleValue(card, role));
  result.insert(QStringLiteral("description"), descriptionHtml(item));
  return result;
}

CatalogListModel::CatalogListModel(QObject* parent)
    : QAbstractListModel(parent) {}

void CatalogListModel::setPage(QList<CatalogCard> cards, int total,
                               std::optional<QString> cursor, bool append) {
  if (append) {
    QSet<QString> known;
    for (const auto& card : m_rows) known.insert(card.itemId);
    QList<CatalogCard> additions;
    for (auto& card : cards) {
      if (!known.contains(card.itemId)) {
        known.insert(card.itemId);
        additions.append(std::move(card));
      }
    }
    if (!additions.isEmpty()) {
      beginInsertRows({}, count(),
                      count() + static_cast<int>(additions.size()) - 1);
      m_rows.append(std::move(additions));
      endInsertRows();
    }
  } else if (cards.size() == m_rows.size() &&
             std::equal(cards.cbegin(), cards.cend(), m_rows.cbegin(),
                        [](const auto& a, const auto& b) {
                          return a.itemId == b.itemId;
                        })) {
    for (qsizetype i = 0; i < cards.size(); ++i) {
      if (cards.at(i) != m_rows.at(i)) {
        m_rows[i] = std::move(cards[i]);
        emit dataChanged(index(static_cast<int>(i)),
                         index(static_cast<int>(i)));
      }
    }
  } else {
    beginResetModel();
    m_rows = std::move(cards);
    endResetModel();
  }
  m_total = total;
  m_cursor = std::move(cursor);
  emit changed();
}

void CatalogListModel::refresh(
    const std::function<CatalogCard(const CatalogCard&)>& convert) {
  for (auto& card : m_rows) card = convert(card);
  if (!m_rows.isEmpty()) emit dataChanged(index(0), index(count() - 1));
}

int CatalogListModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : count();
}

QVariant CatalogListModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= count()) return {};
  return roleValue(m_rows.at(index.row()), role);
}

QHash<int, QByteArray> CatalogListModel::roleNames() const {
  QHash<int, QByteArray> names;
  for (const auto& [role, name] : kRoles) names.insert(role, name);
  return names;
}

}  // namespace px::modrim::workshop
