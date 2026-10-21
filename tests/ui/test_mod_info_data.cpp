#include <QFile>
#include <QHash>
#include <QTemporaryDir>
#include <memory>
#include <utility>

#include <catch2/catch_test_macros.hpp>

#include "core/downloads/manager.hpp"
#include "test_support.hpp"
#include "ui/panels/mod_info_data.hpp"

namespace px::modrim {
namespace {

std::shared_ptr<AboutXmlMod> makeMod(const QString& name, const QString& pid) {
  auto mod = std::make_shared<AboutXmlMod>();
  mod->name = name;
  mod->packageId = CaseInsensitiveStr(pid);
  mod->description.clear();
  return mod;
}

void addDependency(AboutXmlMod& mod, const QString& pid,
                   const QStringList& alternatives = {}) {
  DependencyMod dependency;
  dependency.name = pid;
  dependency.packageId = CaseInsensitiveStr(pid);
  dependency.alternativePackageIds = CaseInsensitiveSet(alternatives);
  mod.aboutRules.dependencies.append(std::move(dependency));
}

struct ModInfoContext {
  QTemporaryDir game;
  std::unique_ptr<CoreContext> ctx;

  ModInfoContext(const QList<ListedModPtr>& mods, QStringList active) {
    REQUIRE(game.isValid());
    QFile version(game.filePath("Version.txt"));
    REQUIRE(version.open(QIODevice::WriteOnly));
    REQUIRE(version.write("1.5.4297") == 8);
    version.close();
    AppConfig config;
    config.paths.game = game.path().toStdString();
    ctx = px::ui_test::makeCore(config);
    // CoreContext registers the real, empty DownloadManager during
    // construction.
    REQUIRE_FALSE(ctx->downloads().available());
    QHash<QString, ListedModPtr> indexed;
    for (const auto& mod : mods) indexed.insert(mod->uuid(), mod);
    ctx->load(std::move(indexed), std::move(active));
  }
};

ModInfo buildInfo(const ModInfoContext& context, const ListedMod& mod,
                  const QList<view::ModIssueView>& issues = {}) {
  auto data = buildModInfo(*context.ctx, mod);
  updateActiveState(context.ctx->activeUuids(), mod.uuid(), data);
  updateIssues(issues, data.info);
  return data;
}

}  // namespace

TEST_CASE("Mod info autolinks bare URLs without changing markup",
          "[ui][mod-info]") {
  CHECK(autolink("see https://a.com/x?y=1 now") ==
        "see <a href=\"https://a.com/x?y=1\">https://a.com/x?y=1</a> now");
  CHECK(autolink("go to https://a.com.") ==
        "go to <a href=\"https://a.com\">https://a.com</a>.");
  const QString anchor = "<a href=\"https://b.com\">https://b.com</a>";
  CHECK(autolink(anchor) == anchor);
  CHECK(
      autolink("https://a.com/?a=1&amp;b=2") ==
      "<a href=\"https://a.com/?a=1&amp;b=2\">https://a.com/?a=1&amp;b=2</a>");
}

TEST_CASE("Mod info converts BBCode images and linked images",
          "[ui][mod-info]") {
  CHECK(bbcodeImages("[img]https://i.imgur.com/a.png[/img]\nhi") ==
        "<img src=\"https://i.imgur.com/a.png\" width=\"" + kImgWidthToken +
            "\">\nhi");
  CHECK_FALSE(autolink(bbcodeImages("[img]https://i.imgur.com/a.png[/img]"))
                  .contains("<a "));
  const auto html = bbcodeMarkup(bbcodeImages(
      "[url=https://discord.gg/x][img]https://i.imgur.com/a.png[/img][/url]"));
  CHECK(html.startsWith("<a href=\"https://discord.gg/x\"><img src="));
  CHECK(html.endsWith("></a>"));
}

TEST_CASE("Mod info converts BBCode links and formatting", "[ui][mod-info]") {
  CHECK(bbcodeMarkup("[url=https://a.com/w]the wiki[/url]") ==
        "<a href=\"https://a.com/w\">the wiki</a>");
  const QString unsafe = "[url=javascript:alert(1)]x[/url]";
  CHECK(bbcodeMarkup(unsafe) == unsafe);
  CHECK(bbcodeMarkup("[b]a[/b] [i]b[/i]") == "<b>a</b> <i>b</i>");
}

TEST_CASE("Mod info converts Steam structural tags without stray breaks",
          "[ui][mod-info]") {
  CHECK(bbcodeMarkup("[list]<br>[*]one<br>[*]two<br>[/list]<br>after") ==
        "<ul><li>one<li>two</ul>after");
  CHECK(bbcodeMarkup("[h2]Title[/h2]<br>body") == "<h2>Title</h2>body");
  CHECK(bbcodeMarkup("[quote=Bob]hi[/quote]") ==
        "<blockquote><i>Bob wrote:</i><br>hi</blockquote>");
  CHECK(bbcodeMarkup("[table noborder=1][tr][td]x[/td][/tr][/table]") ==
        "<table><tr><td>x</td></tr></table>");
}

TEST_CASE("Mod info restores entities once and excludes URL delimiters",
          "[ui][mod-info]") {
  CHECK(restoreEntities("&amp;#8226; item") == "&#8226; item");
  CHECK(restoreEntities("a &amp; b") == "a &amp; b");
  CHECK(descriptionToHtml("see \"https://a.com/x\" ok") ==
        "see &quot;<a href=\"https://a.com/x\">https://a.com/x</a>&quot; ok");
  CHECK(descriptionToHtml("<https://a.com/x>") ==
        "&lt;<a href=\"https://a.com/x\">https://a.com/x</a>&gt;");
}

TEST_CASE("Mod info rich descriptions preserve formatting entities and images",
          "[ui][mod-info]") {
  CHECK(descriptionToHtml(
            "[h2]Title[/h2]\n<b>Bold</b> &#8226;\n"
            "[img]https://a.com/image.png[/img]\nhttps://a.com/wiki") ==
        "<h2>Title</h2><b>Bold</b> &#8226;<br>"
        "<img src=\"https://a.com/image.png\" width=\"" +
            kImgWidthToken +
            "\"><br>"
            "<a href=\"https://a.com/wiki\">https://a.com/wiki</a>");
}

TEST_CASE("Mod info plaintext descriptions remain literal with line breaks",
          "[ui][mod-info]") {
  CHECK(descriptionToHtml(
            "<b>Literal & text</b>\n[img]https://a.com/image.png[/img]\n"
            "[url=https://a.com]wiki[/url]",
            "plaintext") ==
        "&lt;b&gt;Literal &amp; text&lt;/b&gt;<br>"
        "[img]https://a.com/image.png[/img]<br>[url=https://a.com]wiki[/url]");
}

TEST_CASE("Mod info only embeds HTTPS images", "[ui][mod-info][security]") {
  CHECK_FALSE(
      descriptionToHtml("[img]file:///etc/passwd[/img]").contains("<img"));
  const auto html = descriptionToHtml("[img]http://a.com/i.png[/img]");
  CHECK_FALSE(html.contains("<img"));
  CHECK(html.contains("<a href=\"http://a.com/i.png\">"));
  CHECK_FALSE(
      descriptionToHtml("[url=javascript:alert(1)]x[/url]").contains("<a "));
}

TEST_CASE("Mod info escapes raw HTML and URL attribute injection",
          "[ui][mod-info][security]") {
  const auto html =
      descriptionToHtml("<a href=\"evil\">x</a> <b onclick=\"1\">y</b>");
  CHECK_FALSE(html.contains("<a "));
  CHECK_FALSE(html.contains("<b "));
  CHECK(html.contains("&lt;a href="));
  const auto injection =
      descriptionToHtml("[url=https://a.com/x\" onclick=\"evil]t[/url]");
  CHECK_FALSE(injection.contains("onclick=\""));
  CHECK_FALSE(injection.contains("\" onclick"));
  CHECK(descriptionToHtml("<img src=\"file:///etc/passwd\">") ==
        "&lt;img src=&quot;file:///etc/passwd&quot;&gt;");
}

TEST_CASE("Mod info rejects unsafe schemes in every rich link form",
          "[ui][mod-info][security]") {
  const QStringList urls{"javascript:alert(1)", "data:image/png;base64,AAAA",
                         "file:///etc/passwd", "qrc:/private.png",
                         "steam://run/294100"};
  for (const auto& url : urls) {
    CAPTURE(url);
    const auto html = descriptionToHtml(
        "[url=" + url + "]link[/url] [url]" + url + "[/url] [img]" + url +
        "[/img] <link=" + url + ">Unity link</link>");
    CHECK_FALSE(html.contains("<a "));
    CHECK_FALSE(html.contains("<img "));
  }
}

TEST_CASE(
    "Mod info reports dependency states and case insensitive alternatives",
    "[ui][mod-info]") {
  auto active = makeMod("Active", "a.active");
  auto inactive = makeMod("Inactive", "a.inactive");
  auto alternative = makeMod("Alt", "a.alt");
  auto main = makeMod("Main", "a.main");
  addDependency(*main, "a.active");
  addDependency(*main, "A.Inactive");
  addDependency(*main, "a.missing", {"a.alt"});
  addDependency(*main, "a.gone");
  ModInfoContext context({main, active, inactive, alternative},
                         {active->uuid()});
  const auto needs = buildInfo(context, *main).info.value("needs").toList();
  const QVariantList expected{
      QVariantMap{
          {"name", "a.active"}, {"state", "active"}, {"uuid", active->uuid()}},
      QVariantMap{{"name", "A.Inactive"},
                  {"state", "inactive"},
                  {"uuid", inactive->uuid()}},
      QVariantMap{{"name", "a.missing"},
                  {"state", "inactive"},
                  {"uuid", alternative->uuid()}},
      QVariantMap{{"name", "a.gone"}, {"state", "missing"}, {"uuid", ""}}};
  CHECK(needs == expected);
}

TEST_CASE("Mod info conflicts include only active mods sorted by name",
          "[ui][mod-info]") {
  auto beta = makeMod("beta", "x.b");
  auto alpha = makeMod("Alpha", "x.a");
  auto off = makeMod("Off", "x.off");
  auto main = makeMod("Main", "x.main");
  main->aboutRules.incompatibleWith =
      CaseInsensitiveSet(QStringList{"X.B", "x.a", "x.off", "x.no"});
  ModInfoContext context({main, alpha, beta, off},
                         {alpha->uuid(), beta->uuid()});
  CHECK(buildInfo(context, *main).info.value("conflicts").toStringList() ==
        QStringList{"Alpha", "beta"});
}

TEST_CASE("Mod info needed by matches alternatives and sorts dependents",
          "[ui][mod-info]") {
  auto target = makeMod("Target", "t.target");
  auto zed = makeMod("zed", "t.z");
  auto apple = makeMod("Apple", "t.a");
  auto unrelated = makeMod("Unrelated", "t.u");
  addDependency(*zed, "t.target");
  addDependency(*apple, "t.other", {"T.Target"});
  addDependency(*unrelated, "t.other");
  ModInfoContext context({target, zed, apple, unrelated}, {});
  const QVariantList expected{
      QVariantMap{{"name", "Apple"}, {"uuid", apple->uuid()}},
      QVariantMap{{"name", "zed"}, {"uuid", zed->uuid()}}};
  CHECK(buildInfo(context, *target).info.value("neededBy").toList() ==
        expected);
}

TEST_CASE(
    "Mod info builds active mod status issues dependencies and description",
    "[ui][mod-info]") {
  auto dep = makeMod("Dep", "d.dep");
  auto main = makeMod("Main", "m.main");
  addDependency(*main, "d.dep");
  main->description = "hello <b>x</b> https://a.com/x";
  main->authors = {"Ann"};
  auto other = makeMod("Other", "o.other");
  ModInfoContext context({dep, main, other}, {other->uuid(), main->uuid()});
  const view::ModIssueView issue{"Cat", std::nullopt, false};
  const auto data = buildInfo(context, *main, {issue});
  const auto& info = data.info;
  CHECK(info.value("name").toString() == "Main");
  CHECK(info.value("author").toString() == "Ann");
  const auto status = info.value("status").toMap();
  CHECK(status.value("level").toString() == "warning");
  CHECK(status.value("title").toString() == "1 warning");
  CHECK(status.value("active").toBool());
  CHECK(status.value("position").toInt() == 2);
  CHECK(status.value("total").toInt() == 2);
  const QVariantList expectedIssues{
      QVariantMap{{"title", "Cat"}, {"detail", ""}, {"isError", false}}};
  CHECK(status.value("issues").toList() == expectedIssues);
  const QVariantList expectedNeeds{QVariantMap{
      {"name", "d.dep"}, {"state", "inactive"}, {"uuid", dep->uuid()}}};
  CHECK(info.value("needs").toList() == expectedNeeds);
  CHECK(data.description.contains("<a href=\"https://a.com/x\">"));
  const QVariantMap expectedVersion{
      {"known", false}, {"ok", false}, {"target", "1.5"}};
  CHECK(info.value("version").toMap() == expectedVersion);
}

TEST_CASE("Mod info builds inactive mod without description or dependents",
          "[ui][mod-info]") {
  auto mod = makeMod("Solo", "s.solo");
  ModInfoContext context({mod}, {});
  const auto data = buildInfo(context, *mod);
  const auto& info = data.info;
  const auto status = info.value("status").toMap();
  CHECK(status.value("level").toString() == "ok");
  CHECK_FALSE(status.value("active").toBool());
  CHECK(status.value("position").toInt() == 0);
  CHECK(data.description.isEmpty());
  CHECK(info.value("neededBy").toList().isEmpty());
}

TEST_CASE("Mod info issue updates leave metadata and dependency state alone",
          "[ui][mod-info]") {
  auto dep = makeMod("Dep", "d.dep");
  auto main = makeMod("Main", "m.main");
  addDependency(*main, "d.dep");
  main->description = "https://a.com/x";
  ModInfoContext context({dep, main}, {main->uuid()});
  const auto data = buildInfo(context, *main);
  auto info = data.info;
  updateIssues({view::ModIssueView{"Cat", QStringLiteral("Detail"), true}},
               info);
  const auto status = info.value("status").toMap();
  CHECK(status.value("level").toString() == "error");
  CHECK(status.value("title").toString() == "1 error");
  const QVariantList expectedIssues{
      QVariantMap{{"title", "Cat"}, {"detail", "Detail"}, {"isError", true}}};
  CHECK(status.value("issues").toList() == expectedIssues);
  CHECK(status.value("active").toBool());
  CHECK(status.value("position").toInt() == 1);
  CHECK(info.value("needs") == data.info.value("needs"));
  CHECK(info.value("details") == data.info.value("details"));
  CHECK(info.value("author") == data.info.value("author"));
  CHECK(info.value("conflicts") == data.info.value("conflicts"));
}

TEST_CASE("Mod info active updates refresh positions states and conflicts",
          "[ui][mod-info]") {
  auto dep = makeMod("Dep", "d.dep");
  auto alpha = makeMod("Alpha", "c.a");
  auto main = makeMod("Main", "m.main");
  addDependency(*main, "d.dep");
  main->aboutRules.incompatibleWith = CaseInsensitiveSet(QStringList{"c.a"});
  ModInfoContext context({dep, alpha, main}, {main->uuid()});
  auto data = buildModInfo(*context.ctx, *main);
  updateActiveState(context.ctx->activeUuids(), main->uuid(), data);
  CHECK(data.info.value("status").toMap().value("position").toInt() == 1);
  CHECK(data.info.value("conflicts").toStringList().isEmpty());
  const auto description = data.description;
  updateActiveState({alpha->uuid(), dep->uuid(), main->uuid()}, main->uuid(),
                    data);
  const auto status = data.info.value("status").toMap();
  CHECK(status.value("active").toBool());
  CHECK(status.value("position").toInt() == 3);
  CHECK(status.value("total").toInt() == 3);
  CHECK(data.info.value("conflicts").toStringList() == QStringList{"Alpha"});
  CHECK(data.info.value("needs").toList() ==
        QVariantList{QVariantMap{
            {"name", "d.dep"}, {"uuid", dep->uuid()}, {"state", "active"}}});
  CHECK(data.description == description);
}

TEST_CASE("Mod info metadata rebuild refreshes description and authors",
          "[ui][mod-info]") {
  auto mod = makeMod("Old", "r.mod");
  mod->description = "old";
  mod->authors = {"Ann"};
  ModInfoContext context({mod}, {});
  const auto before = buildInfo(context, *mod);
  mod->name = "New";
  mod->description = "new https://a.com/x";
  mod->authors = {"Ann", "Bob"};
  const auto after = buildInfo(context, *mod);
  CHECK(before.description == "old");
  CHECK(after.description.contains("<a href=\"https://a.com/x\">"));
  CHECK(after.info.value("name").toString() == "New");
  CHECK(after.info.value("author").toString() == "Ann, Bob");
}

TEST_CASE("Mod info age formatting has deterministic boundaries and plurals",
          "[ui][mod-info]") {
  constexpr double kNow = 2'000'000'000;
  CHECK(formatAge(0, kNow).isEmpty());
  CHECK(formatAge(-1, kNow).isEmpty());
  CHECK(formatAge(kNow + 1, kNow) == "just now");
  CHECK(formatAge(kNow, kNow) == "just now");
  CHECK(formatAge(kNow - 3599, kNow) == "just now");
  struct AgeCase {
    double elapsed;
    const char* expected;
  };
  const AgeCase cases[]{{3600, "1 hour ago"},
                        {7200, "2 hours ago"},
                        {86399, "23 hours ago"},
                        {86400, "1 day ago"},
                        {2 * 86400, "2 days ago"},
                        {7 * 86400 - 1, "6 days ago"},
                        {7 * 86400, "1 week ago"},
                        {14 * 86400, "2 weeks ago"},
                        {30 * 86400 - 1, "4 weeks ago"},
                        {30 * 86400, "1 month ago"},
                        {60 * 86400, "2 months ago"},
                        {365 * 86400 - 1, "12 months ago"},
                        {365 * 86400, "1 year ago"},
                        {730 * 86400, "2 years ago"}};
  for (const auto& age : cases) {
    CAPTURE(age.elapsed);
    CHECK(formatAge(kNow - age.elapsed, kNow) == age.expected);
  }
}

}  // namespace px::modrim
