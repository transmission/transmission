/*
 * This file Copyright (C) 2012-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <set>
#include <stdlib.h> /* qsort() */
#include <unordered_map>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h>
#include <libtransmission/web-utils.h>

#include "FaviconCache.h" /* gtr_get_favicon() */
#include "FilterBar.h"
#include "HigWorkarea.h" /* GUI_PAD */
#include "Session.h" /* MC_TORRENT */
#include "Utils.h" /* gtr_get_host_from_url() */

namespace
{

auto const DIRTY_KEY = Glib::Quark("tr-filter-dirty-key");
auto const SESSION_KEY = Glib::Quark("tr-session-key");
auto const TEXT_KEY = Glib::Quark("tr-filter-text-key");
auto const TORRENT_MODEL_KEY = Glib::Quark("tr-filter-torrent-model-key");

} // namespace

class FilterBar::Impl
{
public:
    Impl(FilterBar& widget, tr_session* session, Glib::RefPtr<Gtk::TreeModel> const& torrent_model);
    ~Impl();

    Glib::RefPtr<Gtk::TreeModel> get_filter_model() const;

private:
    Gtk::ComboBox* activity_combo_box_new(Glib::RefPtr<Gtk::TreeModel> const& tmodel);
    Gtk::ComboBox* tracker_combo_box_new(Glib::RefPtr<Gtk::TreeModel> const& tmodel);

    void update_count_label_idle();

    bool is_row_visible(Gtk::TreeModel::const_iterator const& iter);

    void selection_changed_cb();
    bool update_count_label();
    void filter_entry_changed();

private:
    FilterBar& widget_;

    Gtk::ComboBox* activity_ = nullptr;
    Gtk::ComboBox* tracker_ = nullptr;
    Gtk::Entry* entry_ = nullptr;
    Gtk::Label* show_lb_ = nullptr;
    Glib::RefPtr<Gtk::TreeModelFilter> filter_model_;
    int active_activity_type_ = 0;
    int active_tracker_type_ = 0;
    Glib::ustring active_tracker_host_;

    sigc::connection activity_model_row_changed_tag_;
    sigc::connection activity_model_row_inserted_tag_;
    sigc::connection activity_model_row_deleted_cb_tag_;

    sigc::connection torrent_model_row_changed_tag_;
    sigc::connection torrent_model_row_inserted_tag_;
    sigc::connection torrent_model_row_deleted_cb_tag_;
};

/***
****
****  TRACKERS
****
***/

namespace
{

enum
{
    TRACKER_FILTER_TYPE_ALL,
    TRACKER_FILTER_TYPE_HOST,
    TRACKER_FILTER_TYPE_SEPARATOR,
};

class TrackerFilterModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    TrackerFilterModelColumns()
    {
        add(name);
        add(count);
        add(type);
        add(host);
        add(pixbuf);
    }

    Gtk::TreeModelColumn<Glib::ustring> name; /* human-readable name; ie, Legaltorrents */
    Gtk::TreeModelColumn<int> count; /* how many matches there are */
    Gtk::TreeModelColumn<int> type;
    Gtk::TreeModelColumn<std::string> host; /* pattern-matching text; ie, legaltorrents.com */
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> pixbuf;
};

TrackerFilterModelColumns const tracker_filter_cols;

/* human-readable name; ie, Legaltorrents */
Glib::ustring get_name_from_host(std::string const& host)
{
    std::string name;

    if (tr_addressIsIP(host.c_str()))
    {
        name = host;
    }
    else if (auto const dot = host.rfind('.'); dot != std::string::npos)
    {
        name = host.substr(0, dot);
    }
    else
    {
        name = host;
    }

    if (!name.empty())
    {
        name.front() = Glib::Ascii::toupper(name.front());
    }

    return name;
}

void tracker_model_update_count(Gtk::TreeModel::iterator const& iter, int n)
{
    if (n != iter->get_value(tracker_filter_cols.count))
    {
        iter->set_value(tracker_filter_cols.count, n);
    }
}

void favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const& pixbuf, Gtk::TreeRowReference& reference)
{
    if (pixbuf != nullptr)
    {
        auto const path = reference.get_path();
        auto const model = reference.get_model();

        if (auto const iter = model->get_iter(path); iter)
        {
            iter->set_value(tracker_filter_cols.pixbuf, pixbuf);
        }
    }
}

bool tracker_filter_model_update(Glib::RefPtr<Gtk::TreeStore> const& tracker_model)
{
    tracker_model->steal_data(DIRTY_KEY);

    /* Walk through all the torrents, tallying how many matches there are
     * for the various categories. Also make a sorted list of all tracker
     * hosts s.t. we can merge it with the existing list */
    int num_torrents = 0;
    std::vector<std::string const*> hosts;
    std::set<std::string> strings;
    std::unordered_map<std::string const*, int> hosts_hash;
    auto* tmodel = static_cast<Gtk::TreeModel*>(tracker_model->get_data(TORRENT_MODEL_KEY));
    for (auto const& row : tmodel->children())
    {
        auto const* tor = static_cast<tr_torrent const*>(row.get_value(torrent_cols.torrent));
        auto const* const inf = tr_torrentInfo(tor);

        std::set<std::string const*> keys;

        for (unsigned int i = 0; i < inf->trackerCount; ++i)
        {
            auto const* const key = &*strings.insert(gtr_get_host_from_url(inf->trackers[i].announce)).first;

            if (auto const count = hosts_hash.find(key); count == hosts_hash.end())
            {
                hosts_hash.emplace(key, 0);
                hosts.push_back(key);
            }

            keys.insert(key);
        }

        for (auto const* const key : keys)
        {
            ++hosts_hash.at(key);
        }

        ++num_torrents;
    }

    std::sort(hosts.begin(), hosts.end(), [](auto const* lhs, auto const& rhs) { return *lhs < *rhs; });

    // update the "all" count
    auto iter = tracker_model->children().begin();
    if (iter)
    {
        tracker_model_update_count(iter, num_torrents);
    }

    // offset past the "All" and the separator
    ++iter;
    ++iter;

    size_t i = 0;
    for (;;)
    {
        // are we done yet?
        bool const new_hosts_done = i >= hosts.size();
        bool const old_hosts_done = !iter;
        if (new_hosts_done && old_hosts_done)
        {
            break;
        }

        // decide what to do
        bool remove_row = false;
        bool insert_row = false;
        if (new_hosts_done)
        {
            remove_row = true;
        }
        else if (old_hosts_done)
        {
            insert_row = true;
        }
        else
        {
            auto const host = iter->get_value(tracker_filter_cols.host);
            int const cmp = host.compare(*hosts.at(i));

            if (cmp < 0)
            {
                remove_row = true;
            }
            else if (cmp > 0)
            {
                insert_row = true;
            }
        }

        // do something
        if (remove_row)
        {
            iter = tracker_model->erase(iter);
        }
        else if (insert_row)
        {
            auto* session = static_cast<tr_session*>(tracker_model->get_data(SESSION_KEY));
            auto const* host = hosts.at(i);
            auto const add = tracker_model->insert(iter);
            add->set_value(tracker_filter_cols.host, *host);
            add->set_value(tracker_filter_cols.name, get_name_from_host(*host));
            add->set_value(tracker_filter_cols.count, hosts_hash.at(host));
            add->set_value(tracker_filter_cols.type, static_cast<int>(TRACKER_FILTER_TYPE_HOST));
            auto path = tracker_model->get_path(add);
            gtr_get_favicon(
                session,
                *host,
                [ref = Gtk::TreeRowReference(tracker_model, path)](auto const& pixbuf) mutable
                { favicon_ready_cb(pixbuf, ref); });
            // ++iter;
            ++i;
        }
        else // update row
        {
            auto const* const host = hosts.at(i);
            auto const count = hosts_hash.at(host);
            tracker_model_update_count(iter, count);
            ++iter;
            ++i;
        }
    }

    return false;
}

Glib::RefPtr<Gtk::TreeStore> tracker_filter_model_new(Glib::RefPtr<Gtk::TreeModel> const& tmodel)
{
    auto const store = Gtk::TreeStore::create(tracker_filter_cols);

    auto iter = store->append();
    iter->set_value(tracker_filter_cols.name, Glib::ustring(_("All")));
    iter->set_value(tracker_filter_cols.type, static_cast<int>(TRACKER_FILTER_TYPE_ALL));

    iter = store->append();
    iter->set_value(tracker_filter_cols.type, static_cast<int>(TRACKER_FILTER_TYPE_SEPARATOR));

    store->set_data(TORRENT_MODEL_KEY, gtr_get_ptr(tmodel));
    tracker_filter_model_update(store);
    return store;
}

bool is_it_a_separator(Glib::RefPtr<Gtk::TreeModel> const& /*model*/, Gtk::TreeIter const& iter)
{
    return iter->get_value(tracker_filter_cols.type) == TRACKER_FILTER_TYPE_SEPARATOR;
}

void tracker_model_update_idle(Glib::RefPtr<Gtk::TreeStore> const& tracker_model)
{
    bool const pending = tracker_model->get_data(DIRTY_KEY) != nullptr;

    if (!pending)
    {
        tracker_model->set_data(DIRTY_KEY, GINT_TO_POINTER(1));
        Glib::signal_idle().connect([tracker_model]() { return tracker_filter_model_update(tracker_model); });
    }
}

void render_pixbuf_func(Gtk::CellRendererPixbuf* cell_renderer, Gtk::TreeModel::const_iterator const& iter)
{
    cell_renderer->property_width() = (iter->get_value(tracker_filter_cols.type) == TRACKER_FILTER_TYPE_HOST) ? 20 : 0;
}

void render_number_func(Gtk::CellRendererText* cell_renderer, Gtk::TreeModel::const_iterator const& iter)
{
    auto const count = iter->get_value(tracker_filter_cols.count);
    cell_renderer->property_text() = count >= 0 ? gtr_sprintf("%'d", count) : "";
}

Gtk::CellRendererText* number_renderer_new()
{
    auto* r = Gtk::make_managed<Gtk::CellRendererText>();

    r->property_alignment() = Pango::ALIGN_RIGHT;
    r->property_weight() = Pango::WEIGHT_ULTRALIGHT;
    r->property_xalign() = 1.0;
    r->property_xpad() = GUI_PAD;

    return r;
}

} // namespace

Gtk::ComboBox* FilterBar::Impl::tracker_combo_box_new(Glib::RefPtr<Gtk::TreeModel> const& tmodel)
{
    /* create the tracker combobox */
    auto const cat_model = tracker_filter_model_new(tmodel);
    auto* c = Gtk::make_managed<Gtk::ComboBox>(static_cast<Glib::RefPtr<Gtk::TreeModel> const&>(cat_model));
    c->set_row_separator_func(&is_it_a_separator);
    c->set_active(0);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        c->pack_start(*r, false);
        c->set_cell_data_func(*r, [r](auto const& iter) { render_pixbuf_func(r, iter); });
        c->add_attribute(r->property_pixbuf(), tracker_filter_cols.pixbuf);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        c->pack_start(*r, false);
        c->add_attribute(r->property_text(), tracker_filter_cols.name);
    }

    {
        auto* r = number_renderer_new();
        c->pack_end(*r, true);
        c->set_cell_data_func(*r, [r](auto const& iter) { render_number_func(r, iter); });
    }

    torrent_model_row_changed_tag_ = tmodel->signal_row_changed().connect(
        [cat_model](auto const& /*path*/, auto const& /*iter*/) { tracker_model_update_idle(cat_model); });
    torrent_model_row_inserted_tag_ = tmodel->signal_row_inserted().connect(
        [cat_model](auto const& /*path*/, auto const& /*iter*/) { tracker_model_update_idle(cat_model); });
    torrent_model_row_deleted_cb_tag_ = tmodel->signal_row_deleted().connect( //
        [cat_model](auto const& /*path*/) { tracker_model_update_idle(cat_model); });

    return c;
}

namespace
{

bool test_tracker(tr_torrent const* tor, int active_tracker_type, Glib::ustring const& host)
{
    bool matches = true;

    if (active_tracker_type == TRACKER_FILTER_TYPE_HOST)
    {
        auto const* const inf = tr_torrentInfo(tor);

        matches = false;

        for (unsigned int i = 0; !matches && i < inf->trackerCount; ++i)
        {
            matches = gtr_get_host_from_url(inf->trackers[i].announce) == host;
        }
    }

    return matches;
}

/***
****
****  ACTIVITY
****
***/

enum
{
    ACTIVITY_FILTER_ALL,
    ACTIVITY_FILTER_DOWNLOADING,
    ACTIVITY_FILTER_SEEDING,
    ACTIVITY_FILTER_ACTIVE,
    ACTIVITY_FILTER_PAUSED,
    ACTIVITY_FILTER_FINISHED,
    ACTIVITY_FILTER_VERIFYING,
    ACTIVITY_FILTER_ERROR,
    ACTIVITY_FILTER_SEPARATOR
};

class ActivityFilterModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    ActivityFilterModelColumns()
    {
        add(name);
        add(count);
        add(type);
        add(icon_name);
    }

    Gtk::TreeModelColumn<Glib::ustring> name;
    Gtk::TreeModelColumn<int> count;
    Gtk::TreeModelColumn<int> type;
    Gtk::TreeModelColumn<Glib::ustring> icon_name;
};

ActivityFilterModelColumns const activity_filter_cols;

bool activity_is_it_a_separator(Glib::RefPtr<Gtk::TreeModel> const& /*model*/, Gtk::TreeIter const& iter)
{
    return iter->get_value(activity_filter_cols.type) == ACTIVITY_FILTER_SEPARATOR;
}

bool test_torrent_activity(tr_torrent* tor, int type)
{
    auto const* st = tr_torrentStatCached(tor);

    switch (type)
    {
    case ACTIVITY_FILTER_DOWNLOADING:
        return st->activity == TR_STATUS_DOWNLOAD || st->activity == TR_STATUS_DOWNLOAD_WAIT;

    case ACTIVITY_FILTER_SEEDING:
        return st->activity == TR_STATUS_SEED || st->activity == TR_STATUS_SEED_WAIT;

    case ACTIVITY_FILTER_ACTIVE:
        return st->peersSendingToUs > 0 || st->peersGettingFromUs > 0 || st->webseedsSendingToUs > 0 ||
            st->activity == TR_STATUS_CHECK;

    case ACTIVITY_FILTER_PAUSED:
        return st->activity == TR_STATUS_STOPPED;

    case ACTIVITY_FILTER_FINISHED:
        return st->finished == true;

    case ACTIVITY_FILTER_VERIFYING:
        return st->activity == TR_STATUS_CHECK || st->activity == TR_STATUS_CHECK_WAIT;

    case ACTIVITY_FILTER_ERROR:
        return st->error != 0;

    default: /* ACTIVITY_FILTER_ALL */
        return true;
    }
}

void status_model_update_count(Gtk::TreeIter const& iter, int n)
{
    if (n != iter->get_value(activity_filter_cols.count))
    {
        iter->set_value(activity_filter_cols.count, n);
    }
}

bool activity_filter_model_update(Glib::RefPtr<Gtk::ListStore> const& activity_model)
{
    auto* tmodel = static_cast<Gtk::TreeModel*>(activity_model->get_data(TORRENT_MODEL_KEY));

    activity_model->steal_data(DIRTY_KEY);

    for (auto& row : activity_model->children())
    {
        auto const type = row.get_value(activity_filter_cols.type);
        auto hits = 0;

        for (auto const& torrent_row : tmodel->children())
        {
            if (test_torrent_activity(static_cast<tr_torrent*>(torrent_row.get_value(torrent_cols.torrent)), type))
            {
                ++hits;
            }
        }

        status_model_update_count(row, hits);
    }

    return false;
}

Glib::RefPtr<Gtk::ListStore> activity_filter_model_new(Glib::RefPtr<Gtk::TreeModel> const& tmodel)
{
    static struct
    {
        int type;
        char const* context;
        char const* name;
        Glib::ustring icon_name;
    } const types[] = {
        { ACTIVITY_FILTER_ALL, nullptr, N_("All"), {} },
        { ACTIVITY_FILTER_SEPARATOR, nullptr, nullptr, {} },
        { ACTIVITY_FILTER_ACTIVE, nullptr, N_("Active"), "system-run" },
        { ACTIVITY_FILTER_DOWNLOADING, "Verb", NC_("Verb", "Downloading"), "network-receive" },
        { ACTIVITY_FILTER_SEEDING, "Verb", NC_("Verb", "Seeding"), "network-transmit" },
        { ACTIVITY_FILTER_PAUSED, nullptr, N_("Paused"), "media-playback-pause" },
        { ACTIVITY_FILTER_FINISHED, nullptr, N_("Finished"), "media-playback-stop" },
        { ACTIVITY_FILTER_VERIFYING, "Verb", NC_("Verb", "Verifying"), "view-refresh" },
        { ACTIVITY_FILTER_ERROR, nullptr, N_("Error"), "dialog-error" },
    };

    auto const store = Gtk::ListStore::create(activity_filter_cols);

    for (auto const& type : types)
    {
        auto const name = type.name != nullptr ?
            Glib::ustring(type.context != nullptr ? g_dpgettext2(nullptr, type.context, type.name) : _(type.name)) :
            Glib::ustring();
        auto const iter = store->append();
        iter->set_value(activity_filter_cols.name, name);
        iter->set_value(activity_filter_cols.type, type.type);
        iter->set_value(activity_filter_cols.icon_name, type.icon_name);
    }

    store->set_data(TORRENT_MODEL_KEY, gtr_get_ptr(tmodel));
    activity_filter_model_update(store);
    return store;
}

void render_activity_pixbuf_func(Gtk::CellRendererPixbuf* cell_renderer, Gtk::TreeModel::iterator const& iter)
{
    auto const type = iter->get_value(activity_filter_cols.type);
    cell_renderer->property_width() = type == ACTIVITY_FILTER_ALL ? 0 : 20;
    cell_renderer->property_ypad() = type == ACTIVITY_FILTER_ALL ? 0 : 2;
}

void activity_model_update_idle(Glib::RefPtr<Gtk::ListStore> const& activity_model)
{
    bool const pending = activity_model->get_data(DIRTY_KEY) != nullptr;

    if (!pending)
    {
        activity_model->set_data(DIRTY_KEY, GINT_TO_POINTER(1));
        Glib::signal_idle().connect([activity_model]() { return activity_filter_model_update(activity_model); });
    }
}

} // namespace

Gtk::ComboBox* FilterBar::Impl::activity_combo_box_new(Glib::RefPtr<Gtk::TreeModel> const& tmodel)
{
    auto const activity_model = activity_filter_model_new(tmodel);
    auto* c = Gtk::make_managed<Gtk::ComboBox>(static_cast<Glib::RefPtr<Gtk::TreeModel> const&>(activity_model));
    c->set_row_separator_func(&activity_is_it_a_separator);
    c->set_active(0);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        c->pack_start(*r, false);
        c->add_attribute(r->property_icon_name(), activity_filter_cols.icon_name);
        c->set_cell_data_func(*r, [r](auto const& iter) { render_activity_pixbuf_func(r, iter); });
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        c->pack_start(*r, true);
        c->add_attribute(r->property_text(), activity_filter_cols.name);
    }

    {
        auto* r = number_renderer_new();
        c->pack_end(*r, true);
        c->set_cell_data_func(*r, [r](auto const& iter) { render_number_func(r, iter); });
    }

    activity_model_row_changed_tag_ = tmodel->signal_row_changed().connect(
        [activity_model](auto const& /*path*/, auto const& /*iter*/) { activity_model_update_idle(activity_model); });
    activity_model_row_inserted_tag_ = tmodel->signal_row_inserted().connect(
        [activity_model](auto const& /*path*/, auto const& /*iter*/) { activity_model_update_idle(activity_model); });
    activity_model_row_deleted_cb_tag_ = tmodel->signal_row_deleted().connect( //
        [activity_model](auto const& /*path*/) { activity_model_update_idle(activity_model); });

    return c;
}

/****
*****
*****  ENTRY FIELD
*****
****/

namespace
{

bool testText(tr_torrent const* tor, Glib::ustring const* key)
{
    bool ret = false;

    if (key == nullptr || key->empty())
    {
        ret = true;
    }
    else
    {
        /* test the torrent name... */
        ret = Glib::ustring(tr_torrentName(tor)).casefold().find(*key) != Glib::ustring::npos;

        /* test the files... */
        for (tr_file_index_t i = 0, n = tr_torrentFileCount(tor); i < n && !ret; ++i)
        {
            ret = Glib::ustring(tr_torrentFile(tor, i).name).casefold().find(*key) != Glib::ustring::npos;
        }
    }

    return ret;
}

} // namespace

void FilterBar::Impl::filter_entry_changed()
{
    filter_model_->set_data(
        TEXT_KEY,
        new Glib::ustring(gtr_str_strip(entry_->get_text().casefold())),
        [](void* p) { delete static_cast<Glib::ustring*>(p); });
    filter_model_->refilter();
}

/*****
******
******
******
*****/

bool FilterBar::Impl::is_row_visible(Gtk::TreeModel::const_iterator const& iter)
{
    auto* tor = static_cast<tr_torrent*>(iter->get_value(torrent_cols.torrent));
    auto const* text = static_cast<Glib::ustring const*>(filter_model_->get_data(TEXT_KEY));

    return tor != nullptr && test_tracker(tor, active_tracker_type_, active_tracker_host_) &&
        test_torrent_activity(tor, active_activity_type_) && testText(tor, text);
}

void FilterBar::Impl::selection_changed_cb()
{
    /* set active_activity_type_ from the activity combobox */
    if (auto const iter = activity_->get_active(); iter)
    {
        active_activity_type_ = iter->get_value(activity_filter_cols.type);
    }
    else
    {
        active_activity_type_ = ACTIVITY_FILTER_ALL;
    }

    /* set the active tracker type & host from the tracker combobox */
    if (auto const iter = tracker_->get_active(); iter)
    {
        active_tracker_type_ = iter->get_value(tracker_filter_cols.type);
        active_tracker_host_ = iter->get_value(tracker_filter_cols.host);
    }
    else
    {
        active_tracker_type_ = TRACKER_FILTER_TYPE_ALL;
        active_tracker_host_.clear();
    }

    /* refilter */
    filter_model_->refilter();
}

/***
****
***/

bool FilterBar::Impl::update_count_label()
{
    /* get the visible count */
    auto const visibleCount = static_cast<int>(filter_model_->children().size());

    /* get the tracker count */
    int trackerCount;
    if (auto const iter = tracker_->get_active(); iter)
    {
        trackerCount = iter->get_value(tracker_filter_cols.count);
    }
    else
    {
        trackerCount = 0;
    }

    /* get the activity count */
    int activityCount;
    if (auto const iter = activity_->get_active(); iter)
    {
        activityCount = iter->get_value(activity_filter_cols.count);
    }
    else
    {
        activityCount = 0;
    }

    /* set the text */
    show_lb_->set_markup_with_mnemonic(
        visibleCount == std::min(activityCount, trackerCount) ? _("_Show:") : gtr_sprintf(_("_Show %'d of:"), visibleCount));

    show_lb_->steal_data(DIRTY_KEY);
    return false;
}

void FilterBar::Impl::update_count_label_idle()
{
    bool const pending = show_lb_->get_data(DIRTY_KEY) != nullptr;

    if (!pending)
    {
        show_lb_->set_data(DIRTY_KEY, GINT_TO_POINTER(1));
        Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::update_count_label));
    }
}

/***
****
***/

FilterBar::FilterBar(tr_session* session, Glib::RefPtr<Gtk::TreeModel> const& tmodel)
    : Gtk::Box(Gtk::ORIENTATION_HORIZONTAL, GUI_PAD_SMALL)
    , impl_(std::make_unique<Impl>(*this, session, tmodel))
{
}

FilterBar::Impl::Impl(FilterBar& widget, tr_session* session, Glib::RefPtr<Gtk::TreeModel> const& tmodel)
    : widget_(widget)
{
    show_lb_ = Gtk::make_managed<Gtk::Label>();
    activity_ = activity_combo_box_new(tmodel);
    tracker_ = tracker_combo_box_new(tmodel);
    filter_model_ = Gtk::TreeModelFilter::create(tmodel);
    filter_model_->signal_row_deleted().connect([this](auto const& /*path*/) { update_count_label_idle(); });
    filter_model_->signal_row_inserted().connect([this](auto const& /*path*/, auto const& /*iter*/)
                                                 { update_count_label_idle(); });

    tracker_->property_width_request() = 170;
    static_cast<Gtk::TreeStore*>(gtr_get_ptr(tracker_->get_model()))->set_data(SESSION_KEY, session);

    filter_model_->set_visible_func(sigc::mem_fun(*this, &Impl::is_row_visible));

    tracker_->signal_changed().connect(sigc::mem_fun(*this, &Impl::selection_changed_cb));
    activity_->signal_changed().connect(sigc::mem_fun(*this, &Impl::selection_changed_cb));

    /* add the activity combobox */
    show_lb_->set_mnemonic_widget(*activity_);
    widget_.pack_start(*show_lb_, false, false, 0);
    widget_.pack_start(*activity_, true, true, 0);
    activity_->set_margin_end(GUI_PAD);

    /* add the tracker combobox */
    widget_.pack_start(*tracker_, true, true, 0);
    tracker_->set_margin_end(GUI_PAD);

    /* add the entry field */
    entry_ = Gtk::make_managed<Gtk::Entry>();
    entry_->set_icon_from_icon_name("edit-clear", Gtk::ENTRY_ICON_SECONDARY);
    entry_->signal_icon_release().connect([this](auto /*icon_position*/, auto const* /*event*/) { entry_->set_text({}); });
    widget_.pack_start(*entry_, true, true, 0);

    entry_->signal_changed().connect(sigc::mem_fun(*this, &Impl::filter_entry_changed));
    selection_changed_cb();

    update_count_label();
}

FilterBar::~FilterBar() = default;

FilterBar::Impl::~Impl()
{
    torrent_model_row_deleted_cb_tag_.disconnect();
    torrent_model_row_inserted_tag_.disconnect();
    torrent_model_row_changed_tag_.disconnect();

    activity_model_row_deleted_cb_tag_.disconnect();
    activity_model_row_inserted_tag_.disconnect();
    activity_model_row_changed_tag_.disconnect();
}

Glib::RefPtr<Gtk::TreeModel> FilterBar::get_filter_model() const
{
    return impl_->get_filter_model();
}

Glib::RefPtr<Gtk::TreeModel> FilterBar::Impl::get_filter_model() const
{
    return filter_model_;
}
