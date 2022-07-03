// This file Copyright © 2005-2022 Transmission authors and contributors.
// It may be used under the MIT (SPDX: MIT) license.
// License text can be found in the licenses/ folder.

#include <array>
#include <string>

#include <glibmm/i18n.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_formatter_speed_KBps() */

#include "Actions.h"
#include "FilterBar.h"
#include "HigWorkarea.h"
#include "MainWindow.h"
#include "Prefs.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "TorrentCellRenderer.h"
#include "Utils.h"

class MainWindow::Impl
{
public:
    Impl(MainWindow& window, Glib::RefPtr<Gio::ActionGroup> const& actions, Glib::RefPtr<Session> const& core);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

    Glib::RefPtr<Gtk::TreeSelection> get_selection() const;

    void refresh();

    void prefsChanged(tr_quark key);

private:
    Gtk::TreeView* makeview(Glib::RefPtr<Gtk::TreeModel> const& model);

    Gtk::Menu* createOptionsMenu();
    Gtk::Menu* createSpeedMenu(tr_direction dir);
    Gtk::Menu* createRatioMenu();

    void on_popup_menu(GdkEventButton* event);

    void onSpeedToggled(Gtk::CheckMenuItem* check, tr_direction dir, bool enabled);
    void onSpeedSet(tr_direction dir, int KBps);

    void onRatioToggled(Gtk::CheckMenuItem* check, bool enabled);
    void onRatioSet(double ratio);

    void updateStats();
    void updateSpeeds();

    void syncAltSpeedButton();

    void status_menu_toggled_cb(Gtk::CheckMenuItem* menu_item, std::string const& val);
    void onOptionsClicked(Gtk::Button* button);
    void onYinYangClicked(Gtk::Button* button);
    void alt_speed_toggled_cb();
    void onAltSpeedToggledIdle();

private:
    MainWindow& window_;

    std::array<Gtk::RadioMenuItem*, 2> speedlimit_on_item_;
    std::array<Gtk::RadioMenuItem*, 2> speedlimit_off_item_;
    Gtk::RadioMenuItem* ratio_on_item_ = nullptr;
    Gtk::RadioMenuItem* ratio_off_item_ = nullptr;
    Gtk::ScrolledWindow* scroll_ = nullptr;
    Gtk::TreeView* view_ = nullptr;
    Gtk::Toolbar* toolbar_ = nullptr;
    FilterBar* filter_ = nullptr;
    Gtk::Grid* status_ = nullptr;
    Gtk::Menu* status_menu_;
    Gtk::Label* ul_lb_ = nullptr;
    Gtk::Label* dl_lb_ = nullptr;
    Gtk::Label* stats_lb_ = nullptr;
    Gtk::Image* alt_speed_image_ = nullptr;
    Gtk::ToggleButton* alt_speed_button_ = nullptr;
    Gtk::Menu* options_menu_ = nullptr;
    Glib::RefPtr<Gtk::TreeSelection> selection_;
    TorrentCellRenderer* renderer_ = nullptr;
    Gtk::TreeViewColumn* column_ = nullptr;
    Glib::RefPtr<Session> const core_;
    sigc::connection pref_handler_id_;
    Gtk::Menu* popup_menu_ = nullptr;
};

/***
****
***/

void MainWindow::Impl::on_popup_menu(GdkEventButton* event)
{
    if (popup_menu_ == nullptr)
    {
        popup_menu_ = Gtk::make_managed<Gtk::Menu>(gtr_action_get_object<Gio::Menu>("main-window-popup"));
        popup_menu_->attach_to_widget(window_);
    }

    popup_menu_->popup_at_pointer(reinterpret_cast<GdkEvent*>(event));
}

namespace
{

bool tree_view_search_equal_func(
    Glib::RefPtr<Gtk::TreeModel> const& /*model*/,
    int /*column*/,
    Glib::ustring const& key,
    Gtk::TreeModel::iterator const& iter)
{
    auto const name = iter->get_value(torrent_cols.name_collated);
    return name.find(key.lowercase()) == Glib::ustring::npos;
}

} // namespace

Gtk::TreeView* MainWindow::Impl::makeview(Glib::RefPtr<Gtk::TreeModel> const& model)
{
    auto* view = Gtk::make_managed<Gtk::TreeView>();
    view->set_search_column(torrent_cols.name_collated);
    view->set_search_equal_func(&tree_view_search_equal_func);
    view->set_headers_visible(false);
    view->set_fixed_height_mode(true);

    selection_ = view->get_selection();

    column_ = Gtk::make_managed<Gtk::TreeViewColumn>();
    column_->set_title(_("Torrent"));
    column_->set_resizable(true);
    column_->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);

    renderer_ = Gtk::make_managed<TorrentCellRenderer>();
    column_->pack_start(*renderer_, false);
    column_->add_attribute(renderer_->property_torrent(), torrent_cols.torrent);
    column_->add_attribute(renderer_->property_piece_upload_speed(), torrent_cols.speed_up);
    column_->add_attribute(renderer_->property_piece_download_speed(), torrent_cols.speed_down);

    view->append_column(*column_);
    renderer_->property_xpad() = GUI_PAD_SMALL;
    renderer_->property_ypad() = GUI_PAD_SMALL;

    selection_->set_mode(Gtk::SELECTION_MULTIPLE);

    view->signal_popup_menu().connect_notify([this]() { on_popup_menu(nullptr); });
    view->signal_button_press_event().connect(
        [this, view](GdkEventButton* event)
        { return on_tree_view_button_pressed(view, event, sigc::mem_fun(*this, &Impl::on_popup_menu)); },
        false);
    view->signal_button_release_event().connect([view](GdkEventButton* event)
                                                { return on_tree_view_button_released(view, event); });
    view->signal_row_activated().connect([](auto const& /*path*/, auto* /*column*/)
                                         { gtr_action_activate("show-torrent-properties"); });

    view->set_model(model);

    return view;
}

void MainWindow::Impl::prefsChanged(tr_quark const key)
{
    switch (key)
    {
    case TR_KEY_compact_view:
        renderer_->property_compact() = gtr_pref_flag_get(key);
        /* since the cell size has changed, we need gtktreeview to revalidate
         * its fixed-height mode values. Unfortunately there's not an API call
         * for that, but it *does* revalidate when it thinks the style's been tweaked */
        g_signal_emit_by_name(Glib::unwrap(view_), "style-updated", nullptr, nullptr);
        break;

    case TR_KEY_show_statusbar:
        status_->set_visible(gtr_pref_flag_get(key));
        break;

    case TR_KEY_show_filterbar:
        filter_->set_visible(gtr_pref_flag_get(key));
        break;

    case TR_KEY_show_toolbar:
        toolbar_->set_visible(gtr_pref_flag_get(key));
        break;

    case TR_KEY_statusbar_stats:
        refresh();
        break;

    case TR_KEY_alt_speed_enabled:
    case TR_KEY_alt_speed_up:
    case TR_KEY_alt_speed_down:
        syncAltSpeedButton();
        break;

    default:
        break;
    }
}

MainWindow::Impl::~Impl()
{
    pref_handler_id_.disconnect();
}

void MainWindow::Impl::onYinYangClicked(Gtk::Button* button)
{
    status_menu_->popup_at_widget(button, Gdk::GRAVITY_NORTH_EAST, Gdk::GRAVITY_SOUTH_EAST, nullptr);
}

void MainWindow::Impl::status_menu_toggled_cb(Gtk::CheckMenuItem* menu_item, std::string const& val)
{
    if (menu_item->get_active())
    {
        core_->set_pref(TR_KEY_statusbar_stats, val);
    }
}

void MainWindow::Impl::syncAltSpeedButton()
{
    bool const b = gtr_pref_flag_get(TR_KEY_alt_speed_enabled);
    char const* const stock = b ? "alt-speed-on" : "alt-speed-off";

    alt_speed_button_->set_active(b);
    alt_speed_image_->set_from_icon_name(stock, Gtk::BuiltinIconSize::ICON_SIZE_MENU);
    alt_speed_button_->set_halign(Gtk::ALIGN_CENTER);
    alt_speed_button_->set_valign(Gtk::ALIGN_CENTER);
    alt_speed_button_->set_tooltip_text(fmt::format(
        b ? _("Click to disable Alternative Speed Limits\n ({download_speed} down, {upload_speed} up)") :
            _("Click to enable Alternative Speed Limits\n ({download_speed} down, {upload_speed} up)"),
        fmt::arg("download_speed", tr_formatter_speed_KBps(gtr_pref_int_get(TR_KEY_alt_speed_down))),
        fmt::arg("upload_speed", tr_formatter_speed_KBps(gtr_pref_int_get(TR_KEY_alt_speed_up)))));
}

void MainWindow::Impl::alt_speed_toggled_cb()
{
    core_->set_pref(TR_KEY_alt_speed_enabled, alt_speed_button_->get_active());
}

/***
****  FILTER
***/

void MainWindow::Impl::onAltSpeedToggledIdle()
{
    core_->set_pref(TR_KEY_alt_speed_enabled, tr_sessionUsesAltSpeed(core_->get_session()));
}

/***
****  Speed limit menu
***/

void MainWindow::Impl::onSpeedToggled(Gtk::CheckMenuItem* check, tr_direction dir, bool enabled)
{
    if (check->get_active())
    {
        core_->set_pref(dir == TR_UP ? TR_KEY_speed_limit_up_enabled : TR_KEY_speed_limit_down_enabled, enabled);
    }
}

void MainWindow::Impl::onSpeedSet(tr_direction dir, int KBps)
{
    core_->set_pref(dir == TR_UP ? TR_KEY_speed_limit_up : TR_KEY_speed_limit_down, KBps);
    core_->set_pref(dir == TR_UP ? TR_KEY_speed_limit_up_enabled : TR_KEY_speed_limit_down_enabled, true);
}

Gtk::Menu* MainWindow::Impl::createSpeedMenu(tr_direction dir)
{
    auto* m = Gtk::make_managed<Gtk::Menu>();
    Gtk::RadioButtonGroup group;

    speedlimit_off_item_[dir] = Gtk::make_managed<Gtk::RadioMenuItem>(group, _("Unlimited"));
    speedlimit_off_item_[dir]->signal_toggled().connect([this, dir]()
                                                        { onSpeedToggled(speedlimit_off_item_[dir], dir, false); });
    m->append(*speedlimit_off_item_[dir]);

    speedlimit_on_item_[dir] = Gtk::make_managed<Gtk::RadioMenuItem>(group, "");
    speedlimit_on_item_[dir]->signal_toggled().connect([this, dir]() { onSpeedToggled(speedlimit_on_item_[dir], dir, true); });
    m->append(*speedlimit_on_item_[dir]);

    m->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());

    for (auto const KBps : { 50, 100, 250, 500, 1000, 2500, 5000, 10000 })
    {
        auto* w = Gtk::make_managed<Gtk::MenuItem>(tr_formatter_speed_KBps(KBps));
        w->signal_activate().connect([this, dir, KBps]() { onSpeedSet(dir, KBps); });
        m->append(*w);
    }

    return m;
}

/***
****  Speed limit menu
***/

void MainWindow::Impl::onRatioToggled(Gtk::CheckMenuItem* check, bool enabled)
{
    if (check->get_active())
    {
        core_->set_pref(TR_KEY_ratio_limit_enabled, enabled);
    }
}

void MainWindow::Impl::onRatioSet(double ratio)
{
    core_->set_pref(TR_KEY_ratio_limit, ratio);
    core_->set_pref(TR_KEY_ratio_limit_enabled, true);
}

Gtk::Menu* MainWindow::Impl::createRatioMenu()
{
    static double const stockRatios[] = { 0.25, 0.5, 0.75, 1, 1.5, 2, 3 };

    auto* m = Gtk::make_managed<Gtk::Menu>();
    Gtk::RadioButtonGroup group;

    ratio_off_item_ = Gtk::make_managed<Gtk::RadioMenuItem>(group, _("Seed Forever"));
    ratio_off_item_->signal_toggled().connect([this]() { onRatioToggled(ratio_off_item_, false); });
    m->append(*ratio_off_item_);

    ratio_on_item_ = Gtk::make_managed<Gtk::RadioMenuItem>(group, "");
    ratio_on_item_->signal_toggled().connect([this]() { onRatioToggled(ratio_on_item_, true); });
    m->append(*ratio_on_item_);

    m->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());

    for (auto const ratio : stockRatios)
    {
        auto* w = Gtk::make_managed<Gtk::MenuItem>(tr_strlratio(ratio));
        w->signal_activate().connect([this, ratio]() { onRatioSet(ratio); });
        m->append(*w);
    }

    return m;
}

/***
****  Option menu
***/

Gtk::Menu* MainWindow::Impl::createOptionsMenu()
{
    Gtk::MenuItem* m;
    auto* top = Gtk::make_managed<Gtk::Menu>();

    m = Gtk::make_managed<Gtk::MenuItem>(_("Limit Download Speed"));
    m->set_submenu(*createSpeedMenu(TR_DOWN));
    top->append(*m);

    m = Gtk::make_managed<Gtk::MenuItem>(_("Limit Upload Speed"));
    m->set_submenu(*createSpeedMenu(TR_UP));
    top->append(*m);

    top->append(*Gtk::make_managed<Gtk::SeparatorMenuItem>());

    m = Gtk::make_managed<Gtk::MenuItem>(_("Stop Seeding at Ratio"));
    m->set_submenu(*createRatioMenu());
    top->append(*m);

    top->show_all();
    return top;
}

void MainWindow::Impl::onOptionsClicked(Gtk::Button* button)
{
    gtr_label_set_text(
        *static_cast<Gtk::Label*>(speedlimit_on_item_[TR_DOWN]->get_child()),
        tr_formatter_speed_KBps(gtr_pref_int_get(TR_KEY_speed_limit_down)));

    (gtr_pref_flag_get(TR_KEY_speed_limit_down_enabled) ? speedlimit_on_item_[TR_DOWN] : speedlimit_off_item_[TR_DOWN])
        ->set_active(true);

    gtr_label_set_text(
        *static_cast<Gtk::Label*>(speedlimit_on_item_[TR_UP]->get_child()),
        tr_formatter_speed_KBps(gtr_pref_int_get(TR_KEY_speed_limit_up)));

    (gtr_pref_flag_get(TR_KEY_speed_limit_up_enabled) ? speedlimit_on_item_[TR_UP] : speedlimit_off_item_[TR_UP])
        ->set_active(true);

    gtr_label_set_text(
        *static_cast<Gtk::Label*>(ratio_on_item_->get_child()),
        fmt::format(_("Stop at Ratio ({ratio})"), fmt::arg("ratio", tr_strlratio(gtr_pref_double_get(TR_KEY_ratio_limit)))));

    (gtr_pref_flag_get(TR_KEY_ratio_limit_enabled) ? ratio_on_item_ : ratio_off_item_)->set_active(true);

    options_menu_->popup_at_widget(button, Gdk::GRAVITY_NORTH_WEST, Gdk::GRAVITY_SOUTH_WEST, nullptr);
}

/***
****  PUBLIC
***/

std::unique_ptr<MainWindow> MainWindow::create(
    Gtk::Application& app,
    Glib::RefPtr<Gio::ActionGroup> const& actions,
    Glib::RefPtr<Session> const& core)
{
    return std::unique_ptr<MainWindow>(new MainWindow(app, actions, core));
}

MainWindow::MainWindow(Gtk::Application& app, Glib::RefPtr<Gio::ActionGroup> const& actions, Glib::RefPtr<Session> const& core)
    : Gtk::ApplicationWindow()
    , impl_(std::make_unique<Impl>(*this, actions, core))
{
    app.add_window(*this);
}

MainWindow::~MainWindow() = default;

MainWindow::Impl::Impl(MainWindow& window, Glib::RefPtr<Gio::ActionGroup> const& actions, Glib::RefPtr<Session> const& core)
    : window_(window)
    , core_(core)
{
    static struct
    {
        char const* val;
        char const* i18n;
    } const stats_modes[] = {
        { "total-ratio", N_("Total Ratio") },
        { "session-ratio", N_("Session Ratio") },
        { "total-transfer", N_("Total Transfer") },
        { "session-transfer", N_("Session Transfer") },
    };

    /* make the window */
    window.set_title(Glib::get_application_name());
    window.set_role("tr-main");
    window.set_default_size(gtr_pref_int_get(TR_KEY_main_window_width), gtr_pref_int_get(TR_KEY_main_window_height));
    window.move(gtr_pref_int_get(TR_KEY_main_window_x), gtr_pref_int_get(TR_KEY_main_window_y));

    if (gtr_pref_flag_get(TR_KEY_main_window_is_maximized))
    {
        window.maximize();
    }

    window.insert_action_group("win", actions);
    /* Add style provider to the window. */
    /* Please move it to separate .css file if you’re adding more styles here. */
    auto const* style = ".tr-workarea.frame {border-left-width: 0; border-right-width: 0; border-radius: 0;}";
    auto css_provider = Gtk::CssProvider::create();
    css_provider->load_from_data(style);
    Gtk::StyleContext::add_provider_for_screen(
        Gdk::Screen::get_default(),
        css_provider,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    /* window's main container */
    auto* vbox = Gtk::make_managed<Gtk::Box>(Gtk::ORIENTATION_VERTICAL, 0);
    window.add(*vbox);

    /* toolbar */
    toolbar_ = gtr_action_get_widget<Gtk::Toolbar>("main-window-toolbar");
    toolbar_->get_style_context()->add_class(GTK_STYLE_CLASS_PRIMARY_TOOLBAR);

    /* filter */
    filter_ = Gtk::make_managed<FilterBar>(core_->get_session(), core_->get_model());
    filter_->set_border_width(GUI_PAD_SMALL);

    /* status menu */
    status_menu_ = Gtk::make_managed<Gtk::Menu>();
    Gtk::RadioButtonGroup stats_modes_group;
    auto const pch = gtr_pref_string_get(TR_KEY_statusbar_stats);

    for (auto const& mode : stats_modes)
    {
        auto* w = Gtk::make_managed<Gtk::RadioMenuItem>(stats_modes_group, _(mode.i18n));
        w->set_active(mode.val == pch);
        w->signal_toggled().connect([this, w, val = mode.val]() { status_menu_toggled_cb(w, val); });
        status_menu_->append(*w);
        w->show();
    }

    /**
    *** Statusbar
    **/

    status_ = Gtk::make_managed<Gtk::Grid>();
    status_->set_orientation(Gtk::ORIENTATION_HORIZONTAL);
    status_->set_border_width(GUI_PAD_SMALL);

    /* gear */
    auto* gear_button = Gtk::make_managed<Gtk::Button>();
    gear_button->add(*Gtk::make_managed<Gtk::Image>("preferences-other", Gtk::ICON_SIZE_MENU));
    gear_button->set_tooltip_text(_("Options"));
    gear_button->set_relief(Gtk::RELIEF_NONE);
    options_menu_ = createOptionsMenu();
    gear_button->signal_clicked().connect([this, gear_button]() { onOptionsClicked(gear_button); });
    status_->add(*gear_button);

    /* turtle */
    alt_speed_image_ = Gtk::make_managed<Gtk::Image>();
    alt_speed_button_ = Gtk::make_managed<Gtk::ToggleButton>();
    alt_speed_button_->set_image(*alt_speed_image_);
    alt_speed_button_->set_relief(Gtk::RELIEF_NONE);
    alt_speed_button_->signal_toggled().connect(sigc::mem_fun(*this, &Impl::alt_speed_toggled_cb));
    status_->add(*alt_speed_button_);

    /* spacer */
    auto* w = Gtk::make_managed<Gtk::Fixed>();
    w->set_hexpand(true);
    status_->add(*w);

    /* download */
    dl_lb_ = Gtk::make_managed<Gtk::Label>();
    dl_lb_->set_single_line_mode(true);
    status_->add(*dl_lb_);

    /* upload */
    ul_lb_ = Gtk::make_managed<Gtk::Label>();
    ul_lb_->set_margin_start(GUI_PAD);
    ul_lb_->set_single_line_mode(true);
    status_->add(*ul_lb_);

    /* ratio */
    stats_lb_ = Gtk::make_managed<Gtk::Label>();
    stats_lb_->set_margin_start(GUI_PAD_BIG);
    stats_lb_->set_single_line_mode(true);
    status_->add(*stats_lb_);

    /* ratio selector */
    auto* ratio_button = Gtk::make_managed<Gtk::Button>();
    ratio_button->set_tooltip_text(_("Statistics"));
    ratio_button->add(*Gtk::make_managed<Gtk::Image>("ratio", Gtk::ICON_SIZE_MENU));
    ratio_button->set_relief(Gtk::RELIEF_NONE);
    ratio_button->signal_clicked().connect([this, ratio_button]() { onYinYangClicked(ratio_button); });
    status_->add(*ratio_button);

    /**
    *** Workarea
    **/

    view_ = makeview(filter_->get_filter_model());
    scroll_ = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll_->set_policy(Gtk::POLICY_NEVER, Gtk::POLICY_AUTOMATIC);
    scroll_->set_shadow_type(Gtk::SHADOW_OUT);
    scroll_->get_style_context()->add_class("tr-workarea");
    scroll_->add(*view_);

    /* lay out the widgets */
    vbox->pack_start(*toolbar_, false, false);
    vbox->pack_start(*filter_, false, false);
    vbox->pack_start(*scroll_, true, true);
    vbox->pack_start(*status_, false, false);

    {
        /* this is to determine the maximum width/height for the label */
        int width = 0;
        int height = 0;
        auto const pango_layout = ul_lb_->create_pango_layout("999.99 kB/s");
        pango_layout->get_pixel_size(width, height);
        ul_lb_->set_size_request(width, height);
        dl_lb_->set_size_request(width, height);
        ul_lb_->set_halign(Gtk::ALIGN_END);
        ul_lb_->set_valign(Gtk::ALIGN_CENTER);
        dl_lb_->set_halign(Gtk::ALIGN_END);
        dl_lb_->set_valign(Gtk::ALIGN_CENTER);
    }

    /* show all but the window */
    vbox->show_all();

    /* listen for prefs changes that affect the window */
    prefsChanged(TR_KEY_compact_view);
    prefsChanged(TR_KEY_show_filterbar);
    prefsChanged(TR_KEY_show_statusbar);
    prefsChanged(TR_KEY_statusbar_stats);
    prefsChanged(TR_KEY_show_toolbar);
    prefsChanged(TR_KEY_alt_speed_enabled);
    pref_handler_id_ = core_->signal_prefs_changed().connect(sigc::mem_fun(*this, &Impl::prefsChanged));

    tr_sessionSetAltSpeedFunc(
        core_->get_session(),
        [](tr_session* /*s*/, bool /*isEnabled*/, bool /*byUser*/, gpointer p)
        { Glib::signal_idle().connect_once([p]() { static_cast<Impl*>(p)->onAltSpeedToggledIdle(); }); },
        this);

    refresh();
}

void MainWindow::Impl::updateStats()
{
    Glib::ustring buf;
    tr_session_stats stats;
    auto const* const session = core_->get_session();

    /* update the stats */
    if (auto const pch = gtr_pref_string_get(TR_KEY_statusbar_stats); pch == "session-ratio")
    {
        tr_sessionGetStats(session, &stats);
        buf = fmt::format(_("Ratio: {ratio}"), fmt::arg("ratio", tr_strlratio(stats.ratio)));
    }
    else if (pch == "session-transfer")
    {
        tr_sessionGetStats(session, &stats);
        buf = fmt::format(
            C_("current session totals", "Down: {downloaded_size}, Up: {uploaded_size}"),
            fmt::arg("downloaded_size", tr_strlsize(stats.downloadedBytes)),
            fmt::arg("uploaded_size", tr_strlsize(stats.uploadedBytes)));
    }
    else if (pch == "total-transfer")
    {
        tr_sessionGetCumulativeStats(session, &stats);
        buf = fmt::format(
            C_("all-time totals", "Down: {downloaded_size}, Up: {uploaded_size}"),
            fmt::arg("downloaded_size", tr_strlsize(stats.downloadedBytes)),
            fmt::arg("uploaded_size", tr_strlsize(stats.uploadedBytes)));
    }
    else /* default is total-ratio */
    {
        tr_sessionGetCumulativeStats(session, &stats);
        buf = fmt::format(_("Ratio: {ratio}"), fmt::arg("ratio", tr_strlratio(stats.ratio)));
    }

    stats_lb_->set_text(buf);
}

void MainWindow::Impl::updateSpeeds()
{
    auto const* const session = core_->get_session();

    if (session != nullptr)
    {
        auto dn_count = int{};
        auto dn_speed = double{};
        auto up_count = int{};
        auto up_speed = double{};

        auto const model = core_->get_model();
        for (auto const& row : model->children())
        {
            dn_count += row.get_value(torrent_cols.active_peers_down);
            dn_speed += row.get_value(torrent_cols.speed_down);
            up_count += row.get_value(torrent_cols.active_peers_up);
            up_speed += row.get_value(torrent_cols.speed_up);
        }

        dl_lb_->set_text(fmt::format(_("{download_speed} ▼"), fmt::arg("download_speed", tr_formatter_speed_KBps(dn_speed))));
        dl_lb_->set_visible(dn_count > 0);

        ul_lb_->set_text(fmt::format(_("{upload_speed} ▲"), fmt::arg("upload_speed", tr_formatter_speed_KBps(up_speed))));
        ul_lb_->set_visible(dn_count > 0 || up_count > 0);
    }
}

void MainWindow::refresh()
{
    impl_->refresh();
}

void MainWindow::Impl::refresh()
{
    if (core_ != nullptr && core_->get_session() != nullptr)
    {
        updateSpeeds();
        updateStats();
    }
}

Glib::RefPtr<Gtk::TreeSelection> MainWindow::get_selection() const
{
    return impl_->get_selection();
}

Glib::RefPtr<Gtk::TreeSelection> MainWindow::Impl::get_selection() const
{
    return selection_;
}

void MainWindow::set_busy(bool isBusy)
{
    if (get_realized())
    {
        auto const display = get_display();
        auto const cursor = isBusy ? Gdk::Cursor::create(display, Gdk::WATCH) : Glib::RefPtr<Gdk::Cursor>();

        get_window()->set_cursor(cursor);
        display->flush();
    }
}
