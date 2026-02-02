// This file Copyright © Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#pragma once

#include <glibmm/refptr.h>
#include <gtkmm/builder.h>
#include <gtkmm/dialog.h>
#include <gtkmm/entry.h>
#include <gtkmm/filechoosernative.h>
#include <gtkmm/window.h>

#include <memory>

class Session;

struct tr_ctor;

class TorrentUrlChooserDialog : public Gtk::Dialog
{
public:
    TorrentUrlChooserDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core);
    TorrentUrlChooserDialog(TorrentUrlChooserDialog&&) = delete;
    TorrentUrlChooserDialog(TorrentUrlChooserDialog const&) = delete;
    TorrentUrlChooserDialog& operator=(TorrentUrlChooserDialog&&) = delete;
    TorrentUrlChooserDialog& operator=(TorrentUrlChooserDialog const&) = delete;
    ~TorrentUrlChooserDialog() override = default;

    static std::unique_ptr<TorrentUrlChooserDialog> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

private:
    void onOpenURLResponse(int response, Gtk::Entry const& entry, Glib::RefPtr<Session> const& core);
};

class TorrentFileChooserDialog : public Gtk::FileChooserNative
{
public:
    TorrentFileChooserDialog(TorrentFileChooserDialog&&) = delete;
    TorrentFileChooserDialog(TorrentFileChooserDialog const&) = delete;
    TorrentFileChooserDialog& operator=(TorrentFileChooserDialog&&) = delete;
    TorrentFileChooserDialog& operator=(TorrentFileChooserDialog const&) = delete;
    ~TorrentFileChooserDialog() override = default;

    static std::unique_ptr<TorrentFileChooserDialog> create(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

protected:
    TorrentFileChooserDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core);

private:
    void onOpenDialogResponse(int response, Glib::RefPtr<Session> const& core);
};

class OptionsDialog : public Gtk::Dialog
{
public:
    OptionsDialog(
        BaseObjectType* cast_item,
        Glib::RefPtr<Gtk::Builder> const& builder,
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core,
        std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor);
    OptionsDialog(OptionsDialog&&) = delete;
    OptionsDialog(OptionsDialog const&) = delete;
    OptionsDialog& operator=(OptionsDialog&&) = delete;
    OptionsDialog& operator=(OptionsDialog const&) = delete;
    ~OptionsDialog() override;

    static std::unique_ptr<OptionsDialog> create(
        Gtk::Window& parent,
        Glib::RefPtr<Session> const& core,
        std::unique_ptr<tr_ctor, void (*)(tr_ctor*)> ctor);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
