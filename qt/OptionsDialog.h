/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QMap>
#include <QString>
#include <QTimer>
#include <QVector>

#include "AddData.h" // AddData
#include "BaseDialog.h"
#include "Macros.h"
#include "Torrent.h" // FileList
#include "ui_OptionsDialog.h"

class Prefs;
class Session;

extern "C"
{
struct tr_variant;
}

class OptionsDialog : public BaseDialog
{
    Q_OBJECT
    TR_DISABLE_COPY_MOVE(OptionsDialog)

public:
    OptionsDialog(Session& session, Prefs const& prefs, AddData addme, QWidget* parent = nullptr);
    ~OptionsDialog() override;

private slots:
    void onAccepted();
    void onPriorityChanged(QSet<int> const& file_indices, int);
    void onWantedChanged(QSet<int> const& file_indices, bool);
    void onVerify();
    void onTimeout();

    void onSourceChanged();
    void onDestinationChanged();

    void onSessionUpdated();

private:
    using mybins_t = QMap<uint32_t, int32_t>;

    void reload();
    void updateWidgetsLocality();
    void clearInfo();
    void clearVerify();

    AddData add_;
    FileList files_;
    QCryptographicHash verify_hash_ = QCryptographicHash(QCryptographicHash::Sha1);

    QDir local_destination_;
    QFile verify_file_;
    QPushButton* verify_button_ = {};
    QTimer edit_timer_;
    QTimer verify_timer_;
    QVector<bool> verify_flags_;
    QVector<bool> wanted_;
    QVector<int> priorities_;
    Session& session_;
    Ui::OptionsDialog ui_ = {};
    mybins_t verify_bins_;
    tr_info info_ = {};
    uint64_t verify_file_pos_ = {};
    uint32_t verify_piece_index_ = {};
    uint32_t verify_piece_pos_ = {};
    int verify_file_index_ = {};
    char verify_buf_[2048 * 4] = {};
    bool have_info_ = {};
    bool is_local_ = {};
};
