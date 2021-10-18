/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include "BaseDialog.h"
#include "Macros.h"
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
