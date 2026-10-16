#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

namespace px::modrim {

// Design tokens for QML, exposed as the `Theme` root context property (see
// createQmlEngine). Color values come from the same PALETTE as the stylesheet;
// *Bg values are #AARRGGBB because QML rejects rgba() strings.
class Theme : public QObject {
  Q_OBJECT

  Q_PROPERTY(QString elevate0 READ elevate0 CONSTANT)
  Q_PROPERTY(QString elevate1 READ elevate1 CONSTANT)
  Q_PROPERTY(QString elevate2 READ elevate2 CONSTANT)
  Q_PROPERTY(QString elevate3 READ elevate3 CONSTANT)
  Q_PROPERTY(QString elevate4 READ elevate4 CONSTANT)
  Q_PROPERTY(QString border READ border CONSTANT)

  Q_PROPERTY(QString textMain READ textMain CONSTANT)
  Q_PROPERTY(QString textMuted READ textMuted CONSTANT)
  Q_PROPERTY(QString textDim READ textDim CONSTANT)
  Q_PROPERTY(QString onAccent READ onAccent CONSTANT)
  Q_PROPERTY(QString neutral READ neutral CONSTANT)

  Q_PROPERTY(QString primary READ primary CONSTANT)
  Q_PROPERTY(QString primaryHover READ primaryHover CONSTANT)
  Q_PROPERTY(QString primaryBg READ primaryBg CONSTANT)
  Q_PROPERTY(QString success READ success CONSTANT)
  Q_PROPERTY(QString successHover READ successHover CONSTANT)
  Q_PROPERTY(QString successBg READ successBg CONSTANT)
  Q_PROPERTY(QString warning READ warning CONSTANT)
  Q_PROPERTY(QString warningBg READ warningBg CONSTANT)
  Q_PROPERTY(QString danger READ danger CONSTANT)
  Q_PROPERTY(QString dangerHover READ dangerHover CONSTANT)
  Q_PROPERTY(QString dangerBg READ dangerBg CONSTANT)

  Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
  Q_PROPERTY(QString fontMono READ fontMono CONSTANT)
  Q_PROPERTY(int fontSizeXs READ fontSizeXs CONSTANT)
  Q_PROPERTY(int fontSizeSm READ fontSizeSm CONSTANT)
  Q_PROPERTY(int fontSizeMd READ fontSizeMd CONSTANT)
  Q_PROPERTY(int fontSizeLg READ fontSizeLg CONSTANT)
  Q_PROPERTY(int fontSizeXl READ fontSizeXl CONSTANT)
  Q_PROPERTY(int radiusXs READ radiusXs CONSTANT)
  Q_PROPERTY(int radiusSm READ radiusSm CONSTANT)
  Q_PROPERTY(int radiusMd READ radiusMd CONSTANT)
  Q_PROPERTY(int radiusLg READ radiusLg CONSTANT)
  Q_PROPERTY(int radiusPill READ radiusPill CONSTANT)
  Q_PROPERTY(int scrollbarWidth READ scrollbarWidth CONSTANT)
  Q_PROPERTY(int tooltipDelay READ tooltipDelay CONSTANT)
  Q_PROPERTY(double disabledOpacity READ disabledOpacity CONSTANT)
  Q_PROPERTY(QString overlay READ overlay CONSTANT)
  Q_PROPERTY(int railMinWidth READ railMinWidth CONSTANT)
  Q_PROPERTY(int railMaxWidth READ railMaxWidth CONSTANT)
  Q_PROPERTY(int railCollapseWidth READ railCollapseWidth CONSTANT)
  Q_PROPERTY(int sidebarWidth READ sidebarWidth CONSTANT)

  Q_PROPERTY(QStringList logoVariants READ logoVariants CONSTANT)
  Q_PROPERTY(QString logoFileDataUri READ logoFileDataUri CONSTANT)

 public:
  // Picks one of the four logo variants at random, as the legacy Theme did.
  explicit Theme(QObject* parent = nullptr);

  QString elevate0() const;
  QString elevate1() const;
  QString elevate2() const;
  QString elevate3() const;
  QString elevate4() const;
  QString border() const;

  QString textMain() const;
  QString textMuted() const;
  QString textDim() const;
  QString onAccent() const;
  QString neutral() const;

  QString primary() const;
  QString primaryHover() const;
  QString primaryBg() const;
  QString success() const;
  QString successHover() const;
  QString successBg() const;
  QString warning() const;
  QString warningBg() const;
  QString danger() const;
  QString dangerHover() const;
  QString dangerBg() const;

  QString fontFamily() const;
  QString fontMono() const;
  int fontSizeXs() const;
  int fontSizeSm() const;
  int fontSizeMd() const;
  int fontSizeLg() const;
  int fontSizeXl() const;
  int radiusXs() const;
  int radiusSm() const;
  int radiusMd() const;
  int radiusLg() const;
  int radiusPill() const;
  int scrollbarWidth() const;
  int tooltipDelay() const;
  double disabledOpacity() const;
  QString overlay() const;
  int railMinWidth() const;
  int railMaxWidth() const;
  int railCollapseWidth() const;
  int sidebarWidth() const;

  QStringList logoVariants() const;
  QString logoFileDataUri() const;

 private:
  QStringList m_logoVariants;
  QString m_logoDataUri;
};

}  // namespace px::modrim
