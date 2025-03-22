// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <optional>
#include <vector>

#include <QDir>
#include <QFile>
#include <QString>
#include <QTimer>

#include "AddData.h" // AddData
#include "BaseDialog.h"
#include "Torrent.h" // FileList
#include "Typedefs.h" // file_indices_t
#include "ui_OptionsDialog.h"

#include <libtransmission/transmission.h>

#include <libtransmission/torrent-metainfo.h>

class Prefs;
class Session;

extern "C"
{
    struct tr_variant;
}

class OptionsDialog : public BaseDialog
{
    Q_OBJECT

public:
    OptionsDialog(Session& session, Prefs const& prefs, AddData addme, QWidget* parent = nullptr);
    OptionsDialog& operator=(OptionsDialog&&) = delete;
    OptionsDialog& operator=(OptionsDialog const&) = delete;
    OptionsDialog(OptionsDialog&&) = delete;
    OptionsDialog(OptionsDialog const&) = delete;
    ~OptionsDialog() override;

private slots:
    void onAccepted();
    void onPriorityChanged(file_indices_t const& file_indices, int);
    void onWantedChanged(file_indices_t const& file_indices, bool);

    void onSourceChanged();
    void onDestinationChanged();

    void onSessionUpdated();

private:
    void reload();
    void updateWidgetsLocality();
    void clearInfo();

    AddData add_;
    FileList files_;

    QDir local_destination_;
    QTimer edit_timer_;
    std::vector<bool> wanted_;
    std::vector<int> priorities_;
    Session& session_;
    Ui::OptionsDialog ui_ = {};
    std::optional<tr_torrent_metainfo> metainfo_;
    bool is_local_ = {};
};
