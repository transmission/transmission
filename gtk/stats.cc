/*
 * This file Copyright (C) 2007-2021 Mnemosyne LLC
 *
 * It may be used under the GNU GPL versions 2 or 3
 * or any future license endorsed by Mnemosyne LLC.
 *
 */

#include <glibmm.h>
#include <glibmm/i18n.h>

#include "hig.h"
#include "stats.h"
#include "tr-core.h"
#include "tr-prefs.h"
#include "util.h"

enum
{
    TR_RESPONSE_RESET = 1
};

class StatsDialog::Impl
{
public:
    Impl(StatsDialog& dialog, TrCore* core);
    ~Impl();

private:
    bool updateStats();
    void dialogResponse(int response);

private:
    StatsDialog& dialog_;
    TrCore* const core_;

    Gtk::Label* one_up_lb_;
    Gtk::Label* one_down_lb_;
    Gtk::Label* one_ratio_lb_;
    Gtk::Label* one_time_lb_;

    Gtk::Label* all_up_lb_;
    Gtk::Label* all_down_lb_;
    Gtk::Label* all_ratio_lb_;
    Gtk::Label* all_time_lb_;

    Gtk::Label* all_sessions_lb_;

    sigc::connection update_stats_tag_;
};

namespace
{

void setLabel(Gtk::Label* l, Glib::ustring const& str)
{
    gtr_label_set_text(Glib::unwrap(l), str.c_str());
}

void setLabelFromRatio(Gtk::Label* l, double d)
{
    char buf[128];
    tr_strlratio(buf, d, sizeof(buf));
    setLabel(l, buf);
}

} // namespace

bool StatsDialog::Impl::updateStats()
{
    char buf[128];
    tr_session_stats one;
    tr_session_stats all;
    auto const buflen = sizeof(buf);

    tr_sessionGetStats(gtr_core_session(core_), &one);
    tr_sessionGetCumulativeStats(gtr_core_session(core_), &all);

    setLabel(one_up_lb_, tr_strlsize(buf, one.uploadedBytes, buflen));
    setLabel(one_down_lb_, tr_strlsize(buf, one.downloadedBytes, buflen));
    setLabel(one_time_lb_, tr_strltime(buf, one.secondsActive, buflen));
    setLabelFromRatio(one_ratio_lb_, one.ratio);

    setLabel(
        all_sessions_lb_,
        Glib::ustring::sprintf(
            ngettext("Started %'d time", "Started %'d times", (int)all.sessionCount),
            (int)all.sessionCount));

    setLabel(all_up_lb_, tr_strlsize(buf, all.uploadedBytes, buflen));
    setLabel(all_down_lb_, tr_strlsize(buf, all.downloadedBytes, buflen));
    setLabel(all_time_lb_, tr_strltime(buf, all.secondsActive, buflen));
    setLabelFromRatio(all_ratio_lb_, all.ratio);

    return true;
}

StatsDialog::Impl::~Impl()
{
    update_stats_tag_.disconnect();
}

void StatsDialog::Impl::dialogResponse(int response)
{
    if (response == TR_RESPONSE_RESET)
    {
        Gtk::MessageDialog w(dialog_, _("Reset your statistics?"), false, Gtk::MESSAGE_QUESTION, Gtk::BUTTONS_NONE, true);
        w.add_button(_("_Cancel"), Gtk::RESPONSE_CANCEL);
        w.add_button(_("_Reset"), TR_RESPONSE_RESET);
        w.set_secondary_text(
            _("These statistics are for your information only. "
              "Resetting them doesn't affect the statistics logged by your BitTorrent trackers."));

        if (w.run() == TR_RESPONSE_RESET)
        {
            tr_sessionClearStats(gtr_core_session(core_));
            updateStats();
        }
    }

    if (response == Gtk::RESPONSE_CLOSE)
    {
        dialog_.hide();
    }
}

std::unique_ptr<StatsDialog> StatsDialog::create(Gtk::Window& parent, TrCore* core)
{
    return std::unique_ptr<StatsDialog>(new StatsDialog(parent, core));
}

StatsDialog::StatsDialog(Gtk::Window& parent, TrCore* core)
    : Gtk::Dialog(_("Statistics"), parent)
    , impl_(std::make_unique<Impl>(*this, core))
{
}

StatsDialog::~StatsDialog() = default;

StatsDialog::Impl::Impl(StatsDialog& dialog, TrCore* core)
    : dialog_(dialog)
    , core_(core)
{
    guint row = 0;

    dialog_.add_button(_("_Reset"), TR_RESPONSE_RESET);
    dialog_.add_button(_("_Close"), Gtk::RESPONSE_CLOSE);
    dialog_.set_default_response(Gtk::RESPONSE_CLOSE);

    auto* t = Glib::wrap(hig_workarea_create());
    hig_workarea_add_section_title(Glib::unwrap(t), &row, _("Current Session"));

    one_up_lb_ = Gtk::make_managed<Gtk::Label>();
    one_up_lb_->set_single_line_mode(true);
    hig_workarea_add_row(Glib::unwrap(t), &row, _("Uploaded:"), Glib::unwrap(static_cast<Gtk::Widget*>(one_up_lb_)), nullptr);
    one_down_lb_ = Gtk::make_managed<Gtk::Label>();
    one_down_lb_->set_single_line_mode(true);
    hig_workarea_add_row(
        Glib::unwrap(t),
        &row,
        _("Downloaded:"),
        Glib::unwrap(static_cast<Gtk::Widget*>(one_down_lb_)),
        nullptr);
    one_ratio_lb_ = Gtk::make_managed<Gtk::Label>();
    one_ratio_lb_->set_single_line_mode(true);
    hig_workarea_add_row(Glib::unwrap(t), &row, _("Ratio:"), Glib::unwrap(static_cast<Gtk::Widget*>(one_ratio_lb_)), nullptr);
    one_time_lb_ = Gtk::make_managed<Gtk::Label>();
    one_time_lb_->set_single_line_mode(true);
    hig_workarea_add_row(Glib::unwrap(t), &row, _("Duration:"), Glib::unwrap(static_cast<Gtk::Widget*>(one_time_lb_)), nullptr);

    hig_workarea_add_section_divider(Glib::unwrap(t), &row);
    hig_workarea_add_section_title(Glib::unwrap(t), &row, _("Total"));

    all_sessions_lb_ = Gtk::make_managed<Gtk::Label>(_("Started %'d time"));
    all_sessions_lb_->set_single_line_mode(true);
    hig_workarea_add_label_w(Glib::unwrap(t), row, Glib::unwrap(static_cast<Gtk::Widget*>(all_sessions_lb_)));
    ++row;

    all_up_lb_ = Gtk::make_managed<Gtk::Label>();
    all_up_lb_->set_single_line_mode(true);
    hig_workarea_add_row(Glib::unwrap(t), &row, _("Uploaded:"), Glib::unwrap(static_cast<Gtk::Widget*>(all_up_lb_)), nullptr);
    all_down_lb_ = Gtk::make_managed<Gtk::Label>();
    all_down_lb_->set_single_line_mode(true);
    hig_workarea_add_row(
        Glib::unwrap(t),
        &row,
        _("Downloaded:"),
        Glib::unwrap(static_cast<Gtk::Widget*>(all_down_lb_)),
        nullptr);
    all_ratio_lb_ = Gtk::make_managed<Gtk::Label>();
    all_ratio_lb_->set_single_line_mode(true);
    hig_workarea_add_row(Glib::unwrap(t), &row, _("Ratio:"), Glib::unwrap(static_cast<Gtk::Widget*>(all_ratio_lb_)), nullptr);
    all_time_lb_ = Gtk::make_managed<Gtk::Label>();
    all_time_lb_->set_single_line_mode(true);
    hig_workarea_add_row(Glib::unwrap(t), &row, _("Duration:"), Glib::unwrap(static_cast<Gtk::Widget*>(all_time_lb_)), nullptr);

    gtr_dialog_set_content(Glib::unwrap(&dialog_), Glib::unwrap(t));

    updateStats();
    dialog_.signal_response().connect(sigc::mem_fun(this, &Impl::dialogResponse));
    update_stats_tag_ = Glib::signal_timeout().connect_seconds(
        sigc::mem_fun(this, &Impl::updateStats),
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);
}
