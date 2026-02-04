// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>

#include <libtransmission/makemeta.h>

#include "BaseDialog.h"
#include "ui_MakeDialog.h"

class QAbstractButton;
class Session;

class MakeDialog : public BaseDialog
{
    Q_OBJECT

public:
    explicit MakeDialog(Session& session, QWidget* parent = nullptr);
    ~MakeDialog() override = default;
    MakeDialog(MakeDialog&&) = delete;
    MakeDialog(MakeDialog const&) = delete;
    MakeDialog& operator=(MakeDialog&&) = delete;
    MakeDialog& operator=(MakeDialog const&) = delete;

protected:
    // QWidget
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void on_source_changed();
    void make_torrent();
    void on_piece_size_updated(int value);

private:
    [[nodiscard]] QString get_source() const;
    void update_pieces_label();
    Session& session_;

    Ui::MakeDialog ui_ = {};

    std::optional<tr_metainfo_builder> builder_;
};
