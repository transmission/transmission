/*
 * This file Copyright (C) 2014-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_PATH_BUTTON_H
#define QTR_PATH_BUTTON_H

#include <QToolButton>

class PathButton: public QToolButton
{
    Q_OBJECT

  public:
    enum Mode
    {
      DirectoryMode,
      FileMode
    };

  public:
    PathButton (QWidget * parent = nullptr);

    void setMode (Mode mode);
    void setTitle (const QString& title);
    void setNameFilter (const QString& nameFilter);

    void setPath (const QString& path);
    const QString& path () const;

    // QWidget
    virtual QSize sizeHint () const;

  signals:
    void pathChanged (const QString& path);

  protected:
    // QWidget
    virtual void paintEvent (QPaintEvent * event);

  private:
    void updateAppearance ();

    bool isDirMode () const;
    QString effectiveTitle () const;

  private slots:
    void onClicked ();
    void onFileSelected (const QString& path);

  private:
    Mode myMode;
    QString myTitle;
    QString myNameFilter;
    QString myPath;
};

#endif // QTR_PATH_BUTTON_H
