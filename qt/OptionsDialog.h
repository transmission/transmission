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

public:
    OptionsDialog(Session& session, Prefs const& prefs, AddData const& addme, QWidget* parent = nullptr);
    virtual ~OptionsDialog();

private:
    using mybins_t = QMap<uint32_t, int32_t>;

private:
    void reload();
    void updateWidgetsLocality();
    void clearInfo();
    void clearVerify();

private slots:
    void onAccepted();
    void onPriorityChanged(QSet<int> const& fileIndices, int);
    void onWantedChanged(QSet<int> const& fileIndices, bool);
    void onVerify();
    void onTimeout();

    void onSourceChanged();
    void onDestinationChanged();

    void onSessionUpdated();

private:
    Session& session_;
    AddData add_;

    Ui::OptionsDialog ui;

    bool is_local_;
    QDir local_destination_;
    bool have_info_;
    tr_info info_;
    QPushButton* verify_button_;
    QVector<int> priorities_;
    QVector<bool> wanted_;
    FileList files_;

    QTimer verify_timer_;
    char verify_buf_[2048 * 4];
    QFile verify_file_;
    uint64_t verify_file_pos_;
    int verify_file_index_;
    uint32_t verify_piece_index_;
    uint32_t verify_piece_pos_;
    QVector<bool> verify_flags_;
    QCryptographicHash verify_hash_;
    mybins_t verify_bins_;
    QTimer edit_timer_;
};
