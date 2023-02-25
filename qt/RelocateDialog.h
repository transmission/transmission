// This file Copyright © 2009-2023 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <libtransmission/tr-macros.h>

#include "BaseDialog.h"
#include "Typedefs.h"
#include "ui_RelocateDialog.h"

class Session;
class TorrentModel;

class RelocateDialog : public BaseDialog
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(RelocateDialog)

public:
    RelocateDialog(Session&, TorrentModel const&, torrent_ids_t ids, QWidget* parent = nullptr);

private slots:
    void onSetLocation();
    void onMoveToggled(bool) const;

private:
    QString newLocation() const;

    Session& session_;
    torrent_ids_t const ids_;

    Ui::RelocateDialog ui_ = {};

    static bool move_flag;
};
