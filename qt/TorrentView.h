/*
 * This file Copyright (C) 2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_TORRENT_VIEW_H
#define QTR_TORRENT_VIEW_H

#include <QListView>

class TorrentView: public QListView
{
    Q_OBJECT

  public:
    TorrentView (QWidget * parent = nullptr);

  public slots:
    void setHeaderText (const QString& text);

  signals:
    void headerDoubleClicked ();

  protected:
    virtual void resizeEvent (QResizeEvent * event);

  private:
    class HeaderWidget;

  private:
    void adjustHeaderPosition ();

  private:
    HeaderWidget * const myHeaderWidget;
};

#endif // QTR_TORRENT_VIEW_H
