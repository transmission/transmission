// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <QString>

#include "BaseDialog.h"
#include "Typedefs.h"

class QDialogButtonBox;
class QLineEdit;
class Session;
class TorrentModel;

class RenameTorrentDialog : public BaseDialog
{
    Q_OBJECT

public:
    RenameTorrentDialog(Session& session, TorrentModel const& model, torrent_ids_t ids, QWidget* parent = nullptr);
    ~RenameTorrentDialog() override = default;
    RenameTorrentDialog(RenameTorrentDialog&&) = delete;
    RenameTorrentDialog(RenameTorrentDialog const&) = delete;
    RenameTorrentDialog& operator=(RenameTorrentDialog&&) = delete;
    RenameTorrentDialog& operator=(RenameTorrentDialog const&) = delete;

private:
    [[nodiscard]] QString newName() const;
    void onAccepted();
    void updateButtons() const;
    void selectBaseName() const;

    Session& session_;
    tr_torrent_id_t torrent_id_ = {};
    QString old_name_;
    QLineEdit* line_edit_ = {};
    QDialogButtonBox* button_box_ = {};
};
