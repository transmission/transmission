// This file Copyright © 2012-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <QDir>
#include <QFile>

#include <libtransmission/transmission.h>

#include <libtransmission/error.h>
#include <libtransmission/torrent-metainfo.h>
#include <libtransmission/utils.h>
#include <libtransmission/web-utils.h>

#include "AddData.h"
#include "Utils.h"

namespace
{

QString getNameFromMetainfo(QByteArray const& benc)
{
    auto metainfo = tr_torrent_metainfo{};
    if (!metainfo.parseBenc({ benc.constData(), static_cast<size_t>(benc.size()) }))
    {
        return {};
    }

    return QString::fromStdString(metainfo.name());
}

QString getNameFromMagnet(QString const& magnet)
{
    auto tmp = tr_magnet_metainfo{};

    if (!tmp.parseMagnet(magnet.toStdString()))
    {
        return magnet;
    }

    if (!std::empty(tmp.name()))
    {
        return QString::fromStdString(tmp.name());
    }

    return QString::fromStdString(tmp.infoHashString());
}

} // namespace

int AddData::set(QString const& key)
{
    if (auto const key_std = key.toStdString(); tr_urlIsValid(key_std))
    {
        this->url = key;
        type = URL;
    }
    else if (QFile(key).exists())
    {
        this->filename = QDir::fromNativeSeparators(key);
        type = FILENAME;

        auto file = QFile{ key };
        file.open(QIODevice::ReadOnly);
        this->metainfo = file.readAll();
        file.close();
    }
    else if (tr_magnet_metainfo{}.parseMagnet(key_std))
    {
        this->magnet = key;
        this->type = MAGNET;
    }
    else if (auto const raw = QByteArray::fromBase64(key.toUtf8()); !raw.isEmpty())
    {
        this->metainfo.append(raw);
        this->type = METAINFO;
    }
    else
    {
        this->type = NONE;
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
        return getNameFromMagnet(magnet);

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

void AddData::disposeSourceFile() const
{
    auto file = QFile{ filename };
    if (!disposal_ || !file.exists())
    {
        return;
    }

    switch (*disposal_)
    {
    case FilenameDisposal::Delete:
        file.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        file.remove();
        break;

    case FilenameDisposal::Rename:
        file.rename(QStringLiteral("%1.added").arg(filename));
        break;

    default:
        // no action
        break;
    }
}
