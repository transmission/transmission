// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>
#include <string_view>

#include <QByteArray>
#include <QString>
#include <QUrl>

class AddData
{
public:
    // what to do with the source file after adding the torrent
    enum class FilenameDisposal
    {
        NoAction,
        Delete,
        Rename
    };

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

    [[nodiscard]] QByteArray toBase64() const;
    [[nodiscard]] QString readableName() const;
    [[nodiscard]] QString readableShortName() const;

    void disposeSourceFile() const;

    constexpr void setFileDisposal(FilenameDisposal disposal)
    {
        disposal_ = disposal;
    }

    [[nodiscard]] constexpr auto& fileDisposal() const noexcept
    {
        return disposal_;
    }

    static std::optional<AddData> create(QString const& str)
    {
        if (auto ret = AddData{ str }; ret.type != NONE)
        {
            return ret;
        }

        return {};
    }

    // The two forms interop-names.h defines for AddMetainfo, and no others:
    // a link as itself, or a torrent file's contents base64'd.
    // A bare path is not one of them. set() would resolve it against this process's working
    // directory and read whatever it found there. The sender named that file but does not
    // share it, and across a bus need not even own it.
    [[nodiscard]] static std::optional<AddData> fromWireMetainfo(std::string_view metainfo);

    int type = NONE;
    QByteArray metainfo;
    QString filename;
    QString magnet;
    QUrl url;

private:
    std::optional<FilenameDisposal> disposal_;
};
