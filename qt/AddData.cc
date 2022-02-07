// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QDir>
#include <QFile>

#include <libtransmission/transmission.h>

#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/utils.h>
#include <libtransmission/error.h>

#include "AddData.h"
#include "Utils.h"

namespace
{

QString getNameFromMetainfo(QByteArray const& benc)
{
    auto metainfo = tr_torrent_metainfo{};
    if (!metainfo.parseBenc({ benc.constData(), size_t(benc.size()) }))
    {
        return {};
    }

    return QString::fromStdString(metainfo.name());
}

} // namespace

int AddData::set(QString const& key)
{
    if (Utils::isMagnetLink(key))
    {
        magnet = key;
        type = MAGNET;
    }
    else if (Utils::isUriWithSupportedScheme(key))
    {
        url = key;
        type = URL;
    }
    else if (QFile(key).exists())
    {
        filename = QDir::fromNativeSeparators(key);
        type = FILENAME;

        QFile file(key);
        file.open(QIODevice::ReadOnly);
        metainfo = file.readAll();
        file.close();
    }
    else if (Utils::isHexHashcode(key))
    {
        magnet = QStringLiteral("magnet:?xt=urn:btih:") + key;
        type = MAGNET;
    }
    else
    {
        auto raw = QByteArray::fromBase64(key.toUtf8());
        if (!raw.isEmpty())
        {
            metainfo.append(raw);
            type = METAINFO;
        }
        else
        {
            type = NONE;
        }
    }

    return type;
}

QByteArray AddData::toBase64() const
{
    return metainfo.toBase64();
}

QString AddData::readableName() const
{
    switch (type)
    {
    case FILENAME:
        return filename;

    case MAGNET:
        return magnet;

    case URL:
        return url.toString();

    case METAINFO:
        return getNameFromMetainfo(metainfo);

    default: // NONE
        return {};
    }
}

QString AddData::readableShortName() const
{
    switch (type)
    {
    case FILENAME:
        return QFileInfo(filename).baseName();

    case URL:
        return url.path().split(QLatin1Char('/')).last();

    default:
        return readableName();
    }
}
