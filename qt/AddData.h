// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

class AddData
{
public:
    enum
    {
        NONE,
        MAGNET,
        URL,
        FILENAME,
        METAINFO
    };

    AddData() = default;

    explicit AddData(QString const& str)
    {
        set(str);
    }

    int set(QString const&);

    QByteArray toBase64() const;
    QString readableName() const;
    QString readableShortName() const;

    static bool isSupported(QString const& str)
    {
        return AddData(str).type != NONE;
    }

    int type = NONE;
    QByteArray metainfo;
    QString filename;
    QString magnet;
    QUrl url;
};
