// This file Copyright © 2012-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <algorithm> // std::transform()
#include <array>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <fmt/core.h>

#include "FaviconCache.h" // gtr_get_favicon()
#include "FilterBar.h"
#include "HigWorkarea.h" // GUI_PAD
#include "Session.h" // torrent_cols
#include "Utils.h"

class FilterBar::Impl
{
public:
    Impl(FilterBar& widget, tr_session* session, Glib::RefPtr<Gtk::TreeModel> const& torrent_model);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

    [[nodiscard]] Glib::RefPtr<Gtk::TreeModel> get_filter_model() const;

private:
    template<typename T>
    T* get_template_child(char const* name) const;

    void activity_combo_box_init(Gtk::ComboBox& combo);
    static void render_activity_pixbuf_func(Gtk::CellRendererPixbuf& cell_renderer, Gtk::TreeModel::const_iterator const& iter);

    void tracker_combo_box_init(Gtk::ComboBox& combo);
    static void render_pixbuf_func(Gtk::CellRendererPixbuf& cell_renderer, Gtk::TreeModel::const_iterator const& iter);
    static void render_number_func(Gtk::CellRendererText& cell_renderer, Gtk::TreeModel::const_iterator const& iter);

    void selection_changed_cb();
    void filter_entry_changed();

    Glib::RefPtr<Gtk::ListStore> activity_filter_model_new();
    void activity_model_update_idle();
    bool activity_filter_model_update();
    void status_model_update_count(Gtk::TreeModel::iterator const& iter, int n);
    bool activity_is_it_a_separator(Gtk::TreeModel::const_iterator const& iter);

    Glib::RefPtr<Gtk::TreeStore> tracker_filter_model_new();
    void tracker_model_update_idle();
    bool tracker_filter_model_update();
    void tracker_model_update_count(Gtk::TreeModel::iterator const& iter, int n);
    bool is_it_a_separator(Gtk::TreeModel::const_iterator const& iter);
    void favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const& pixbuf, Gtk::TreeRowReference& reference);

    void update_count_label_idle();
    bool update_count_label();
    bool is_row_visible(Gtk::TreeModel::const_iterator const& iter);

    bool test_tracker(tr_torrent const& tor, int active_tracker_type, Glib::ustring const& host);
    bool test_torrent_activity(tr_torrent& tor, int type);

    static Glib::ustring get_name_from_host(std::string const& host);

    static Gtk::CellRendererText* number_renderer_new();

    static bool testText(tr_torrent const& tor, Glib::ustring const& key);

private:
    FilterBar& widget_;
    tr_session* const session_;
    Glib::RefPtr<Gtk::TreeModel> const torrent_model_;

    Glib::RefPtr<Gtk::ListStore> const activity_model_;
    Glib::RefPtr<Gtk::TreeStore> const tracker_model_;

    Gtk::ComboBox* activity_ = nullptr;
    Gtk::ComboBox* tracker_ = nullptr;
    Gtk::Entry* entry_ = nullptr;
    Gtk::Label* show_lb_ = nullptr;
    Glib::RefPtr<Gtk::TreeModelFilter> filter_model_;
    int active_activity_type_ = 0;
    int active_tracker_type_ = 0;
    Glib::ustring active_tracker_sitename_;

    sigc::connection activity_model_row_changed_tag_;
    sigc::connection activity_model_row_inserted_tag_;
    sigc::connection activity_model_row_deleted_cb_tag_;
    sigc::connection activity_model_update_tag_;

    sigc::connection tracker_model_row_changed_tag_;
    sigc::connection tracker_model_row_inserted_tag_;
    sigc::connection tracker_model_row_deleted_cb_tag_;
    sigc::connection tracker_model_update_tag_;

    sigc::connection filter_model_row_deleted_tag_;
    sigc::connection filter_model_row_inserted_tag_;

    sigc::connection update_count_label_tag_;

    Glib::ustring filter_text_;
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
    TrackerFilterModelColumns() noexcept
    {
        add(displayname);
        add(count);
        add(type);
        add(sitename);
        add(pixbuf);
    }

    Gtk::TreeModelColumn<Glib::ustring> displayname; /* human-readable name; ie, Legaltorrents */
    Gtk::TreeModelColumn<int> count; /* how many matches there are */
    Gtk::TreeModelColumn<int> type;
    Gtk::TreeModelColumn<Glib::ustring> sitename; // pattern-matching text; see tr_parsed_url.sitename
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> pixbuf;
};

TrackerFilterModelColumns const tracker_filter_cols;

} // namespace

/* human-readable name; ie, Legaltorrents */
Glib::ustring FilterBar::Impl::get_name_from_host(std::string const& host)
{
    std::string name = host;

    if (!name.empty())
    {
        name.front() = Glib::Ascii::toupper(name.front());
    }

    return name;
}

void FilterBar::Impl::tracker_model_update_count(Gtk::TreeModel::iterator const& iter, int n)
{
    if (n != iter->get_value(tracker_filter_cols.count))
    {
        iter->set_value(tracker_filter_cols.count, n);
    }
}

void FilterBar::Impl::favicon_ready_cb(Glib::RefPtr<Gdk::Pixbuf> const& pixbuf, Gtk::TreeRowReference& reference)
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

bool FilterBar::Impl::tracker_filter_model_update()
{
    struct site_info
    {
        int count = 0;
        std::string host;
        std::string sitename;

        bool operator<(site_info const& that) const
        {
            return sitename < that.sitename;
        }
    };

    /* Walk through all the torrents, tallying how many matches there are
     * for the various categories. Also make a sorted list of all tracker
     * hosts s.t. we can merge it with the existing list */
    auto n_torrents = int{ 0 };
    auto site_infos = std::unordered_map<std::string /*site*/, site_info>{};
    for (auto const& row : torrent_model_->children())
    {
        auto const* tor = static_cast<tr_torrent const*>(row.get_value(torrent_cols.torrent));

        auto torrent_sites_and_hosts = std::map<std::string, std::string>{};
        for (size_t i = 0, n = tr_torrentTrackerCount(tor); i < n; ++i)
        {
            auto const view = tr_torrentTracker(tor, i);
            torrent_sites_and_hosts.try_emplace(std::data(view.sitename), view.host);
        }

        for (auto const& [sitename, host] : torrent_sites_and_hosts)
        {
            auto& info = site_infos[sitename];
            info.sitename = sitename;
            info.host = host;
            ++info.count;
        }

        ++n_torrents;
    }

    auto const n_sites = std::size(site_infos);
    auto sites_v = std::vector<site_info>(n_sites);
    std::transform(std::begin(site_infos), std::end(site_infos), std::begin(sites_v), [](auto const& it) { return it.second; });
    std::sort(std::begin(sites_v), std::end(sites_v));

    // update the "all" count
    auto iter = tracker_model_->children().begin();
    if (iter)
    {
        tracker_model_update_count(iter, n_torrents);
    }

    // offset past the "All" and the separator
    ++iter;
    ++iter;

    size_t i = 0;
    for (;;)
    {
        // are we done yet?
        bool const new_sites_done = i >= n_sites;
        bool const old_sites_done = !iter;
        if (new_sites_done && old_sites_done)
        {
            break;
        }

        // decide what to do
        bool remove_row = false;
        bool insert_row = false;
        if (new_sites_done)
        {
            remove_row = true;
        }
        else if (old_sites_done)
        {
            insert_row = true;
        }
        else
        {
            auto const sitename = iter->get_value(tracker_filter_cols.sitename);
            int const cmp = sitename.raw().compare(sites_v.at(i).sitename);

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
            iter = tracker_model_->erase(iter);
        }
        else if (insert_row)
        {
            auto const& site = sites_v.at(i);
            auto const add = tracker_model_->insert(iter);
            add->set_value(tracker_filter_cols.sitename, Glib::ustring{ site.sitename });
            add->set_value(tracker_filter_cols.displayname, get_name_from_host(site.sitename));
            add->set_value(tracker_filter_cols.count, site.count);
            add->set_value(tracker_filter_cols.type, static_cast<int>(TRACKER_FILTER_TYPE_HOST));
            auto path = tracker_model_->get_path(add);
            gtr_get_favicon(
                session_,
                site.host,
                [this, ref = Gtk::TreeRowReference(tracker_model_, path)](auto const& pixbuf) mutable
                { favicon_ready_cb(pixbuf, ref); });
            // ++iter;
            ++i;
        }
        else // update row
        {
            tracker_model_update_count(iter, sites_v.at(i).count);
            ++iter;
            ++i;
        }
    }

    return false;
}

Glib::RefPtr<Gtk::TreeStore> FilterBar::Impl::tracker_filter_model_new()
{
    auto store = Gtk::TreeStore::create(tracker_filter_cols);

    auto iter = store->append();
    iter->set_value(tracker_filter_cols.displayname, Glib::ustring(_("All")));
    iter->set_value(tracker_filter_cols.type, static_cast<int>(TRACKER_FILTER_TYPE_ALL));

    iter = store->append();
    iter->set_value(tracker_filter_cols.type, static_cast<int>(TRACKER_FILTER_TYPE_SEPARATOR));

    return store;
}

bool FilterBar::Impl::is_it_a_separator(Gtk::TreeModel::const_iterator const& iter)
{
    return iter->get_value(tracker_filter_cols.type) == TRACKER_FILTER_TYPE_SEPARATOR;
}

void FilterBar::Impl::tracker_model_update_idle()
{
    if (!tracker_model_update_tag_.connected())
    {
        tracker_model_update_tag_ = Glib::signal_idle().connect([this]() { return tracker_filter_model_update(); });
    }
}

void FilterBar::Impl::render_pixbuf_func(Gtk::CellRendererPixbuf& cell_renderer, Gtk::TreeModel::const_iterator const& iter)
{
    cell_renderer.property_width() = (iter->get_value(tracker_filter_cols.type) == TRACKER_FILTER_TYPE_HOST) ? 20 : 0;
}

void FilterBar::Impl::render_number_func(Gtk::CellRendererText& cell_renderer, Gtk::TreeModel::const_iterator const& iter)
{
    auto const count = iter->get_value(tracker_filter_cols.count);
    cell_renderer.property_text() = count >= 0 ? fmt::format("{:L}", count) : "";
}

Gtk::CellRendererText* FilterBar::Impl::number_renderer_new()
{
    auto* r = Gtk::make_managed<Gtk::CellRendererText>();

    r->property_alignment() = TR_PANGO_ALIGNMENT(RIGHT);
    r->property_weight() = TR_PANGO_WEIGHT(ULTRALIGHT);
    r->property_xalign() = 1.0;
    r->property_xpad() = GUI_PAD;

    return r;
}

void FilterBar::Impl::tracker_combo_box_init(Gtk::ComboBox& combo)
{
    combo.set_model(tracker_model_);
    combo.set_row_separator_func(sigc::hide<0>(sigc::mem_fun(*this, &Impl::is_it_a_separator)));
    combo.set_active(0);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        combo.pack_start(*r, false);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_pixbuf_func(*r, iter); });
        combo.add_attribute(r->property_pixbuf(), tracker_filter_cols.pixbuf);
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        combo.pack_start(*r, false);
        combo.add_attribute(r->property_text(), tracker_filter_cols.displayname);
    }

    {
        auto* r = number_renderer_new();
        combo.pack_end(*r, true);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_number_func(*r, iter); });
    }

    tracker_model_row_changed_tag_ = torrent_model_->signal_row_changed().connect( //
        [this](auto const& /*path*/, auto const& /*iter*/) { tracker_model_update_idle(); });
    tracker_model_row_inserted_tag_ = torrent_model_->signal_row_inserted().connect( //
        [this](auto const& /*path*/, auto const& /*iter*/) { tracker_model_update_idle(); });
    tracker_model_row_deleted_cb_tag_ = torrent_model_->signal_row_deleted().connect( //
        [this](auto const& /*path*/) { tracker_model_update_idle(); });
}

bool FilterBar::Impl::test_tracker(tr_torrent const& tor, int active_tracker_type, Glib::ustring const& host)
{
    if (active_tracker_type != TRACKER_FILTER_TYPE_HOST)
    {
        return true;
    }

    for (size_t i = 0, n = tr_torrentTrackerCount(&tor); i < n; ++i)
    {
        if (auto const tracker = tr_torrentTracker(&tor, i); std::data(tracker.sitename) == host)
        {
            return true;
        }
    }

    return false;
}

namespace
{

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
    ActivityFilterModelColumns() noexcept
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

} // namespace

bool FilterBar::Impl::activity_is_it_a_separator(Gtk::TreeModel::const_iterator const& iter)
{
    return iter->get_value(activity_filter_cols.type) == ACTIVITY_FILTER_SEPARATOR;
}

bool FilterBar::Impl::test_torrent_activity(tr_torrent& tor, int type)
{
    auto const* st = tr_torrentStatCached(&tor);

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
        return st->finished;

    case ACTIVITY_FILTER_VERIFYING:
        return st->activity == TR_STATUS_CHECK || st->activity == TR_STATUS_CHECK_WAIT;

    case ACTIVITY_FILTER_ERROR:
        return st->error != 0;

    default: /* ACTIVITY_FILTER_ALL */
        return true;
    }
}

void FilterBar::Impl::status_model_update_count(Gtk::TreeModel::iterator const& iter, int n)
{
    if (n != iter->get_value(activity_filter_cols.count))
    {
        iter->set_value(activity_filter_cols.count, n);
    }
}

bool FilterBar::Impl::activity_filter_model_update()
{
    for (auto& row : activity_model_->children())
    {
        auto const type = row.get_value(activity_filter_cols.type);
        auto hits = 0;

        for (auto const& torrent_row : torrent_model_->children())
        {
            if (test_torrent_activity(*static_cast<tr_torrent*>(torrent_row.get_value(torrent_cols.torrent)), type))
            {
                ++hits;
            }
        }

        status_model_update_count(TR_GTK_TREE_MODEL_CHILD_ITER(row), hits);
    }

    return false;
}

Glib::RefPtr<Gtk::ListStore> FilterBar::Impl::activity_filter_model_new()
{
    struct FilterTypeInfo
    {
        int type;
        char const* context;
        char const* name;
        Glib::ustring icon_name;
    };

    static auto const types = std::array<FilterTypeInfo, 9>({ {
        { ACTIVITY_FILTER_ALL, nullptr, N_("All"), {} },
        { ACTIVITY_FILTER_SEPARATOR, nullptr, nullptr, {} },
        { ACTIVITY_FILTER_ACTIVE, nullptr, N_("Active"), "system-run" },
        { ACTIVITY_FILTER_DOWNLOADING, "Verb", NC_("Verb", "Downloading"), "network-receive" },
        { ACTIVITY_FILTER_SEEDING, "Verb", NC_("Verb", "Seeding"), "network-transmit" },
        { ACTIVITY_FILTER_PAUSED, nullptr, N_("Paused"), "media-playback-pause" },
        { ACTIVITY_FILTER_FINISHED, nullptr, N_("Finished"), "media-playback-stop" },
        { ACTIVITY_FILTER_VERIFYING, "Verb", NC_("Verb", "Verifying"), "view-refresh" },
        { ACTIVITY_FILTER_ERROR, nullptr, N_("Error"), "dialog-error" },
    } });

    auto store = Gtk::ListStore::create(activity_filter_cols);

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

    return store;
}

void FilterBar::Impl::render_activity_pixbuf_func(
    Gtk::CellRendererPixbuf& cell_renderer,
    Gtk::TreeModel::const_iterator const& iter)
{
    auto const type = iter->get_value(activity_filter_cols.type);
    cell_renderer.property_width() = type == ACTIVITY_FILTER_ALL ? 0 : 20;
    cell_renderer.property_ypad() = type == ACTIVITY_FILTER_ALL ? 0 : 2;
}

void FilterBar::Impl::activity_model_update_idle()
{
    if (!activity_model_update_tag_.connected())
    {
        activity_model_update_tag_ = Glib::signal_idle().connect([this]() { return activity_filter_model_update(); });
    }
}

void FilterBar::Impl::activity_combo_box_init(Gtk::ComboBox& combo)
{
    combo.set_model(activity_model_);
    combo.set_row_separator_func(sigc::hide<0>(sigc::mem_fun(*this, &Impl::activity_is_it_a_separator)));
    combo.set_active(0);

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererPixbuf>();
        combo.pack_start(*r, false);
        combo.add_attribute(r->property_icon_name(), activity_filter_cols.icon_name);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_activity_pixbuf_func(*r, iter); });
    }

    {
        auto* r = Gtk::make_managed<Gtk::CellRendererText>();
        combo.pack_start(*r, true);
        combo.add_attribute(r->property_text(), activity_filter_cols.name);
    }

    {
        auto* r = number_renderer_new();
        combo.pack_end(*r, true);
        combo.set_cell_data_func(*r, [r](auto const& iter) { render_number_func(*r, iter); });
    }

    activity_model_row_changed_tag_ = torrent_model_->signal_row_changed().connect( //
        [this](auto const& /*path*/, auto const& /*iter*/) { activity_model_update_idle(); });
    activity_model_row_inserted_tag_ = torrent_model_->signal_row_inserted().connect( //
        [this](auto const& /*path*/, auto const& /*iter*/) { activity_model_update_idle(); });
    activity_model_row_deleted_cb_tag_ = torrent_model_->signal_row_deleted().connect( //
        [this](auto const& /*path*/) { activity_model_update_idle(); });
}

/****
*****
*****  ENTRY FIELD
*****
****/

bool FilterBar::Impl::testText(tr_torrent const& tor, Glib::ustring const& key)
{
    bool ret = false;

    if (key.empty())
    {
        ret = true;
    }
    else
    {
        /* test the torrent name... */
        ret = Glib::ustring(tr_torrentName(&tor)).casefold().find(key) != Glib::ustring::npos;

        /* test the files... */
        for (tr_file_index_t i = 0, n = tr_torrentFileCount(&tor); i < n && !ret; ++i)
        {
            ret = Glib::ustring(tr_torrentFile(&tor, i).name).casefold().find(key) != Glib::ustring::npos;
        }
    }

    return ret;
}

void FilterBar::Impl::filter_entry_changed()
{
    filter_text_ = gtr_str_strip(entry_->get_text().casefold());
    filter_model_->refilter();
}

/*****
******
******
******
*****/

bool FilterBar::Impl::is_row_visible(Gtk::TreeModel::const_iterator const& iter)
{
    auto* const tor = static_cast<tr_torrent*>(iter->get_value(torrent_cols.torrent));

    return tor != nullptr && test_tracker(*tor, active_tracker_type_, active_tracker_sitename_) &&
        test_torrent_activity(*tor, active_activity_type_) && testText(*tor, filter_text_);
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
        active_tracker_sitename_ = iter->get_value(tracker_filter_cols.sitename);
    }
    else
    {
        active_tracker_type_ = TRACKER_FILTER_TYPE_ALL;
        active_tracker_sitename_.clear();
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
    int trackerCount = 0;
    if (auto const iter = tracker_->get_active(); iter)
    {
        trackerCount = iter->get_value(tracker_filter_cols.count);
    }

    /* get the activity count */
    int activityCount = 0;
    if (auto const iter = activity_->get_active(); iter)
    {
        activityCount = iter->get_value(activity_filter_cols.count);
    }

    /* set the text */
    show_lb_->set_markup_with_mnemonic(
        visibleCount == std::min(activityCount, trackerCount) ?
            _("_Show:") :
            fmt::format(_("_Show {count:L} of:"), fmt::arg("count", visibleCount)));

    return false;
}

void FilterBar::Impl::update_count_label_idle()
{
    if (!update_count_label_tag_.connected())
    {
        update_count_label_tag_ = Glib::signal_idle().connect(sigc::mem_fun(*this, &Impl::update_count_label));
    }
}

/***
****
***/

FilterBarExtraInit::FilterBarExtraInit()
    : ExtraClassInit(&FilterBarExtraInit::class_init, nullptr, &FilterBarExtraInit::instance_init)
{
}

void FilterBarExtraInit::class_init(void* klass, void* /*user_data*/)
{
    auto* const widget_klass = GTK_WIDGET_CLASS(klass);

    gtk_widget_class_set_template_from_resource(widget_klass, gtr_get_full_resource_path("FilterBar.ui").c_str());

    gtk_widget_class_bind_template_child_full(widget_klass, "activity_combo", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "tracker_combo", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "text_entry", FALSE, 0);
    gtk_widget_class_bind_template_child_full(widget_klass, "show_label", FALSE, 0);
}

void FilterBarExtraInit::instance_init(GTypeInstance* instance, void* /*klass*/)
{
    gtk_widget_init_template(GTK_WIDGET(instance));
}

/***
****
***/

FilterBar::FilterBar()
    : Glib::ObjectBase(typeid(FilterBar))
{
}

FilterBar::FilterBar(
    BaseObjectType* cast_item,
    Glib::RefPtr<Gtk::Builder> const& /*builder*/,
    tr_session* session,
    Glib::RefPtr<Gtk::TreeModel> const& torrent_model)
    : Glib::ObjectBase(typeid(FilterBar))
    , Gtk::Box(cast_item)
    , impl_(std::make_unique<Impl>(*this, session, torrent_model))
{
}

FilterBar::~FilterBar() = default;

FilterBar::Impl::Impl(FilterBar& widget, tr_session* session, Glib::RefPtr<Gtk::TreeModel> const& torrent_model)
    : widget_(widget)
    , session_(session)
    , torrent_model_(torrent_model)
    , activity_model_(activity_filter_model_new())
    , tracker_model_(tracker_filter_model_new())
    , activity_(get_template_child<Gtk::ComboBox>("activity_combo"))
    , tracker_(get_template_child<Gtk::ComboBox>("tracker_combo"))
    , entry_(get_template_child<Gtk::Entry>("text_entry"))
    , show_lb_(get_template_child<Gtk::Label>("show_label"))
{
    activity_filter_model_update();
    tracker_filter_model_update();

    activity_combo_box_init(*activity_);
    tracker_combo_box_init(*tracker_);

    filter_model_ = Gtk::TreeModelFilter::create(torrent_model);
    filter_model_row_deleted_tag_ = filter_model_->signal_row_deleted().connect([this](auto const& /*path*/)
                                                                                { update_count_label_idle(); });
    filter_model_row_inserted_tag_ = filter_model_->signal_row_inserted().connect(
        [this](auto const& /*path*/, auto const& /*iter*/) { update_count_label_idle(); });

    filter_model_->set_visible_func(sigc::mem_fun(*this, &Impl::is_row_visible));

    tracker_->signal_changed().connect(sigc::mem_fun(*this, &Impl::selection_changed_cb));
    activity_->signal_changed().connect(sigc::mem_fun(*this, &Impl::selection_changed_cb));

#if GTKMM_CHECK_VERSION(4, 0, 0)
    entry_->signal_icon_release().connect([this](auto /*icon_position*/) { entry_->set_text({}); });
#else
    entry_->signal_icon_release().connect([this](auto /*icon_position*/, auto const* /*event*/) { entry_->set_text({}); });
#endif
    entry_->signal_changed().connect(sigc::mem_fun(*this, &Impl::filter_entry_changed));

    selection_changed_cb();
    update_count_label();
}

FilterBar::Impl::~Impl()
{
    update_count_label_tag_.disconnect();

    filter_model_row_deleted_tag_.disconnect();
    filter_model_row_inserted_tag_.disconnect();

    activity_model_update_tag_.disconnect();
    tracker_model_row_deleted_cb_tag_.disconnect();
    tracker_model_row_inserted_tag_.disconnect();
    tracker_model_row_changed_tag_.disconnect();

    activity_model_update_tag_.disconnect();
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

template<typename T>
T* FilterBar::Impl::get_template_child(char const* name) const
{
    auto full_type_name = std::string("gtkmm__CustomObject_");
    Glib::append_canonical_typename(full_type_name, typeid(FilterBar).name());

    return Glib::wrap(G_TYPE_CHECK_INSTANCE_CAST(
        gtk_widget_get_template_child(GTK_WIDGET(widget_.gobj()), g_type_from_name(full_type_name.c_str()), name),
        T::get_base_type(),
        typename T::BaseObjectType));
}
