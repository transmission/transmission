// This file Copyright © Transmission authors and contributors.
// This file is licensed under the MIT (SPDX: MIT) license,
// A copy of this license can be found in licenses/ .

#pragma once

#include "GtkCompat.h"
#include "Torrent.h"

#include <libtransmission/transmission.h>
#include <libtransmission/favicon-cache.h>
#include <libtransmission/variant.h>

#include <gdkmm/pixbuf.h>
#include <giomm/file.h>
#include <giomm/listmodel.h>
#include <glibmm/object.h>
#include <glibmm/refptr.h>
#include <glibmm/ustring.h>
#include <gtkmm/treemodel.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

class Session : public Glib::Object
{
public:
    Session(Session&&) = delete;
    Session(Session const&) = delete;
    Session& operator=(Session&&) = delete;
    Session& operator=(Session const&) = delete;

    enum ErrorCode : uint16_t
    {
        ERR_ADD_TORRENT_ERR = 1,
        ERR_ADD_TORRENT_DUP = 2,
        ERR_NO_MORE_TORRENTS = 1000 /* finished adding a batch */
    };

    enum PortTestIpProtocol : uint8_t
    {
        PORT_TEST_IPV4,
        PORT_TEST_IPV6,
        NUM_PORT_TEST_IP_PROTOCOL // Must always be the last value
    };

    using Model = IF_GTKMM4(Gio::ListModel, Gtk::TreeModel);

public:
    ~Session() override;

    static Glib::RefPtr<Session> create(tr_session* session);

    tr_session* close();

    Glib::RefPtr<Gio::ListModel> get_model() const;
    Glib::RefPtr<Model> get_sorted_model() const;

    void clear();

    tr_session* get_session() const;

    size_t get_active_torrent_count() const;

    size_t get_torrent_count() const;

    tr_torrent* find_torrent(tr_torrent_id_t id) const;

    FaviconCache<Glib::RefPtr<Gdk::Pixbuf>>& favicon_cache() const;

    /******
    *******
    ******/

    /**
     * Load saved state and return number of torrents added.
     * May trigger one or more "error" signals with ERR_ADD_TORRENT
     */
    void load(bool force_paused);

    /**
     * Add a list of torrents.
     * This function assumes ownership of torrentFiles
     *
     * May pop up dialogs for each torrent if that preference is enabled.
     * May trigger one or more "error" signals with ERR_ADD_TORRENT
     */
    void add_files(std::vector<Glib::RefPtr<Gio::File>> const& files, bool do_start, bool do_prompt, bool do_notify);

    /** @brief Add a torrent from a URL */
    bool add_from_url(Glib::ustring const& url);

    /** @brief Add a torrent.
        @param ctor this function assumes ownership of the ctor */
    void add_ctor(tr_ctor* ctor);

    /** Add a torrent. */
    void add_torrent(Glib::RefPtr<Torrent> const& torrent, bool do_notify);

    /**
     * Notifies listeners that torrents have been added.
     * This should be called after one or more tr_core_add* () calls.
     */
    void torrents_added();

    void torrent_changed(tr_torrent_id_t id);

    /******
    *******
    ******/

    /* remove a torrent */
    void remove_torrent(tr_torrent_id_t id, bool delete_files);

    /* update the model with current torrent status */
    void update();

    /**
     * Attempts to start a torrent immediately.
     */
    void start_now(tr_torrent_id_t id);

    /**
    ***  Set a preference value, save the prefs file, and emit the "prefs-changed" signal
    **/

    void set_pref(tr_quark key, std::string const& val);
    void set_pref(tr_quark key, bool val);
    void set_pref(tr_quark key, int val);
    void set_pref(tr_quark key, double val);

    /**
    ***
    **/

    void port_test(PortTestIpProtocol ip_protocol);
    bool port_test_pending(PortTestIpProtocol ip_protocol) const noexcept;

    void blocklist_update();

    void exec(tr_variant const& request);

    void open_folder(tr_torrent_id_t torrent_id) const;

    sigc::signal<void(ErrorCode, Glib::ustring const&)>& signal_add_error();
    sigc::signal<void(tr_ctor*)>& signal_add_prompt();
    sigc::signal<void(bool)>& signal_blocklist_updated();
    sigc::signal<void(bool)>& signal_busy();
    sigc::signal<void(tr_quark)>& signal_prefs_changed();
    sigc::signal<void(std::optional<bool>, PortTestIpProtocol)>& signal_port_tested();
    sigc::signal<void(std::unordered_set<tr_torrent_id_t> const&, Torrent::ChangeFlags)>& signal_torrents_changed();

protected:
    explicit Session(tr_session* session);

private:
    class Impl;
    std::unique_ptr<Impl> const impl_;
};
