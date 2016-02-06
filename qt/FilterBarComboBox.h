/*
 * This file Copyright (C) 2010-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_FILTER_BAR_COMBO_BOX_H
#define QTR_FILTER_BAR_COMBO_BOX_H

#include <QComboBox>

class FilterBarComboBox: public QComboBox
{
    Q_OBJECT

  public:
    enum
    {
      CountRole = Qt::UserRole + 1,
      CountStringRole,
      UserRole
    };

  public:
    FilterBarComboBox (QWidget * parent = nullptr);

    int currentCount () const;

    // QWidget
    QSize minimumSizeHint () const override;
    QSize sizeHint () const override;

  protected:
    // QWidget
    void paintEvent (QPaintEvent * e) override;

  private:
    QSize calculateSize (const QSize& textSize, const QSize& countSize) const;
};

#endif // QTR_FILTER_BAR_COMBO_BOX_H
