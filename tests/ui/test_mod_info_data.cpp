#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <utility>

#include <QApplication>
#include <QFile>
#include <QHash>
#include <QTemporaryDir>

#include "test_support.h"
#include "core/downloads/manager.h"
#include "ui/panels/mod_info_data.h"

namespace px::modrim {
namespace {

std::shared_ptr<AboutXmlMod> make_mod(const QString& name, const QString& pid) {
    auto mod = std::make_shared<AboutXmlMod>();
    mod->name = name;
    mod->package_id = CaseInsensitiveStr(pid);
    mod->description.clear();
    return mod;
}

void add_dependency(AboutXmlMod& mod, const QString& pid,
                    const QStringList& alternatives = {}) {
    DependencyMod dependency;
    dependency.name = pid;
    dependency.package_id = CaseInsensitiveStr(pid);
    dependency.alternative_package_ids = CaseInsensitiveSet(alternatives);
    mod.about_rules.dependencies.append(std::move(dependency));
}

struct ModInfoContext {
    QTemporaryDir game;
    std::unique_ptr<CoreContext> ctx;

    ModInfoContext(const QList<ListedModPtr>& mods, QStringList active) {
        if (!QCoreApplication::instance()) {
            if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
            static int argc = 1;
            static char name[] = "pxmodrim-mod-info-tests";
            static char* argv[] = {name, nullptr};
            static QApplication application(argc, argv);
        }
        REQUIRE(game.isValid());
        QFile version(game.filePath("Version.txt"));
        REQUIRE(version.open(QIODevice::WriteOnly));
        REQUIRE(version.write("1.5.4297") == 8);
        version.close();
        AppConfig config;
        config.paths.game = game.path().toStdString();
        ctx = px::ui_test::makeCore(config);
        // CoreContext registers the real, empty DownloadManager during construction.
        REQUIRE_FALSE(ctx->downloads().available());
        QHash<QString, ListedModPtr> indexed;
        for (const auto& mod : mods) indexed.insert(mod->uuid(), mod);
        ctx->load(std::move(indexed), std::move(active));
    }
};

} // namespace

TEST_CASE("Mod info autolinks bare URLs without changing markup", "[ui][mod-info]") {
    CHECK(autolink("see https://a.com/x?y=1 now") ==
          "see <a href=\"https://a.com/x?y=1\">https://a.com/x?y=1</a> now");
    CHECK(autolink("go to https://a.com.") ==
          "go to <a href=\"https://a.com\">https://a.com</a>.");
    const QString anchor = "<a href=\"https://b.com\">https://b.com</a>";
    CHECK(autolink(anchor) == anchor);
    CHECK(autolink("https://a.com/?a=1&amp;b=2") ==
          "<a href=\"https://a.com/?a=1&amp;b=2\">https://a.com/?a=1&amp;b=2</a>");
}

TEST_CASE("Mod info converts BBCode images and linked images", "[ui][mod-info]") {
    CHECK(bbcode_images("[img]https://i.imgur.com/a.png[/img]\nhi") ==
          "<img src=\"https://i.imgur.com/a.png\" width=\"" + IMG_WIDTH_TOKEN + "\">\nhi");
    CHECK_FALSE(autolink(bbcode_images("[img]https://i.imgur.com/a.png[/img]")).contains("<a "));
    const auto html = bbcode_markup(bbcode_images(
        "[url=https://discord.gg/x][img]https://i.imgur.com/a.png[/img][/url]"));
    CHECK(html.startsWith("<a href=\"https://discord.gg/x\"><img src="));
    CHECK(html.endsWith("></a>"));
}

TEST_CASE("Mod info converts BBCode links and formatting", "[ui][mod-info]") {
    CHECK(bbcode_markup("[url=https://a.com/w]the wiki[/url]") ==
          "<a href=\"https://a.com/w\">the wiki</a>");
    const QString unsafe = "[url=javascript:alert(1)]x[/url]";
    CHECK(bbcode_markup(unsafe) == unsafe);
    CHECK(bbcode_markup("[b]a[/b] [i]b[/i]") == "<b>a</b> <i>b</i>");
}

TEST_CASE("Mod info converts Steam structural tags without stray breaks", "[ui][mod-info]") {
    CHECK(bbcode_markup("[list]<br>[*]one<br>[*]two<br>[/list]<br>after") ==
          "<ul><li>one<li>two</ul>after");
    CHECK(bbcode_markup("[h2]Title[/h2]<br>body") == "<h2>Title</h2>body");
    CHECK(bbcode_markup("[quote=Bob]hi[/quote]") ==
          "<blockquote><i>Bob wrote:</i><br>hi</blockquote>");
    CHECK(bbcode_markup("[table noborder=1][tr][td]x[/td][/tr][/table]") ==
          "<table><tr><td>x</td></tr></table>");
}

TEST_CASE("Mod info restores entities once and excludes URL delimiters", "[ui][mod-info]") {
    CHECK(restore_entities("&amp;#8226; item") == "&#8226; item");
    CHECK(restore_entities("a &amp; b") == "a &amp; b");
    CHECK(description_to_html("see \"https://a.com/x\" ok") ==
          "see &quot;<a href=\"https://a.com/x\">https://a.com/x</a>&quot; ok");
    CHECK(description_to_html("<https://a.com/x>") ==
          "&lt;<a href=\"https://a.com/x\">https://a.com/x</a>&gt;");
}

TEST_CASE("Mod info rich descriptions preserve formatting entities and images", "[ui][mod-info]") {
    CHECK(description_to_html(
              "[h2]Title[/h2]\n<b>Bold</b> &#8226;\n"
              "[img]https://a.com/image.png[/img]\nhttps://a.com/wiki") ==
          "<h2>Title</h2><b>Bold</b> &#8226;<br>"
          "<img src=\"https://a.com/image.png\" width=\"" + IMG_WIDTH_TOKEN + "\"><br>"
          "<a href=\"https://a.com/wiki\">https://a.com/wiki</a>");
}

TEST_CASE("Mod info plaintext descriptions remain literal with line breaks", "[ui][mod-info]") {
    CHECK(description_to_html(
              "<b>Literal & text</b>\n[img]https://a.com/image.png[/img]\n"
              "[url=https://a.com]wiki[/url]", "plaintext") ==
          "&lt;b&gt;Literal &amp; text&lt;/b&gt;<br>"
          "[img]https://a.com/image.png[/img]<br>[url=https://a.com]wiki[/url]");
}

TEST_CASE("Mod info only embeds HTTPS images", "[ui][mod-info][security]") {
    CHECK_FALSE(description_to_html("[img]file:///etc/passwd[/img]").contains("<img"));
    const auto html = description_to_html("[img]http://a.com/i.png[/img]");
    CHECK_FALSE(html.contains("<img"));
    CHECK(html.contains("<a href=\"http://a.com/i.png\">"));
    CHECK_FALSE(description_to_html("[url=javascript:alert(1)]x[/url]").contains("<a "));
}

TEST_CASE("Mod info escapes raw HTML and URL attribute injection", "[ui][mod-info][security]") {
    const auto html = description_to_html("<a href=\"evil\">x</a> <b onclick=\"1\">y</b>");
    CHECK_FALSE(html.contains("<a "));
    CHECK_FALSE(html.contains("<b "));
    CHECK(html.contains("&lt;a href="));
    const auto injection = description_to_html("[url=https://a.com/x\" onclick=\"evil]t[/url]");
    CHECK_FALSE(injection.contains("onclick=\""));
    CHECK_FALSE(injection.contains("\" onclick"));
    CHECK(description_to_html("<img src=\"file:///etc/passwd\">") ==
          "&lt;img src=&quot;file:///etc/passwd&quot;&gt;");
}

TEST_CASE("Mod info rejects unsafe schemes in every rich link form", "[ui][mod-info][security]") {
    const QStringList urls{"javascript:alert(1)", "data:image/png;base64,AAAA",
                           "file:///etc/passwd", "qrc:/private.png", "steam://run/294100"};
    for (const auto& url : urls) {
        CAPTURE(url);
        const auto html = description_to_html(
            "[url=" + url + "]link[/url] [url]" + url + "[/url] [img]" + url +
            "[/img] <link=" + url + ">Unity link</link>");
        CHECK_FALSE(html.contains("<a "));
        CHECK_FALSE(html.contains("<img "));
    }
}

TEST_CASE("Mod info reports dependency states and case insensitive alternatives", "[ui][mod-info]") {
    auto active = make_mod("Active", "a.active");
    auto inactive = make_mod("Inactive", "a.inactive");
    auto alternative = make_mod("Alt", "a.alt");
    auto main = make_mod("Main", "a.main");
    add_dependency(*main, "a.active");
    add_dependency(*main, "A.Inactive");
    add_dependency(*main, "a.missing", {"a.alt"});
    add_dependency(*main, "a.gone");
    ModInfoContext context({main, active, inactive, alternative}, {active->uuid()});
    const auto needs = build_mod_info(*context.ctx, *main, {}).value("needs").toList();
    const QVariantList expected{
        QVariantMap{{"name", "a.active"}, {"state", "active"}, {"uuid", active->uuid()}},
        QVariantMap{{"name", "A.Inactive"}, {"state", "inactive"}, {"uuid", inactive->uuid()}},
        QVariantMap{{"name", "a.missing"}, {"state", "inactive"}, {"uuid", alternative->uuid()}},
        QVariantMap{{"name", "a.gone"}, {"state", "missing"}, {"uuid", ""}}};
    CHECK(needs == expected);
}

TEST_CASE("Mod info conflicts include only active mods sorted by name", "[ui][mod-info]") {
    auto beta = make_mod("beta", "x.b");
    auto alpha = make_mod("Alpha", "x.a");
    auto off = make_mod("Off", "x.off");
    auto main = make_mod("Main", "x.main");
    main->about_rules.incompatible_with = CaseInsensitiveSet(
        QStringList{"X.B", "x.a", "x.off", "x.no"});
    ModInfoContext context({main, alpha, beta, off}, {alpha->uuid(), beta->uuid()});
    CHECK(build_mod_info(*context.ctx, *main, {}).value("conflicts").toStringList() ==
          QStringList{"Alpha", "beta"});
}

TEST_CASE("Mod info needed by matches alternatives and sorts dependents", "[ui][mod-info]") {
    auto target = make_mod("Target", "t.target");
    auto zed = make_mod("zed", "t.z");
    auto apple = make_mod("Apple", "t.a");
    auto unrelated = make_mod("Unrelated", "t.u");
    add_dependency(*zed, "t.target");
    add_dependency(*apple, "t.other", {"T.Target"});
    add_dependency(*unrelated, "t.other");
    ModInfoContext context({target, zed, apple, unrelated}, {});
    const QVariantList expected{
        QVariantMap{{"name", "Apple"}, {"uuid", apple->uuid()}},
        QVariantMap{{"name", "zed"}, {"uuid", zed->uuid()}}};
    CHECK(build_mod_info(*context.ctx, *target, {}).value("neededBy").toList() == expected);
}

TEST_CASE("Mod info builds active mod status issues dependencies and description", "[ui][mod-info]") {
    auto dep = make_mod("Dep", "d.dep");
    auto main = make_mod("Main", "m.main");
    add_dependency(*main, "d.dep");
    main->description = "hello <b>x</b> https://a.com/x";
    main->authors = {"Ann"};
    auto other = make_mod("Other", "o.other");
    ModInfoContext context({dep, main, other}, {other->uuid(), main->uuid()});
    const view::ModIssueView issue{"c", "Cat", std::nullopt, false};
    const auto info = build_mod_info(*context.ctx, *main, {issue});
    CHECK(info.value("name").toString() == "Main");
    CHECK(info.value("author").toString() == "Ann");
    const auto status = info.value("status").toMap();
    CHECK(status.value("level").toString() == "warning");
    CHECK(status.value("title").toString() == "1 warning");
    CHECK(status.value("active").toBool());
    CHECK(status.value("position").toInt() == 2);
    CHECK(status.value("total").toInt() == 2);
    const QVariantList expected_issues{
        QVariantMap{{"title", "Cat"}, {"detail", ""}, {"isError", false}}};
    CHECK(status.value("issues").toList() == expected_issues);
    const QVariantList expected_needs{
        QVariantMap{{"name", "d.dep"}, {"state", "inactive"}, {"uuid", dep->uuid()}}};
    CHECK(info.value("needs").toList() == expected_needs);
    CHECK(info.value("description").toString().contains("<a href=\"https://a.com/x\">"));
    const QVariantMap expected_version{{"known", false}, {"ok", false}, {"target", "1.5"}};
    CHECK(info.value("version").toMap() == expected_version);
}

TEST_CASE("Mod info builds inactive mod without description or dependents", "[ui][mod-info]") {
    auto mod = make_mod("Solo", "s.solo");
    ModInfoContext context({mod}, {});
    const auto info = build_mod_info(*context.ctx, *mod, {});
    const auto status = info.value("status").toMap();
    CHECK(status.value("level").toString() == "ok");
    CHECK_FALSE(status.value("active").toBool());
    CHECK(status.value("position").toInt() == 0);
    CHECK(info.value("description").toString().isEmpty());
    CHECK(info.value("neededBy").toList().isEmpty());
}

TEST_CASE("Mod info age formatting has deterministic boundaries and plurals", "[ui][mod-info]") {
    constexpr double now = 2'000'000'000;
    CHECK(format_age(0, now).isEmpty());
    CHECK(format_age(-1, now).isEmpty());
    CHECK(format_age(now + 1, now) == "just now");
    CHECK(format_age(now, now) == "just now");
    CHECK(format_age(now - 3599, now) == "just now");
    struct AgeCase { double elapsed; const char* expected; };
    const AgeCase cases[]{
        {3600, "1 hour ago"}, {7200, "2 hours ago"}, {86399, "23 hours ago"},
        {86400, "1 day ago"}, {2 * 86400, "2 days ago"}, {7 * 86400 - 1, "6 days ago"},
        {7 * 86400, "1 week ago"}, {14 * 86400, "2 weeks ago"},
        {30 * 86400 - 1, "4 weeks ago"}, {30 * 86400, "1 month ago"},
        {60 * 86400, "2 months ago"}, {365 * 86400 - 1, "12 months ago"},
        {365 * 86400, "1 year ago"}, {730 * 86400, "2 years ago"}};
    for (const auto& age : cases) {
        CAPTURE(age.elapsed);
        CHECK(format_age(now - age.elapsed, now) == age.expected);
    }
}

} // namespace px::modrim
