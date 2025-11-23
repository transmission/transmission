// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QApplication>
#include <QString>
#include <QStyle>

namespace icons
{

enum class Type
{
    AddTracker,
    EditTrackers,
    RemoveTracker,

    AddTorrentFromFile,
    AddTorrentFromURL,
    CreateNewTorrent,
    OpenTorrentDetails,
    OpenTorrentLocalFolder,

    StartTorrent,
    StartTorrentNow,
    PauseTorrent,
    RemoveTorrent,
    RemoveTorrentAndDeleteData,
    SetTorrentLocation,
    CopyMagnetLinkToClipboard,
    VerifyTorrent,

    TorrentErrorEmblem,

    SelectAll,
    DeselectAll,

    Statistics,
    Settings,
    QuitApp,
    Donate,
    About,
    Help,

    QueueMoveTop,
    QueueMoveUp,
    QueueMoveDown,
    QueueMoveBottom,

    NetworkIdle,
    NetworkReceive,
    NetworkTransmit,
    NetworkTransmitReceive,
    NetworkError,

    TorrentStateActive,
    TorrentStateSeeding,
    TorrentStateDownloading,
    TorrentStatePaused,
    TorrentStateVerifying,
    TorrentStateError
};

[[nodiscard]] QIcon icon(Type type, QStyle* style = QApplication::style());

[[nodiscard]] bool shouldBeShownInMenu(Type type);

} // namespace icons
