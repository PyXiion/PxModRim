#include <QLatin1Char>
#include <QString>
#include <initializer_list>

#include <catch2/catch_test_macros.hpp>

#include "ui/components/unity_rich_text.hpp"

namespace {

struct Case {
  const char* input;
  const char* expected;
};

// Checks every case and reports each failing input, rather than stopping at the
// first.
void checkCases(std::initializer_list<Case> cases) {
  for (const Case& c : cases) {
    INFO("input: " << c.input);
    CHECK(px::modrim::unityRichTextToHtml(QString::fromUtf8(c.input)) ==
          QString::fromUtf8(c.expected));
  }
}

}  // namespace

TEST_CASE("unity rich text passes plain text through") {
  checkCases({{"Hello world", "Hello world"}});
}

TEST_CASE("unity rich text maps bold tags") {
  checkCases({
      {"<b>bold</b>", "<b>bold</b>"},
      {"<bold>bold</bold>", "<b>bold</b>"},
  });
}

TEST_CASE("unity rich text maps italic tags") {
  checkCases({
      {"<i>italic</i>", "<i>italic</i>"},
      {"<italic>italic</italic>", "<i>italic</i>"},
  });
}

TEST_CASE("unity rich text maps underline to a span") {
  checkCases(
      {{"<u>underline</u>",
        "<span style=\"text-decoration: underline;\">underline</span>"}});
}

TEST_CASE("unity rich text maps strikethrough") {
  checkCases({{"<s>strike</s>", "<s>strike</s>"}});
}

TEST_CASE("unity rich text maps color names and hex values to font color") {
  checkCases({
      {"<color=red>red</color>", "<font color=\"#ff0000\">red</font>"},
      {"<color=#ff0000>red</color>", "<font color=\"#ff0000\">red</font>"},
  });
}

TEST_CASE("unity rich text maps size units to font-size points") {
  checkCases({
      {"<size=24>big</size>", "<span style=\"font-size: 24pt;\">big</span>"},
      {"<size=200%>big</size>", "<span style=\"font-size: 24pt;\">big</span>"},
      {"<size=1.5em>big</size>", "<span style=\"font-size: 18pt;\">big</span>"},
      {"<size=+5>relative</size>",
       "<span style=\"font-size: 17pt;\">relative</span>"},
      {"<size=-2>small</size>",
       "<span style=\"font-size: 10pt;\">small</span>"},
  });
}

TEST_CASE("unity rich text maps align to a text-align div") {
  checkCases({{"<align=\"center\">center</align>",
               "<div style=\"text-align: center;\">center</div>"}});
}

TEST_CASE("unity rich text maps indent to a margin-left div") {
  checkCases({{"<indent=10%>indented</indent>",
               "<div style=\"margin-left: 40px;\">indented</div>"}});
}

TEST_CASE("unity rich text maps mark to a background-colored mark") {
  checkCases({{"<mark=#ffff00>marked</mark>",
               "<mark style=\"background-color: #ffff00;\">marked</mark>"}});
}

TEST_CASE("unity rich text keeps sub and sup") {
  checkCases({
      {"<sub>sub</sub>", "<sub>sub</sub>"},
      {"<sup>sup</sup>", "<sup>sup</sup>"},
  });
}

TEST_CASE("unity rich text maps http and https links to anchors") {
  checkCases({{"<link=\"http://example.com\">link</link>",
               "<a href=\"http://example.com\">link</a>"}});
}

TEST_CASE("unity rich text drops links with unsafe urls and keeps their text") {
  for (const char* url : {
           "javascript:alert(1)",
           "steam://rungameid/294100",
           "https:example.com",
       }) {
    INFO("url: " << url);
    const QString input =
        QStringLiteral("<link=\"%1\">link</link>").arg(QString::fromUtf8(url));
    CHECK(px::modrim::unityRichTextToHtml(input) == QStringLiteral("link"));
  }
}

TEST_CASE("unity rich text uses the default size for a non-finite size") {
  // 10**400 as a decimal literal: a 1 followed by 400 zeros, which parses to
  // infinity.
  const QString huge = QStringLiteral("1") + QString(400, QLatin1Char('0'));
  const QString html = px::modrim::unityRichTextToHtml(
      QStringLiteral("<size=%1>large</size>").arg(huge));
  CHECK(html ==
        QStringLiteral("<span style=\"font-size: 13pt;\">large</span>"));
}

TEST_CASE("unity rich text uses the default indent for a non-finite indent") {
  // 10**400 as a decimal literal: a 1 followed by 400 zeros, which parses to
  // infinity.
  const QString huge = QStringLiteral("1") + QString(400, QLatin1Char('0'));
  const QString html = px::modrim::unityRichTextToHtml(
      QStringLiteral("<indent=%1>wide</indent>").arg(huge));
  CHECK(html == QStringLiteral("<div style=\"margin-left: 20px;\">wide</div>"));
}

TEST_CASE("unity rich text nests formatting tags") {
  checkCases({
      {"<b><i>bold italic</i></b>", "<b><i>bold italic</i></b>"},
      {"<b><color=#00ff00>green</color></b>",
       "<b><font color=\"#00ff00\">green</font></b>"},
  });
}

TEST_CASE("unity rich text strips unknown tags") {
  checkCases({{"<foo>bar</foo>", "bar"}});
}
