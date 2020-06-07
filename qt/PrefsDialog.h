/*
 * This file Copyright (C) 2009-2015 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#pragma once

#include <QMap>

#include "BaseDialog.h"
#include "Prefs.h"

#include "ui_PrefsDialog.h"

class QHttp;
class QMessageBox;
class QString;

class Prefs;
class Session;

struct FilterData
{
    QString name;
    QString expression;
    QString path;
};

class FilterDataModel : public QAbstractTableModel
{
public:
    FilterDataModel(QObject* parent = {});

    int rowCount(const QModelIndex&) const override;
    int columnCount(const QModelIndex&) const override;

    bool setData(const QModelIndex& index, const QVariant& value, int role) override;

    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    Qt::ItemFlags flags(const QModelIndex &index) const override;

    void append(const FilterData& filter);

    void removeRow(int row);

    const FilterData& getLastElement() const;

    QList<FilterData> getData() const;

private:
   QList<FilterData> data_;
};

class PrefsDialog : public BaseDialog
{
    Q_OBJECT

public:
    PrefsDialog(Session&, Prefs&, QWidget* parent = nullptr);
    virtual ~PrefsDialog();

    void saveModel();

private:
    using key2widget_t = QMap<int, QWidget*>;

private:
    bool updateWidgetValue(QWidget* widget, int pref_key);
    void linkWidgetToPref(QWidget* widget, int pref_key);
    void updateBlocklistLabel();
    void updateDownloadingWidgetsLocality();

    void setPref(int key, QVariant const& v);

    void initFilterDataModel();
    void toggleDynamicTableGroup(bool state);

    void initDownloadingTab();
    void initSeedingTab();
    void initSpeedTab();
    void initPrivacyTab();
    void initNetworkTab();
    void initDesktopTab();
    void initRemoteTab();

private slots:
    void checkBoxToggled(bool checked);
    void addDynamicDirButtonClicked();
    void removeDynamicDirButtonClicked();
    void spinBoxEditingFinished();
    void timeEditingFinished();
    void lineEditingFinished();
    void pathChanged(QString const& path);
    void refreshPref(int key);
    void encryptionEdited(int);
    void altSpeedDaysEdited(int);
    void sessionUpdated();
    void onPortTested(bool);
    void onPortTest();
    void onIdleLimitChanged();
    void onQueueStalledMinutesChanged();

    void onUpdateBlocklistClicked();
    void onUpdateBlocklistCancelled();
    void onBlocklistDialogDestroyed(QObject*);
    void onBlocklistUpdated(int n);

private:
    Session& session_;
    Prefs& prefs_;

    Ui::PrefsDialog ui_ = {};

    bool const is_server_;
    bool is_local_ = {};

    key2widget_t widgets_;
    QWidgetList web_widgets_;
    QWidgetList web_auth_widgets_;
    QWidgetList web_whitelist_widgets_;
    QWidgetList proxy_widgets_;
    QWidgetList proxy_auth_widgets_;
    QWidgetList sched_widgets_;
    QWidgetList block_widgets_;
    QWidgetList unsupported_when_remote_;

    int blocklist_http_tag_ = {};
    QHttp* blocklist_http_ = {};
    QMessageBox* blocklist_dialog_ = {};

    FilterDataModel filter_data_model_;
};
