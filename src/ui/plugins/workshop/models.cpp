#include "ui/plugins/workshop/models.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <type_traits>

#include <QLocale>
#include <QRegularExpression>
#include <QSet>
#include <QUrl>

#include "ui/panels/mod_info_data.hpp"

namespace px::modrim::workshop {
namespace {
using namespace workshop;

constexpr std::array<const char*, 25> roles = {
    "itemId", "kind", "title", "author", "previewUrl", "collage", "sourceLabel", "versions", "tags",
    "state", "active", "stateLabel", "actionLabel", "incompatible", "memberCount", "description",
    "workshopUrl", "fileSize", "votes", "votesUp", "votesDown", "queued", "favourite", "summary", "compatLabel"};

QString size_label(const std::optional<std::string>& bytes) {
    return bytes ? QString::number(QString::fromStdString(*bytes).toDouble() / 1048576.0, 'f', 1) + " MB"
                 : QStringLiteral("Unknown");
}

QString compat_label(const QSet<QString>& versions, const QString& game, bool incompatible, bool no_common) {
    if (game.isEmpty()) return {};
    if (no_common) return QStringLiteral("No common version");
    if (incompatible) return "Not for " + game;
    if (versions.isEmpty()) return QStringLiteral("Version not stated");
    return "Works with " + game;
}
}  // namespace

QString safe_url(const QString& value) {
    for (const QChar ch : value) {
        if (ch.isSpace() || ch.unicode() < 32) return {};
    }
    const QUrl url(value);
    const QString scheme = url.scheme().toLower();
    return url.isValid() && (scheme == "http" || scheme == "https") && !url.authority().isEmpty() ? value : QString{};
}

QString summarize(const QString& text, const QString& format) {
    static const QRegularExpression media(QStringLiteral(R"(\[(img|video|previewimage)[^\]]*\].*?\[/\1\])"),
                                           QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    static const QRegularExpression tags(QStringLiteral(R"(\[/?[a-zA-Z*][^\]]*\])"));
    QString result = text;
    if (format == "bbcode") result.replace(media, " ").replace(tags, " ");
    result = result.simplified();
    const auto codepoints = result.toUcs4();
    if (codepoints.size() <= 140) return result;
    result = QString::fromUcs4(codepoints.constData(), 140);
    const auto space = result.lastIndexOf(' ');
    if (space >= 0) result.truncate(space);
    const QString punctuation = QStringLiteral(".,;:-–— ");
    while (!result.isEmpty() && punctuation.contains(result.back())) result.chop(1);
    return result + QChar(0x2026);
}

QVariantMap item_row(const CatalogEntry& entry,
                     const std::function<InstallState(const CatalogMod&)>& install_state,
                     const QString& game_version, bool detail) {
    return std::visit([&](const auto& item) {
        using Item = std::decay_t<decltype(item)>;
        constexpr bool collection = std::is_same_v<Item, CatalogCollection>;
        QSet<QString> versions;
        for (const auto& version : item.supported_versions) versions.insert(QString::fromStdString(version));
        if constexpr (collection) {
            static const QRegularExpression version_tag(QStringLiteral(R"(^\d+\.\d+(?:\.\d+)?$)"));
            for (const auto& tag : item.tags) {
                const QString value = QString::fromStdString(tag);
                if (version_tag.match(value).hasMatch()) versions.insert(value);
            }
        }
        bool incompatible = !game_version.isEmpty() && !versions.isEmpty() && !versions.contains(game_version);
        bool no_common = false;
        InstallState state = InstallState::missing;
        QString source, action, size, votes = QStringLiteral("Unrated"), up = "0", down = "0";
        QString workshop_url, format = QStringLiteral("bbcode");
        QStringList collage;
        qlonglong member_count = 0;
        if constexpr (collection) {
            source = item.source == CollectionSource::picked ? "PxModRim pick" : "Steam collection";
            action = QStringLiteral("Download all");
            member_count = item.member_count;
            size = size_label(item.total_size);
            no_common = item.no_common_version && versions.isEmpty();
            incompatible = incompatible || no_common;
            if (item.workshop_url) workshop_url = QString::fromStdString(*item.workshop_url);
            if (item.description_format == CollectionDescriptionFormat::text) format = QStringLiteral("text");
            for (const auto& preview : item.member_previews) {
                const QString url = safe_url(QString::fromStdString(preview));
                if (!url.isEmpty()) collage.append(url);
            }
        } else {
            state = install_state(item);
            incompatible = incompatible || item.incompatible;
            source = QStringLiteral("Steam Workshop");
            switch (state) {
            case InstallState::missing: action = QStringLiteral("Download"); break;
            case InstallState::installed: action = QStringLiteral("Installed"); break;
            case InstallState::outdated: action = QStringLiteral("Update"); break;
            }
            size = size_label(item.file_size);
            workshop_url = QString::fromStdString(item.workshop_url);
            if (item.votes) {
                const QLocale locale(QLocale::English, QLocale::UnitedStates);
                up = locale.toString(static_cast<qlonglong>(item.votes->up));
                down = locale.toString(static_cast<qlonglong>(item.votes->down));
                const double total = static_cast<double>(item.votes->up) + static_cast<double>(item.votes->down);
                if (total != 0) votes = QString::number(std::nearbyint(100.0 * item.votes->up / total), 'f', 0) + '%';
            }
        }
        QString state_name, state_label;
        switch (state) {
        case InstallState::missing: state_name = "missing"; state_label = "Not installed"; break;
        case InstallState::installed: state_name = "installed"; state_label = "Installed"; break;
        case InstallState::outdated: state_name = "outdated"; state_label = "Update available"; break;
        }
        QString preview = item.preview_url ? QString::fromStdString(*item.preview_url) : QString{};
        if (preview.isEmpty()) {
            for (const auto& image : item.previews) {
                if (image.type == PreviewType::image) {
                    preview = QString::fromStdString(image.url);
                    break;
                }
            }
        }
        QString author;
        if (item.author.name) author = QString::fromStdString(*item.author.name);
        if (author.isEmpty() && item.author.id) author = QString::fromStdString(*item.author.id);
        if (author.isEmpty()) author = QStringLiteral("Unknown author");
        std::vector<std::pair<std::vector<int>, QString>> sorted;
        sorted.reserve(versions.size());
        for (const auto& version : versions) {
            std::vector<int> parts;
            for (const auto& part : version.split('.')) parts.push_back(part.toInt());
            sorted.emplace_back(std::move(parts), version);
        }
        std::sort(sorted.begin(), sorted.end());
        QStringList version_labels, tag_labels;
        for (const auto& version : sorted) version_labels.append(version.second);
        for (const auto& tag : item.tags) {
            const QString value = QString::fromStdString(tag);
            if (!versions.contains(value)) tag_labels.append(value);
        }
        const QString description = QString::fromStdString(item.description);
        return QVariantMap{
            {"itemId", QString::fromStdString(item.id)}, {"kind", collection ? "collection" : "mod"},
            {"title", QString::fromStdString(item.title)}, {"author", author}, {"previewUrl", safe_url(preview)},
            {"collage", collage}, {"sourceLabel", source},
            {"versions", version_labels.isEmpty() ? QStringLiteral("Not specified") : version_labels.join(", ")},
            {"tags", tag_labels.join(QStringLiteral(" · "))}, {"state", state_name}, {"active", false},
            {"queued", false}, {"favourite", false}, {"stateLabel", state_label}, {"actionLabel", action},
            {"incompatible", incompatible}, {"memberCount", member_count},
            {"description", detail ? px::modrim::description_to_html(description, format) : QString{}},
            {"workshopUrl", safe_url(workshop_url)}, {"fileSize", size}, {"votes", votes},
            {"votesUp", up}, {"votesDown", down}, {"summary", summarize(description, format)},
            {"compatLabel", compat_label(versions, game_version, incompatible, no_common)}};
    }, entry);
}

CatalogListModel::CatalogListModel(QObject* parent) : QAbstractListModel(parent) {}

void CatalogListModel::set_page(QList<QVariantMap> rows, int total, std::optional<QString> cursor, bool append) {
    if (append) {
        QSet<QString> known;
        for (const auto& row : rows_) known.insert(row.value("itemId").toString());
        QList<QVariantMap> additions;
        for (auto& row : rows) {
            const QString id = row.value("itemId").toString();
            if (!known.contains(id)) {
                known.insert(id);
                additions.append(std::move(row));
            }
        }
        if (!additions.isEmpty()) {
            beginInsertRows({}, count(), count() + static_cast<int>(additions.size()) - 1);
            rows_.append(std::move(additions));
            endInsertRows();
        }
    } else if (rows.size() == rows_.size() && std::equal(rows.cbegin(), rows.cend(), rows_.cbegin(),
                   [](const auto& a, const auto& b) { return a.value("itemId") == b.value("itemId"); })) {
        for (qsizetype i = 0; i < rows.size(); ++i) {
            if (rows.at(i) != rows_.at(i)) {
                rows_[i] = std::move(rows[i]);
                emit dataChanged(index(static_cast<int>(i)), index(static_cast<int>(i)));
            }
        }
    } else {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
    }
    total_ = total;
    cursor_ = std::move(cursor);
    emit changed();
}

void CatalogListModel::refresh_rows(const std::function<QVariantMap(const QVariantMap&)>& convert) {
    for (auto& row : rows_) row = convert(row);
    if (!rows_.isEmpty()) emit dataChanged(index(0), index(count() - 1));
}

int CatalogListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : count();
}

QVariant CatalogListModel::data(const QModelIndex& index, int role) const {
    const int offset = role - Qt::UserRole - 1;
    if (!index.isValid() || index.row() < 0 || index.row() >= count() || offset < 0 || offset >= static_cast<int>(roles.size())) return {};
    return rows_.at(index.row()).value(QString::fromLatin1(roles[offset]));
}

QHash<int, QByteArray> CatalogListModel::roleNames() const {
    QHash<int, QByteArray> names;
    for (std::size_t i = 0; i < roles.size(); ++i) names.insert(Qt::UserRole + static_cast<int>(i) + 1, roles[i]);
    return names;
}

}  // namespace px::modrim::workshop
