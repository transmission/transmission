/*
 * This file Copyright (C) 2012-2016 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <QDir>
#include <QFile>

#include <libtransmission/crypto-utils.h> // tr_base64_encode()
#include <libtransmission/transmission.h>

#include "AddData.h"
#include "Utils.h"

namespace
{

QString getNameFromMetainfo(QByteArray const& metainfo)
{
    QString name;

    tr_ctor* ctor = tr_ctorNew(nullptr);
    tr_ctorSetMetainfo(ctor, metainfo.constData(), metainfo.size());

    tr_info inf;
    if (tr_torrentParse(ctor, &inf) == TR_PARSE_OK)
    {
        name = QString::fromUtf8(inf.name); // metainfo is required to be UTF-8
        tr_metainfoFree(&inf);
    }

    tr_ctorFree(ctor);
    return name;
}

} // anonymous namespace

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
        size_t len;
        void* raw = tr_base64_decode(key.toUtf8().constData(), key.toUtf8().size(), &len);

        if (raw != nullptr)
        {
            metainfo.append(static_cast<char const*>(raw), int(len));
            tr_free(raw);
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
    QByteArray ret;

    if (!metainfo.isEmpty())
    {
        size_t len;
        void* b64 = tr_base64_encode(metainfo.constData(), metainfo.size(), &len);
        ret = QByteArray(static_cast<char const*>(b64), int(len));
        tr_free(b64);
    }

    return ret;
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
