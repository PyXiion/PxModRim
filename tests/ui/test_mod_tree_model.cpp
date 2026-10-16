#include <QPersistentModelIndex>
#include <QSignalSpy>

#include <catch2/catch_test_macros.hpp>

#include "ui/plugins/organizer/filter_model.hpp"
#include "ui/plugins/organizer/tree_model.hpp"

using namespace px::modrim;
using namespace px::modrim::organizer;
using px::modrim::ModTreeModel;
using px::modrim::OrganizerFilterModel;

namespace {
struct OrganizerFixture {
  OrganizerState state;
  ModsByUuid mods;
  QSet<QString> active{QStringLiteral("u1")};

  OrganizerFixture() {
    state.folders.insert(kRootId,
                         {kRootId, std::nullopt, QStringLiteral("Root")});
    auto add = [&](const QString& uuid, const QString& name, const QString& pid,
                   const QString& provider) {
      auto mod = std::make_shared<AboutXmlMod>();
      mod->name = name;
      mod->packageId = CaseInsensitiveStr(pid);
      mod->providerId = provider;
      mods.insert(uuid, mod);
    };
    add("u1", "One", "a.one", "local");
    add("u2", "Two", "a.two", "steam");
    add("u3", "Loose", "loose.mod", "local");
  }

  void nested() {
    state.folders.insert(2, {2, kRootId, QStringLiteral("Top")});
    state.folders.insert(3, {3, 2, QStringLiteral("Sub")});
    state.placements.insert("a.one", 2);
    state.placements.insert("a.two", 3);
  }

  FolderNode tree(const std::optional<TreeQuery>& query = std::nullopt) const {
    return buildTree(state, mods, active, query);
  }
};

ModTreeModel::SummaryFor noDiagnostics =
    [](const QString&) -> std::optional<view::ModDiagnosticsView> {
  return std::nullopt;
};
}  // namespace

TEST_CASE("organizer tree has nested folder rows and trailing ungrouped") {
  OrganizerFixture f;
  f.nested();
  ModTreeModel model(noDiagnostics);
  model.setTree(f.tree(), f.mods, {}, false);
  REQUIRE(model.rowCount() == 2);
  const auto top = model.index(0, 0);
  const auto ungrouped = model.index(1, 0);
  CHECK(model.data(top).toString() == "Top");
  CHECK(model.data(ungrouped).toString() == "Ungrouped");
  CHECK(model.data(model.index(0, 0, top)).toString() == "Sub");
  CHECK(model.data(model.index(1, 0, top)).toString() == "One");
  CHECK(model.data(model.index(0, 0, ungrouped)).toString() == "Loose");
  CHECK(model.parent(model.index(0, 0, top)) == top);
  CHECK(model.parent(model.index(0, 0, ungrouped)) == ungrouped);
  CHECK_FALSE(model.parent(top).isValid());
  CHECK(model.data(ungrouped, ModTreeModel::kTotalCountRole).toInt() == 1);
  CHECK(model.data(top, ModTreeModel::kTotalCountRole).toInt() == 2);
  CHECK(model.nodeAt(top)->folderId() == 2);
  CHECK(model.nodeAt(model.indexForUuid("u2"))->folderId() == 3);
  CHECK(model.nodeAt(ungrouped)->folderId() == kRootId);
  CHECK(model.nodeAt({}) == nullptr);
  CHECK_FALSE(model.index(-1, 0).isValid());
  CHECK_FALSE(model.index(0, 1).isValid());
  CHECK_FALSE(model.indexForKey("missing").isValid());
  CHECK_FALSE(model.indexForUuid("missing").isValid());
  CHECK_FALSE(model.data({}).isValid());
  CHECK(model.columnCount() == 1);
}

TEST_CASE("organizer check states retain total counts while filtering") {
  OrganizerFixture f;
  f.state.folders.insert(2, {2, kRootId, QStringLiteral("Group")});
  f.state.placements.insert("a.one", 2);
  f.state.placements.insert("a.two", 2);
  ModTreeModel model(noDiagnostics);
  model.setTree(f.tree(), f.mods, {}, false);
  auto group = model.index(0, 0);
  CHECK(model.data(group, ModTreeModel::kCheckStateRole).toInt() ==
        Qt::PartiallyChecked);
  CHECK(model.data(model.index(0, 0, group), ModTreeModel::kCheckStateRole)
            .toInt() == Qt::Checked);
  CHECK(model.data(model.index(1, 0, group), ModTreeModel::kCheckStateRole)
            .toInt() == Qt::Unchecked);
  CHECK(model.data(model.index(1, 0), ModTreeModel::kCheckStateRole).toInt() ==
        Qt::Unchecked);
  TreeQuery query;
  query.text = QStringLiteral("One");
  model.setTree(f.tree(query), f.mods, {}, true);
  group = model.index(0, 0);
  CHECK(model.rowCount() == 1);
  CHECK(model.filtering());
  CHECK(model.data(group, ModTreeModel::kFilteringRole).toBool());
  CHECK(model.data(group, ModTreeModel::kVisibleCountRole).toInt() == 1);
  CHECK(model.data(group, ModTreeModel::kTotalCountRole).toInt() == 2);
  CHECK(model.data(group, ModTreeModel::kEnabledCountRole).toInt() == 1);
  CHECK(model.data(group, ModTreeModel::kCheckStateRole).toInt() ==
        Qt::PartiallyChecked);
  CHECK(model.indexForKey("f:2").isValid());
}

TEST_CASE("organizer issue markers only show on enabled mods") {
  OrganizerFixture f;
  view::ModDiagnosticsView summary{true, true, QStringLiteral("Error"),
                                   QStringLiteral("Incompatible")};
  int calls = 0;
  ModTreeModel model(
      [&](const QString&) -> std::optional<view::ModDiagnosticsView> {
        ++calls;
        return summary;
      });
  model.setTree(f.tree(), f.mods, {}, false);
  const auto enabled = model.indexForUuid("u1");
  const auto inactive = model.indexForUuid("u2");
  CHECK(model.parent(enabled) == model.index(0, 0));
  CHECK(model.data(enabled, ModTreeModel::kHasErrorRole).toBool());
  CHECK(model.data(enabled, ModTreeModel::kHasWarningRole).toBool());
  CHECK(model.data(enabled, ModTreeModel::kErrorTooltipRole).toString() ==
        "Error");
  CHECK(model.data(enabled, ModTreeModel::kWarningTooltipRole).toString() ==
        "Incompatible");
  calls = 0;
  CHECK_FALSE(model.data(inactive, ModTreeModel::kHasErrorRole).toBool());
  CHECK_FALSE(model.data(inactive, ModTreeModel::kHasWarningRole).toBool());
  CHECK(model.data(inactive, ModTreeModel::kErrorTooltipRole)
            .toString()
            .isEmpty());
  CHECK(model.data(inactive, ModTreeModel::kWarningTooltipRole)
            .toString()
            .isEmpty());
  CHECK(calls == 0);
  f.active.insert("u2");
  model.setTree(f.tree(), f.mods, {}, false);
  CHECK(model.data(model.indexForUuid("u2"), ModTreeModel::kHasWarningRole)
            .toBool());
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  model.refreshDiagnostics();
  REQUIRE(changed.count() == 2);
  CHECK(changed.first()[2].value<QList<int>>() ==
        QList<int>{ModTreeModel::kHasErrorRole, ModTreeModel::kErrorTooltipRole,
                   ModTreeModel::kHasWarningRole,
                   ModTreeModel::kWarningTooltipRole});
}

TEST_CASE(
    "organizer same-shape updates preserve persistent indexes and selection") {
  OrganizerFixture f;
  f.nested();
  ModTreeModel model(noDiagnostics);
  model.setTree(f.tree(), f.mods, {}, false);
  QPersistentModelIndex one(model.indexForUuid("u1"));
  model.select({one});
  model.select({model.indexForUuid("u2")}, "extend");
  REQUIRE(model.selectedNodes().size() == 2);
  model.select({model.indexForUuid("u2")}, "toggle");
  CHECK_FALSE(model.isSelected(model.indexForUuid("u2")));
  CHECK(model.isSelected(one));
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  f.active.insert("u2");
  model.setTree(f.tree(), f.mods, {}, false);
  CHECK(reset.isEmpty());
  CHECK(one.isValid());
  CHECK(model.data(one).toString() == "One");
  CHECK(model.data(one, ModTreeModel::kSelectedRole).toBool());
  model.clearSelection();
  CHECK(model.selectedNodes().isEmpty());
  model.select({model.indexForUuid("u3")});
  TreeQuery query;
  query.text = QStringLiteral("One");
  model.setTree(f.tree(query), f.mods, {}, true);
  CHECK(reset.count() == 1);
  CHECK_FALSE(one.isValid());
  CHECK(model.selectedNodes().isEmpty());
}

TEST_CASE(
    "organizer expansion respects collapsed ancestors except when filtering") {
  OrganizerFixture f;
  f.nested();
  f.state.folders[2].collapsed = true;
  ModTreeModel model(noDiagnostics);
  model.setTree(f.tree(), f.mods, {}, false);
  auto expanded = model.expandedIndexes();
  REQUIRE(expanded.size() == 1);
  CHECK(model.data(expanded.first(), ModTreeModel::kKeyRole).toString() ==
        "ungrouped");
  model.setTree(f.tree(), f.mods, {}, true);
  expanded = model.expandedIndexes();
  REQUIRE(expanded.size() == 3);
  CHECK(model.data(expanded[0], ModTreeModel::kKeyRole).toString() == "f:2");
  CHECK(model.data(expanded[1], ModTreeModel::kKeyRole).toString() == "f:3");
  CHECK(model.expandedIndexes(model.indexForKey("f:2")).size() == 1);
}

TEST_CASE("organizer provider metadata tags and rule badges expose QML roles") {
  OrganizerFixture f;
  f.nested();
  f.state.tags.insert(7, {7, QStringLiteral("Later"), QStringLiteral("blue")});
  f.state.tags.insert(2, {2, QStringLiteral("First"), QStringLiteral("red")});
  f.state.modTags.insert("a.one", {7, 2, 99});
  auto about = std::dynamic_pointer_cast<AboutXmlMod>(f.mods["u1"]);
  about->modVersion = QStringLiteral("1.2.3");
  auto tree = f.tree();
  tree.children[0].hasRules = true;
  tree.hasRules = true;
  ModTreeModel model(noDiagnostics);
  model.setTree(tree, f.mods,
                {{QStringLiteral("local"), QStringLiteral("green")}}, false,
                f.state.tags);
  const auto one = model.indexForUuid("u1");
  CHECK(model.data(one, ModTreeModel::kUuidRole).toString() == "u1");
  CHECK(model.data(one, ModTreeModel::kPackageIdRole).toString() == "a.one");
  CHECK(model.data(one, ModTreeModel::kCanPlaceRole).toBool());
  CHECK(model.data(one, ModTreeModel::kProviderLabelRole).toString() ==
        "Local");
  CHECK(model.data(one, ModTreeModel::kProviderIconRole).toString() ==
        "folder");
  CHECK(model.data(one, ModTreeModel::kProviderColorRole).toString() ==
        "green");
  CHECK(model.data(one, ModTreeModel::kModVersionRole).toString() == "1.2.3");
  CHECK(model.data(model.indexForUuid("u2"), ModTreeModel::kProviderIconRole)
            .toString() == "steam");
  const auto tags = model.data(one, ModTreeModel::kTagsRole).toList();
  REQUIRE(tags.size() == 2);
  CHECK(tags[0].toMap()["name"].toString() == "First");
  CHECK(tags[1].toMap()["color"].toString() == "blue");
  CHECK(model.data(model.indexForKey("f:2"), ModTreeModel::kHasRuleRole)
            .toBool());
  CHECK_FALSE(
      model.data(model.indexForKey("ungrouped"), ModTreeModel::kHasRuleRole)
          .toBool());
  CHECK(model.roleNames()[ModTreeModel::kProviderLabelRole] == "providerLabel");
  CHECK(model.roleNames()[ModTreeModel::kTagsRole] == "tags");

  auto invalid = std::make_shared<ListedMod>();
  invalid->providerId = QStringLiteral("git");
  f.mods.insert("invalid", invalid);
  model.setTree(f.tree(), f.mods, {}, false);
  const auto idx = model.indexForUuid("invalid");
  REQUIRE(idx.isValid());
  CHECK_FALSE(model.data(idx, ModTreeModel::kCanPlaceRole).toBool());
  CHECK(model.data(idx, ModTreeModel::kPackageIdRole).toString().isEmpty());
  CHECK(model.data(idx, ModTreeModel::kProviderIconRole).toString() == "git");
  CHECK(model.data(idx, ModTreeModel::kModVersionRole).toString().isEmpty());
}

TEST_CASE(
    "organizer filter sections icons and fallbacks match sidebar entries") {
  OrganizerFilterModel model;
  CHECK(model.rowCount() == 0);
  CHECK(model.forKey("missing").key == "all");
  CHECK_FALSE(model.keyAt(0));
  QList<TreeFilter> entries{
      {QStringLiteral("all"), QStringLiteral("All"), 10},
      {QStringLiteral("active"), QStringLiteral("Active"), 6,
       StatusFilter::kActive},
      {QStringLiteral("inactive"), QStringLiteral("Inactive"), 4,
       StatusFilter::kInactive},
      {QStringLiteral("provider:steam"), QStringLiteral("Steam"), 8,
       StatusFilter::kAll, QStringLiteral("steam")},
      {QStringLiteral("tag:1"), QStringLiteral("Medieval"), 5,
       StatusFilter::kAll, std::nullopt, 1},
      {QStringLiteral("tag:2"), QStringLiteral("Empty"), 0, StatusFilter::kAll,
       std::nullopt, 2}};
  model.update(entries);
  REQUIRE(model.rowCount() == 6);
  const QStringList sections{"Status",    "Status", "Status",
                             "Providers", "Tags",   "Tags"};
  const QStringList icons{"grid", "check-circle", "ban", "steam", "tag", "tag"};
  for (int row = 0; row < 6; ++row) {
    CHECK(model.data(model.index(row), OrganizerFilterModel::kSectionRole)
              .toString() == sections[row]);
    CHECK(model.data(model.index(row), OrganizerFilterModel::kIconRole)
              .toString() == icons[row]);
  }
  CHECK(model.data(model.index(4), OrganizerFilterModel::kKeyRole).toString() ==
        "tag:1");
  CHECK(model.data(model.index(4)).toString() == "Medieval");
  CHECK(
      model.data(model.index(4), OrganizerFilterModel::kLabelRole).toString() ==
      "Medieval");
  CHECK(model.data(model.index(4), OrganizerFilterModel::kCountRole).toInt() ==
        5);
  CHECK(model.data(model.index(5), OrganizerFilterModel::kCountRole).toInt() ==
        0);
  CHECK(model.forKey("tag:1").label == "Medieval");
  CHECK(model.forKey("tag:999").key == "all");
  CHECK(model.keyAt(4) == QStringLiteral("tag:1"));
  CHECK_FALSE(model.keyAt(-1));
  CHECK_FALSE(model.keyAt(6));
  CHECK(model.rowCount(model.index(0)) == 0);
  CHECK_FALSE(model.data({}).isValid());
  CHECK(model.roleNames()[Qt::DisplayRole] == "filterLabel");
  QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
  QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
  entries[4].label = QStringLiteral("Medieval Overhaul");
  model.update(entries);
  CHECK(reset.isEmpty());
  CHECK(changed.count() == 1);
  CHECK(model.data(model.index(4)).toString() == "Medieval Overhaul");
  entries.removeLast();
  model.update(entries);
  CHECK(reset.count() == 1);
}

TEST_CASE(
    "organizer filter infers sections from keys and unknown providers use "
    "folders") {
  OrganizerFilterModel model;
  model.update(
      {{QStringLiteral("provider:custom"), QStringLiteral("Custom"), 1},
       {QStringLiteral("tag:9"), QStringLiteral("Tag"), 1}});
  CHECK(model.data(model.index(0), OrganizerFilterModel::kSectionRole)
            .toString() == "Providers");
  CHECK(
      model.data(model.index(0), OrganizerFilterModel::kIconRole).toString() ==
      "folder");
  CHECK(model.data(model.index(1), OrganizerFilterModel::kSectionRole)
            .toString() == "Tags");
  CHECK(
      model.data(model.index(1), OrganizerFilterModel::kIconRole).toString() ==
      "tag");
}
