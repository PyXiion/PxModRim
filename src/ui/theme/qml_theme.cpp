#include "ui/theme/qml_theme.h"

#include <QFile>
#include <QRandomGenerator>
#include <QStringView>
#include <spdlog/spdlog.h>

#include "ui/theme/constants.h"
#include "ui/theme/palette.h"

namespace px::ui {

namespace {

const QString kLogoDir = QStringLiteral(":/qt/qml/PxModRim/assets/");

// Reads the four embedded logo variants and encodes each as an SVG data URI.
QStringList loadLogoVariants() {
    QStringList uris;
    for (int n = 1; n <= 4; ++n) {
        const QString path = kLogoDir + QStringLiteral("logo_nobg_%1.svg").arg(n);
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            spdlog::error("missing embedded logo resource {}", path.toStdString());
            continue;
        }
        uris.append(QStringLiteral("data:image/svg+xml;base64,") +
                    QString::fromLatin1(file.readAll().toBase64()));
    }
    return uris;
}

}  // namespace

Theme::Theme(QObject* parent) : QObject(parent), logo_variants_(loadLogoVariants()) {
    if (!logo_variants_.isEmpty()) {
        const int pick = QRandomGenerator::global()->bounded(static_cast<int>(logo_variants_.size()));
        logo_data_uri_ = logo_variants_.at(pick);
    }
}

QString Theme::elevate0() const { return paletteValue(u"ELEVATE_0"); }
QString Theme::elevate1() const { return paletteValue(u"ELEVATE_1"); }
QString Theme::elevate2() const { return paletteValue(u"ELEVATE_2"); }
QString Theme::elevate3() const { return paletteValue(u"ELEVATE_3"); }
QString Theme::elevate4() const { return paletteValue(u"ELEVATE_4"); }
QString Theme::border() const { return paletteValue(u"BORDER"); }

QString Theme::textMain() const { return paletteValue(u"TEXT_MAIN"); }
QString Theme::textMuted() const { return paletteValue(u"TEXT_MUTED"); }
QString Theme::textDim() const { return paletteValue(u"TEXT_DIM"); }
QString Theme::onAccent() const { return paletteValue(u"TEXT_ON_ACCENT"); }
QString Theme::neutral() const { return paletteValue(u"NEUTRAL"); }

QString Theme::primary() const { return paletteValue(u"PRIMARY"); }
QString Theme::primaryHover() const { return paletteValue(u"PRIMARY_HOVER"); }
QString Theme::primaryBg() const { return paletteValue(u"PRIMARY_BG_QML"); }
QString Theme::success() const { return paletteValue(u"SUCCESS"); }
QString Theme::successHover() const { return paletteValue(u"SUCCESS_HOVER"); }
QString Theme::successBg() const { return paletteValue(u"SUCCESS_BG_QML"); }
QString Theme::warning() const { return paletteValue(u"WARNING"); }
QString Theme::warningBg() const { return paletteValue(u"WARNING_BG_QML"); }
QString Theme::danger() const { return paletteValue(u"DANGER"); }
QString Theme::dangerHover() const { return paletteValue(u"DANGER_HOVER"); }
QString Theme::dangerBg() const { return paletteValue(u"DANGER_BG_QML"); }

QString Theme::fontFamily() const { return QStringLiteral("Source Sans 3, Liberation Sans, sans-serif"); }
QString Theme::fontMono() const { return QStringLiteral("DejaVu Sans Mono, Liberation Mono, monospace"); }
int Theme::fontSizeXs() const { return 10; }
int Theme::fontSizeSm() const { return 11; }
int Theme::fontSizeMd() const { return 13; }
int Theme::fontSizeLg() const { return 14; }
int Theme::fontSizeXl() const { return 18; }
int Theme::radiusXs() const { return 3; }
int Theme::radiusSm() const { return 4; }
int Theme::radiusMd() const { return 6; }
int Theme::radiusLg() const { return 8; }
int Theme::radiusPill() const { return 999; }
int Theme::scrollbarWidth() const { return 6; }
int Theme::tooltipDelay() const { return TOOLTIP_DELAY_MS; }
double Theme::disabledOpacity() const { return 0.45; }
QString Theme::overlay() const { return QStringLiteral("#8c000000"); }
int Theme::railMinWidth() const { return RAIL_MIN_WIDTH; }
int Theme::railMaxWidth() const { return RAIL_MAX_WIDTH; }
int Theme::railCollapseWidth() const { return RAIL_COLLAPSE_WIDTH; }
int Theme::sidebarWidth() const { return SIDEBAR_WIDTH; }

QStringList Theme::logoVariants() const { return logo_variants_; }
QString Theme::logoFileDataUri() const { return logo_data_uri_; }

}  // namespace px::ui
