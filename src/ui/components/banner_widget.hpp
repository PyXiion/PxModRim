#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

#include "ui/theme/constants.hpp"

class QPaintEvent;
class QResizeEvent;

namespace px::modrim {

// Fixed 16:9 banner (capped at max height) that crops the pixmap to fill it,
// with an optional title and subtitle over a gradient at the bottom.
class AspectRatioBanner : public QWidget {
  Q_OBJECT

 public:
  explicit AspectRatioBanner(QWidget* parent = nullptr,
                             int maxHeight = kBannerMaxHeight);

  void setPixmap(const QPixmap& pixmap);
  void setTitle(const QString& title);
  void setSubtitle(const QString& subtitle);
  void setShowOverlay(bool show);

  bool hasHeightForWidth() const override;
  int heightForWidth(int width) const override;
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void recomputeScaledPixmap();

  QPixmap m_pixmap;
  QPixmap m_scaledPixmap;
  QString m_title;
  QString m_subtitle;
  int m_maxHeight;
  bool m_showOverlay = true;
};

}  // namespace px::modrim
