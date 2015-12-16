/*
 * This file Copyright (C) 2012-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 * $Id$
 */

#ifndef QTR_INTEROP_OBJECT_H
#define QTR_INTEROP_OBJECT_H

#include <QObject>

class InteropObject: public QObject
{
    Q_OBJECT
    Q_CLASSINFO ("D-Bus Interface", "com.transmissionbt.Transmission")

  public:
    InteropObject (QObject * parent = nullptr);

  public slots:
    bool PresentWindow ();
    bool AddMetainfo (const QString& metainfo);
};

#endif // QTR_INTEROP_OBJECT_H
