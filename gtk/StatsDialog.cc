// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <memory>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <fmt/core.h>

#include "HigWorkarea.h"
#include "PrefsDialog.h"
#include "Session.h"
#include "StatsDialog.h"
#include "Utils.h"

static auto constexpr TR_RESPONSE_RESET = int{ 1 };

class StatsDialog::Impl
{
public:
    Impl(StatsDialog& dialog, Glib::RefPtr<Session> const& core);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

private:
    bool updateStats();
    void dialogResponse(int response);

private:
    StatsDialog& dialog_;
    Glib::RefPtr<Session> const core_;

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
    gtr_label_set_text(*l, str);
}

void setLabelFromRatio(Gtk::Label* l, double d)
{
    setLabel(l, tr_strlratio(d));
}

auto startedTimesText(uint64_t n)
{
    return fmt::format(ngettext("Started {count:L} time", "Started {count:L} times", n), fmt::arg("count", n));
}

} // namespace

bool StatsDialog::Impl::updateStats()
{
    tr_session_stats one;
    tr_session_stats all;

    tr_sessionGetStats(core_->get_session(), &one);
    tr_sessionGetCumulativeStats(core_->get_session(), &all);

    setLabel(one_up_lb_, tr_strlsize(one.uploadedBytes));
    setLabel(one_down_lb_, tr_strlsize(one.downloadedBytes));
    setLabel(one_time_lb_, tr_strltime(one.secondsActive));
    setLabelFromRatio(one_ratio_lb_, one.ratio);

    setLabel(all_sessions_lb_, startedTimesText(all.sessionCount));
    setLabel(all_up_lb_, tr_strlsize(all.uploadedBytes));
    setLabel(all_down_lb_, tr_strlsize(all.downloadedBytes));
    setLabel(all_time_lb_, tr_strltime(all.secondsActive));
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
            tr_sessionClearStats(core_->get_session());
            updateStats();
        }
    }

    if (response == Gtk::RESPONSE_CLOSE)
    {
        dialog_.hide();
    }
}

std::unique_ptr<StatsDialog> StatsDialog::create(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
{
    return std::unique_ptr<StatsDialog>(new StatsDialog(parent, core));
}

StatsDialog::StatsDialog(Gtk::Window& parent, Glib::RefPtr<Session> const& core)
    : Gtk::Dialog(_("Statistics"), parent)
    , impl_(std::make_unique<Impl>(*this, core))
{
}

StatsDialog::~StatsDialog() = default;

StatsDialog::Impl::Impl(StatsDialog& dialog, Glib::RefPtr<Session> const& core)
    : dialog_(dialog)
    , core_(core)
{
    guint row = 0;

    dialog_.add_button(_("_Reset"), TR_RESPONSE_RESET);
    dialog_.add_button(_("_Close"), Gtk::RESPONSE_CLOSE);
    dialog_.set_default_response(Gtk::RESPONSE_CLOSE);

    auto* t = Gtk::make_managed<HigWorkarea>();
    t->add_section_title(row, _("Current Session"));

    one_up_lb_ = Gtk::make_managed<Gtk::Label>();
    one_up_lb_->set_single_line_mode(true);
    t->add_row(row, _("Uploaded:"), *one_up_lb_);
    one_down_lb_ = Gtk::make_managed<Gtk::Label>();
    one_down_lb_->set_single_line_mode(true);
    t->add_row(row, _("Downloaded:"), *one_down_lb_);
    one_ratio_lb_ = Gtk::make_managed<Gtk::Label>();
    one_ratio_lb_->set_single_line_mode(true);
    t->add_row(row, _("Ratio:"), *one_ratio_lb_);
    one_time_lb_ = Gtk::make_managed<Gtk::Label>();
    one_time_lb_->set_single_line_mode(true);
    t->add_row(row, _("Duration:"), *one_time_lb_);

    t->add_section_divider(row);
    t->add_section_title(row, _("Total"));

    all_sessions_lb_ = Gtk::make_managed<Gtk::Label>(startedTimesText(1));
    all_sessions_lb_->set_single_line_mode(true);
    t->add_label_w(row, *all_sessions_lb_);
    ++row;

    all_up_lb_ = Gtk::make_managed<Gtk::Label>();
    all_up_lb_->set_single_line_mode(true);
    t->add_row(row, _("Uploaded:"), *all_up_lb_);
    all_down_lb_ = Gtk::make_managed<Gtk::Label>();
    all_down_lb_->set_single_line_mode(true);
    t->add_row(row, _("Downloaded:"), *all_down_lb_);
    all_ratio_lb_ = Gtk::make_managed<Gtk::Label>();
    all_ratio_lb_->set_single_line_mode(true);
    t->add_row(row, _("Ratio:"), *all_ratio_lb_);
    all_time_lb_ = Gtk::make_managed<Gtk::Label>();
    all_time_lb_->set_single_line_mode(true);
    t->add_row(row, _("Duration:"), *all_time_lb_);

    gtr_dialog_set_content(dialog_, *t);

    updateStats();
    dialog_.signal_response().connect(sigc::mem_fun(*this, &Impl::dialogResponse));
    update_stats_tag_ = Glib::signal_timeout().connect_seconds(
        sigc::mem_fun(*this, &Impl::updateStats),
        SECONDARY_WINDOW_REFRESH_INTERVAL_SECONDS);
}
