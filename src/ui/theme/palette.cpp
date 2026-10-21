#include "ui/theme/palette.hpp"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QRegularExpression>
#include <QStringView>

#include <spdlog/spdlog.h>

namespace px::modrim {

namespace {

struct PaletteEntry {
  QLatin1StringView name;
  QLatin1StringView value;
};
constexpr PaletteEntry kPaletteEntries[]{
#include "ui/theme/palette_table.inc"
};

// Mirrors the legacy string.Template.safe_substitute: a `$NAME` token is
// replaced only when NAME is a palette key; anything else stays as written.
QString expandTokens(const QString& raw) {
  static const QRegularExpression token(
      QStringLiteral(R"(\$([A-Z_][A-Z0-9_]*))"));
  QString out;
  out.reserve(raw.size());
  qsizetype last = 0;
  auto it = token.globalMatch(raw);
  while (it.hasNext()) {
    const auto match = it.next();
    out.append(QStringView(raw).mid(last, match.capturedStart() - last));
    const QString value = paletteValue(match.capturedView(1));
    out.append(value.isEmpty() ? match.captured(0) : value);
    last = match.capturedEnd();
  }
  out.append(QStringView(raw).mid(last));
  return out;
}

}  // namespace

QString paletteValue(QStringView name) {
  for (const auto& entry : kPaletteEntries) {
    if (entry.name == name) {
      return QString(entry.value);
    }
  }
  return {};
}

QColor paletteColor(QStringView name) {
  const QString val = paletteValue(name);
  return val.isEmpty() ? QColor{} : QColor(val);
}

const QColor& panelBgQ() {
  static const QColor color = paletteColor(u"ELEVATE_2");
  return color;
}

const QColor& textMainQ() {
  static const QColor color = paletteColor(u"TEXT_MAIN");
  return color;
}

const QColor& textMutedQ() {
  static const QColor color = paletteColor(u"TEXT_MUTED");
  return color;
}

QString getStylesheet() {
  QFile file(QStringLiteral(":/qt/qml/PxModRim/theme/style.qss"));
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    spdlog::warn("failed to open the application stylesheet resource");
    return {};
  }
  return expandTokens(QString::fromUtf8(file.readAll()));
}

void applyAppTheme(QApplication& app) {
  app.setStyle(QStringLiteral("Fusion"));

  QPalette palette;
  palette.setColor(QPalette::Window, paletteColor(u"ELEVATE_0"));
  palette.setColor(QPalette::WindowText, paletteColor(u"TEXT_MAIN"));
  palette.setColor(QPalette::Base, paletteColor(u"ELEVATE_1"));
  palette.setColor(QPalette::AlternateBase, paletteColor(u"ELEVATE_2"));
  palette.setColor(QPalette::Text, paletteColor(u"TEXT_MAIN"));
  palette.setColor(QPalette::Button, paletteColor(u"ELEVATE_2"));
  palette.setColor(QPalette::ButtonText, paletteColor(u"TEXT_MAIN"));
  palette.setColor(QPalette::BrightText, paletteColor(u"TEXT_MAIN"));
  palette.setColor(QPalette::Highlight, paletteColor(u"PRIMARY"));
  palette.setColor(QPalette::HighlightedText, paletteColor(u"ELEVATE_0"));
  palette.setColor(QPalette::Link, paletteColor(u"PRIMARY"));
  palette.setColor(QPalette::ToolTipBase, paletteColor(u"ELEVATE_2"));
  palette.setColor(QPalette::ToolTipText, paletteColor(u"TEXT_MAIN"));
  app.setPalette(palette);

  const QString qss = getStylesheet();
  if (!qss.isEmpty()) {
    app.setStyleSheet(qss);
  }
}

}  // namespace px::modrim
