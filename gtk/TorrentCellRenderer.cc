// This file Copyright © 2007-2022 Mnemosyne LLC.
// It may be used under GPLv2 (SPDX: GPL-2.0-only), GPLv3 (SPDX: GPL-3.0-only),
// or any future license endorsed by Mnemosyne LLC.
// License text can be found in the licenses/ folder.

#include <climits> /* INT_MAX */
#include <cstring> // strchr()
#include <memory>
#include <optional>
#include <string>

#include <glibmm.h>
#include <glibmm/i18n.h>

#include <fmt/core.h>

#include <libtransmission/transmission.h>
#include <libtransmission/utils.h> /* tr_truncd() */

#include "HigWorkarea.h"
#include "IconCache.h"
#include "TorrentCellRenderer.h"
#include "Utils.h"

/* #define TEST_RTL */

/***
****
***/

namespace
{

auto const DefaultBarHeight = 12;
auto const CompactBarWidth = 50;
auto const SmallScale = 0.9;
auto const CompactIconSize = Gtk::ICON_SIZE_MENU;
auto const FullIconSize = Gtk::ICON_SIZE_DND;

auto getProgressString(tr_torrent const* tor, uint64_t total_size, tr_stat const* st)
{
    Glib::ustring gstr;

    bool const isDone = st->leftUntilDone == 0;
    uint64_t const haveTotal = st->haveUnchecked + st->haveValid;
    bool const isSeed = st->haveValid >= total_size;
    double seedRatio;
    bool const hasSeedRatio = tr_torrentGetSeedRatio(tor, &seedRatio);

    if (!isDone) // downloading
    {
        // 50 MB of 200 MB (25%)
        gstr += fmt::format(
            _("{current_size} of {complete_size} ({percent_done}%)"),
            fmt::arg("current_size", tr_strlsize(haveTotal)),
            fmt::arg("complete_size", tr_strlsize(st->sizeWhenDone)),
            fmt::arg("percent_done", tr_strpercent(st->percentDone * 100.0)));
    }
    else if (!isSeed && hasSeedRatio) // partial seed, seed ratio
    {
        // 50 MB of 200 MB (25%), uploaded 30 MB (Ratio: X%, Goal: Y%)
        gstr += fmt::format(
            _("{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio}, Goal: {seed_ratio})"),
            fmt::arg("current_size", tr_strlsize(haveTotal)),
            fmt::arg("complete_size", tr_strlsize(total_size)),
            fmt::arg("percent_complete", tr_strpercent(st->percentComplete * 100.0)),
            fmt::arg("uploaded_size", tr_strlsize(st->uploadedEver)),
            fmt::arg("ratio", tr_strlratio(st->ratio)),
            fmt::arg("seed_ratio", tr_strlratio(seedRatio)));
    }
    else if (!isSeed) // partial seed, no seed ratio
    {
        gstr += fmt::format(
            _("{current_size} of {complete_size} ({percent_complete}%), uploaded {uploaded_size} (Ratio: {ratio})"),
            fmt::arg("current_size", tr_strlsize(haveTotal)),
            fmt::arg("complete_size", tr_strlsize(total_size)),
            fmt::arg("percent_complete", tr_strpercent(st->percentComplete * 100.0)),
            fmt::arg("uploaded_size", tr_strlsize(st->uploadedEver)),
            fmt::arg("ratio", tr_strlratio(st->ratio)));
    }
    else if (hasSeedRatio) // seed, seed ratio
    {
        gstr += fmt::format(
            _("{complete_size}, uploaded {uploaded_size} (Ratio: {ratio}, Goal: {seed_ratio})"),
            fmt::arg("complete_size", tr_strlsize(total_size)),
            fmt::arg("uploaded_size", tr_strlsize(st->uploadedEver)),
            fmt::arg("ratio", tr_strlratio(st->ratio)),
            fmt::arg("seed_ratio", tr_strlratio(seedRatio)));
    }
    else // seed, no seed ratio
    {
        gstr += fmt::format(
            _("{complete_size}, uploaded {uploaded_size} (Ratio: {ratio})"),
            fmt::arg("complete_size", tr_strlsize(total_size)),
            fmt::arg("uploaded_size", tr_strlsize(st->uploadedEver)),
            fmt::arg("ratio", tr_strlratio(st->ratio)));
    }

    // add time remaining when applicable
    if (st->activity == TR_STATUS_DOWNLOAD || (hasSeedRatio && st->activity == TR_STATUS_SEED))
    {
        int const eta = st->eta;
        gstr += " - ";

        if (eta < 0)
        {
            gstr += _("Remaining time unknown");
        }
        else
        {
            gstr += fmt::format(_("{time_span} remaining"), fmt::arg("time_span", tr_strltime(eta)));
        }
    }

    return gstr;
}

std::string getShortTransferString(
    tr_torrent const* const tor,
    tr_stat const* const st,
    double uploadSpeed_KBps,
    double downloadSpeed_KBps)
{
    bool const have_meta = tr_torrentHasMetadata(tor);

    if (bool const have_down = have_meta && (st->peersSendingToUs > 0 || st->webseedsSendingToUs > 0); have_down)
    {
        return fmt::format(
            _("{download_speed} ▼  {upload_speed} ▲"),
            fmt::arg("upload_speed", tr_formatter_speed_KBps(uploadSpeed_KBps)),
            fmt::arg("download_speed", tr_formatter_speed_KBps(downloadSpeed_KBps)));
    }

    if (bool const have_up = have_meta && st->peersGettingFromUs > 0; have_up)
    {
        return fmt::format(_("{upload_speed} ▲"), fmt::arg("upload_speed", tr_formatter_speed_KBps(downloadSpeed_KBps)));
    }

    if (st->isStalled)
    {
        return _("Stalled");
    }

    return {};
}

std::string getShortStatusString(
    tr_torrent const* const tor,
    tr_stat const* const st,
    double uploadSpeed_KBps,
    double downloadSpeed_KBps)
{
    switch (st->activity)
    {
    case TR_STATUS_STOPPED:
        return st->finished ? _("Finished") : _("Paused");

    case TR_STATUS_CHECK_WAIT:
        return _("Queued for verification");

    case TR_STATUS_DOWNLOAD_WAIT:
        return _("Queued for download");

    case TR_STATUS_SEED_WAIT:
        return _("Queued for seeding");

    case TR_STATUS_CHECK:
        return fmt::format(
            _("Verifying local data ({percent_done}% tested)"),
            fmt::arg("percent_done", tr_truncd(st->recheckProgress * 100.0, 1)));

    case TR_STATUS_DOWNLOAD:
    case TR_STATUS_SEED:
        return fmt::format(
            FMT_STRING("{:s} {:s}"),
            getShortTransferString(tor, st, uploadSpeed_KBps, downloadSpeed_KBps),
            fmt::format(_("Ratio: {ratio}"), fmt::arg("ratio", tr_strlratio(st->ratio))));

    default:
        return {};
    }
}

std::optional<std::string> getErrorString(tr_stat const* st)
{
    switch (st->error)
    {
    case TR_STAT_TRACKER_WARNING:
        return fmt::format(_("Tracker warning: '{warning}'"), fmt::arg("warning", st->errorString));

    case TR_STAT_TRACKER_ERROR:
        return fmt::format(_("Tracker Error: '{error}'"), fmt::arg("error", st->errorString));

    case TR_STAT_LOCAL_ERROR:
        return fmt::format(_("Local error: '{error}'"), fmt::arg("error", st->errorString));

    default:
        return std::nullopt;
    }
}

auto getActivityString(
    tr_torrent const* const tor,
    tr_stat const* const st,
    double const uploadSpeed_KBps,
    double const downloadSpeed_KBps)
{
    switch (st->activity)
    {
    case TR_STATUS_STOPPED:
    case TR_STATUS_CHECK_WAIT:
    case TR_STATUS_CHECK:
    case TR_STATUS_DOWNLOAD_WAIT:
    case TR_STATUS_SEED_WAIT:
        return getShortStatusString(tor, st, uploadSpeed_KBps, downloadSpeed_KBps);

    case TR_STATUS_DOWNLOAD:
        if (!tr_torrentHasMetadata(tor))
        {
            return fmt::format(
                ngettext(
                    "Downloading metadata from {active_count} connected peer ({percent_done:d}% done)",
                    "Downloading metadata from {active_count} connected peers ({percent_done:d}% done)",
                    st->peersConnected),
                fmt::arg("active_count", st->peersConnected),
                fmt::arg("percent_done", tr_strpercent(st->metadataPercentComplete * 100.0)));
        }

        if (st->peersSendingToUs != 0 && st->webseedsSendingToUs != 0)
        {
            return fmt::format(
                ngettext(
                    "Downloading from {active_count} of {connected_count} connected peer and webseed",
                    "Downloading from {active_count} of {connected_count} connected peers and webseeds",
                    st->peersConnected + st->webseedsSendingToUs),
                fmt::arg("active_count", st->peersSendingToUs + st->webseedsSendingToUs),
                fmt::arg("connected_count", st->peersConnected + st->webseedsSendingToUs));
        }

        if (st->webseedsSendingToUs != 0)
        {
            return fmt::format(
                ngettext(
                    "Downloading from {active_count} webseed",
                    "Downloading from {active_count} webseeds",
                    st->webseedsSendingToUs),
                fmt::arg("active_count", st->webseedsSendingToUs));
        }

        return fmt::format(
            ngettext(
                "Downloading from {active_count} of {connected_count} connected peer",
                "Downloading from {active_count} of {connected_count} connected peers",
                st->peersConnected),
            fmt::arg("active_count", st->peersSendingToUs),
            fmt::arg("connected_count", st->peersConnected));

    case TR_STATUS_SEED:
        return fmt::format(
            ngettext(
                "Seeding to {active_count} of {connected_count} connected peer",
                "Seeding to {active_count} of {connected_count} connected peers",
                st->peersConnected),
            fmt::arg("active_count", st->peersGettingFromUs),
            fmt::arg("connected_count", st->peersConnected));

    default:
        g_assert_not_reached();
        return std::string{};
    }
}

std::string getStatusString(
    tr_torrent const* tor,
    tr_stat const* st,
    double const uploadSpeed_KBps,
    double const downloadSpeed_KBps)
{
    auto status_str = getErrorString(st).value_or(getActivityString(tor, st, uploadSpeed_KBps, downloadSpeed_KBps));

    if (st->activity != TR_STATUS_CHECK_WAIT && st->activity != TR_STATUS_CHECK && st->activity != TR_STATUS_DOWNLOAD_WAIT &&
        st->activity != TR_STATUS_SEED_WAIT && st->activity != TR_STATUS_STOPPED)
    {
        if (auto const buf = getShortTransferString(tor, st, uploadSpeed_KBps, downloadSpeed_KBps); !std::empty(buf))
        {
            status_str += fmt::format(FMT_STRING(" - {:s}"), buf);
        }
    }

    return status_str;
}

} // namespace

/***
****
***/

class TorrentCellRenderer::Impl
{
public:
    explicit Impl(TorrentCellRenderer& renderer);
    ~Impl();

    TR_DISABLE_COPY_MOVE(Impl)

    void get_size_compact(Gtk::Widget& widget, int& width, int& height) const;
    void get_size_full(Gtk::Widget& widget, int& width, int& height) const;

    void render_compact(
        Cairo::RefPtr<Cairo::Context> const& cr,
        Gtk::Widget& widget,
        Gdk::Rectangle const& background_area,
        Gtk::CellRendererState flags);
    void render_full(
        Cairo::RefPtr<Cairo::Context> const& cr,
        Gtk::Widget& widget,
        Gdk::Rectangle const& background_area,
        Gtk::CellRendererState flags);

public:
    Glib::Property<gpointer> torrent;
    Glib::Property<int> bar_height;

    /* Use this instead of tr_stat.pieceUploadSpeed so that the model can
       control when the speed displays get updated. This is done to keep
       the individual torrents' speeds and the status bar's overall speed
       in sync even if they refresh at slightly different times */
    Glib::Property<double> upload_speed_KBps;

    /* @see upload_speed_Bps */
    Glib::Property<double> download_speed_KBps;

    Glib::Property<bool> compact;

private:
    TorrentCellRenderer& renderer_;

    Gtk::CellRendererText* text_renderer_ = nullptr;
    Gtk::CellRendererProgress* progress_renderer_ = nullptr;
    Gtk::CellRendererPixbuf* icon_renderer_ = nullptr;
};

/***
****
***/

namespace
{

Glib::RefPtr<Gdk::Pixbuf> get_icon(tr_torrent const* tor, Gtk::IconSize icon_size, Gtk::Widget& for_widget)
{
    auto mime_type = std::string_view{};

    if (auto const n_files = tr_torrentFileCount(tor); n_files == 0)
    {
        mime_type = UnknownMimeType;
    }
    else if (n_files > 1)
    {
        mime_type = DirectoryMimeType;
    }
    else
    {
        auto const* const name = tr_torrentFile(tor, 0).name;

        mime_type = strchr(name, '/') != nullptr ? DirectoryMimeType : tr_get_mime_type_for_filename(name);
    }

    return gtr_get_mime_type_icon(mime_type, icon_size, for_widget);
}

} // namespace

/***
****
***/

void TorrentCellRenderer::Impl::get_size_compact(Gtk::Widget& widget, int& width, int& height) const
{
    int xpad;
    int ypad;
    Gtk::Requisition min_size;
    Gtk::Requisition icon_size;
    Gtk::Requisition name_size;
    Gtk::Requisition stat_size;

    auto* const tor = static_cast<tr_torrent*>(torrent.get_value());
    auto const* const st = tr_torrentStatCached(tor);

    auto const icon = get_icon(tor, CompactIconSize, widget);
    auto const name = Glib::ustring(tr_torrentName(tor));
    auto const gstr_stat = getShortStatusString(tor, st, upload_speed_KBps.get_value(), download_speed_KBps.get_value());
    renderer_.get_padding(xpad, ypad);

    /* get the idealized cell dimensions */
    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->get_preferred_size(widget, min_size, icon_size);
    text_renderer_->property_text() = name;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_NONE;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->get_preferred_size(widget, min_size, name_size);
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->get_preferred_size(widget, min_size, stat_size);

    /**
    *** LAYOUT
    **/

    width = xpad * 2 + icon_size.width + GUI_PAD + CompactBarWidth + GUI_PAD + stat_size.width;
    height = ypad * 2 + std::max(name_size.height, bar_height.get_value());
}

void TorrentCellRenderer::Impl::get_size_full(Gtk::Widget& widget, int& width, int& height) const
{
    int xpad;
    int ypad;
    Gtk::Requisition min_size;
    Gtk::Requisition icon_size;
    Gtk::Requisition name_size;
    Gtk::Requisition stat_size;
    Gtk::Requisition prog_size;

    auto* const tor = static_cast<tr_torrent*>(torrent.get_value());
    auto const* const st = tr_torrentStatCached(tor);
    auto const total_size = tr_torrentTotalSize(tor);

    auto const icon = get_icon(tor, FullIconSize, widget);
    auto const name = Glib::ustring(tr_torrentName(tor));
    auto const gstr_stat = getStatusString(tor, st, upload_speed_KBps.get_value(), download_speed_KBps.get_value());
    auto const gstr_prog = getProgressString(tor, total_size, st);
    renderer_.get_padding(xpad, ypad);

    /* get the idealized cell dimensions */
    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->get_preferred_size(widget, min_size, icon_size);
    text_renderer_->property_text() = name;
    text_renderer_->property_weight() = Pango::WEIGHT_BOLD;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_NONE;
    text_renderer_->get_preferred_size(widget, min_size, name_size);
    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_weight() = Pango::WEIGHT_NORMAL;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->get_preferred_size(widget, min_size, prog_size);
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->get_preferred_size(widget, min_size, stat_size);

    /**
    *** LAYOUT
    **/

    width = xpad * 2 + icon_size.width + GUI_PAD + std::max(prog_size.width, stat_size.width);
    height = ypad * 2 + name_size.height + prog_size.height + GUI_PAD_SMALL + bar_height.get_value() + GUI_PAD_SMALL +
        stat_size.height;
}

void TorrentCellRenderer::get_preferred_width_vfunc(Gtk::Widget& widget, int& minimum_width, int& natural_width) const
{
    if (impl_->torrent.get_value() != nullptr)
    {
        int w;
        int h;

        if (impl_->compact.get_value())
        {
            impl_->get_size_compact(widget, w, h);
        }
        else
        {
            impl_->get_size_full(widget, w, h);
        }

        minimum_width = w;
        natural_width = w;
    }
}

void TorrentCellRenderer::get_preferred_height_vfunc(Gtk::Widget& widget, int& minimum_height, int& natural_height) const
{
    if (impl_->torrent.get_value() != nullptr)
    {
        int w;
        int h;

        if (impl_->compact.get_value())
        {
            impl_->get_size_compact(widget, w, h);
        }
        else
        {
            impl_->get_size_full(widget, w, h);
        }

        minimum_height = h;
        natural_height = h;
    }
}

namespace
{

Gdk::RGBA get_text_color(Gtk::Widget& w, tr_stat const* st)
{
    static auto const red = Gdk::RGBA("red");

    if (st->error != 0)
    {
        return red;
    }
    else if (st->activity == TR_STATUS_STOPPED)
    {
        return w.get_style_context()->get_color(Gtk::STATE_FLAG_INSENSITIVE);
    }
    else
    {
        return w.get_style_context()->get_color(Gtk::STATE_FLAG_NORMAL);
    }
}

double get_percent_done(tr_torrent const* tor, tr_stat const* st, bool* seed)
{
    double d;

    if (st->activity == TR_STATUS_SEED && tr_torrentGetSeedRatio(tor, &d))
    {
        *seed = true;
        d = MAX(0.0, st->seedRatioPercentDone);
    }
    else
    {
        *seed = false;
        d = MAX(0.0, st->percentDone);
    }

    return d;
}

} // namespace

void TorrentCellRenderer::Impl::render_compact(
    Cairo::RefPtr<Cairo::Context> const& cr,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gtk::CellRendererState flags)
{
    int xpad;
    int ypad;
    int min_width;
    int width;
    bool seed;

    auto* const tor = static_cast<tr_torrent*>(torrent.get_value());
    auto const* const st = tr_torrentStatCached(tor);
    bool const active = st->activity != TR_STATUS_STOPPED && st->activity != TR_STATUS_DOWNLOAD_WAIT &&
        st->activity != TR_STATUS_SEED_WAIT;
    auto const percentDone = get_percent_done(tor, st, &seed);
    bool const sensitive = active || st->error;

    auto const icon = get_icon(tor, CompactIconSize, widget);
    auto const name = Glib::ustring(tr_torrentName(tor));
    auto const gstr_stat = getShortStatusString(tor, st, upload_speed_KBps.get_value(), download_speed_KBps.get_value());
    renderer_.get_padding(xpad, ypad);
    auto const text_color = get_text_color(widget, st);

    auto fill_area = background_area;
    fill_area.set_x(fill_area.get_x() + xpad);
    fill_area.set_y(fill_area.get_y() + ypad);
    fill_area.set_width(fill_area.get_width() - xpad * 2);
    fill_area.set_height(fill_area.get_height() - ypad * 2);

    auto icon_area = fill_area;
    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->get_preferred_width(widget, min_width, width);
    icon_area.set_width(width);

    auto prog_area = fill_area;
    prog_area.set_width(CompactBarWidth);

    auto stat_area = fill_area;
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_NONE;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->get_preferred_width(widget, min_width, width);
    stat_area.set_width(width);

    auto name_area = fill_area;
    name_area.set_width(
        fill_area.get_width() - icon_area.get_width() - stat_area.get_width() - prog_area.get_width() - GUI_PAD * 3);

    if ((renderer_.get_state(widget, flags) & Gtk::StateFlags::STATE_FLAG_DIR_RTL) == 0)
    {
        icon_area.set_x(fill_area.get_x());
        prog_area.set_x(fill_area.get_x() + fill_area.get_width() - prog_area.get_width());
        stat_area.set_x(prog_area.get_x() - stat_area.get_width() - GUI_PAD);
        name_area.set_x(icon_area.get_x() + icon_area.get_width() + GUI_PAD);
    }
    else
    {
        icon_area.set_x(fill_area.get_x() + fill_area.get_width() - icon_area.get_width());
        prog_area.set_x(fill_area.get_x());
        stat_area.set_x(prog_area.get_x() + prog_area.get_width() + GUI_PAD);
        name_area.set_x(stat_area.get_x() + stat_area.get_width() + GUI_PAD);
    }

    /**
    *** RENDER
    **/

    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->property_sensitive() = sensitive;
    icon_renderer_->render(cr, widget, icon_area, icon_area, flags);

    auto const percent_done = static_cast<int>(percentDone * 100.0);
    progress_renderer_->property_value() = percent_done;
    progress_renderer_->property_text() = fmt::format(FMT_STRING("{:d}%"), percent_done);
    progress_renderer_->property_sensitive() = sensitive;
    progress_renderer_->render(cr, widget, prog_area, prog_area, flags);

    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_END;
    text_renderer_->property_foreground_rgba() = text_color;
    text_renderer_->render(cr, widget, stat_area, stat_area, flags);

    text_renderer_->property_text() = name;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->property_foreground_rgba() = text_color;
    text_renderer_->render(cr, widget, name_area, name_area, flags);
}

void TorrentCellRenderer::Impl::render_full(
    Cairo::RefPtr<Cairo::Context> const& cr,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gtk::CellRendererState flags)
{
    int xpad;
    int ypad;
    Gtk::Requisition min_size;
    Gtk::Requisition size;
    bool seed;

    auto* const tor = static_cast<tr_torrent*>(torrent.get_value());
    auto const* const st = tr_torrentStatCached(tor);
    auto const total_size = tr_torrentTotalSize(tor);
    bool const active = st->activity != TR_STATUS_STOPPED && st->activity != TR_STATUS_DOWNLOAD_WAIT &&
        st->activity != TR_STATUS_SEED_WAIT;
    auto const percentDone = get_percent_done(tor, st, &seed);
    bool const sensitive = active || st->error;

    auto const icon = get_icon(tor, FullIconSize, widget);
    auto const name = Glib::ustring(tr_torrentName(tor));
    auto const gstr_prog = getProgressString(tor, total_size, st);
    auto const gstr_stat = getStatusString(tor, st, upload_speed_KBps.get_value(), download_speed_KBps.get_value());
    renderer_.get_padding(xpad, ypad);
    auto const text_color = get_text_color(widget, st);

    /* get the idealized cell dimensions */
    Gdk::Rectangle icon_area;
    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->get_preferred_size(widget, min_size, size);
    icon_area.set_width(size.width);
    icon_area.set_height(size.height);

    Gdk::Rectangle name_area;
    text_renderer_->property_text() = name;
    text_renderer_->property_weight() = Pango::WEIGHT_BOLD;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_NONE;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->get_preferred_size(widget, min_size, size);
    name_area.set_height(size.height);

    Gdk::Rectangle prog_area;
    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_weight() = Pango::WEIGHT_NORMAL;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->get_preferred_size(widget, min_size, size);
    prog_area.set_height(size.height);

    Gdk::Rectangle stat_area;
    text_renderer_->property_text() = gstr_stat;
    text_renderer_->get_preferred_size(widget, min_size, size);
    stat_area.set_height(size.height);

    Gdk::Rectangle prct_area;

    /**
    *** LAYOUT
    **/

    auto fill_area = background_area;
    fill_area.set_x(fill_area.get_x() + xpad);
    fill_area.set_y(fill_area.get_y() + ypad);
    fill_area.set_width(fill_area.get_width() - xpad * 2);
    fill_area.set_height(fill_area.get_height() - ypad * 2);

    /* icon */
    icon_area.set_y(fill_area.get_y() + (fill_area.get_height() - icon_area.get_height()) / 2);

    /* name */
    name_area.set_y(fill_area.get_y());
    name_area.set_width(fill_area.get_width() - GUI_PAD - icon_area.get_width());

    if ((renderer_.get_state(widget, flags) & Gtk::StateFlags::STATE_FLAG_DIR_RTL) == 0)
    {
        icon_area.set_x(fill_area.get_x());
        name_area.set_x(fill_area.get_x() + fill_area.get_width() - name_area.get_width());
    }
    else
    {
        icon_area.set_x(fill_area.get_x() + fill_area.get_width() - icon_area.get_width());
        name_area.set_x(fill_area.get_x());
    }

    /* prog */
    prog_area.set_x(name_area.get_x());
    prog_area.set_y(name_area.get_y() + name_area.get_height());
    prog_area.set_width(name_area.get_width());

    /* progressbar */
    prct_area.set_x(prog_area.get_x());
    prct_area.set_y(prog_area.get_y() + prog_area.get_height() + GUI_PAD_SMALL);
    prct_area.set_width(prog_area.get_width());
    prct_area.set_height(bar_height.get_value());

    /* status */
    stat_area.set_x(prct_area.get_x());
    stat_area.set_y(prct_area.get_y() + prct_area.get_height() + GUI_PAD_SMALL);
    stat_area.set_width(prct_area.get_width());

    /**
    *** RENDER
    **/

    icon_renderer_->property_pixbuf() = icon;
    icon_renderer_->property_sensitive() = sensitive;
    icon_renderer_->render(cr, widget, icon_area, icon_area, flags);

    text_renderer_->property_text() = name;
    text_renderer_->property_scale() = 1.0;
    text_renderer_->property_foreground_rgba() = text_color;
    text_renderer_->property_ellipsize() = Pango::ELLIPSIZE_END;
    text_renderer_->property_weight() = Pango::WEIGHT_BOLD;
    text_renderer_->render(cr, widget, name_area, name_area, flags);

    text_renderer_->property_text() = gstr_prog;
    text_renderer_->property_scale() = SmallScale;
    text_renderer_->property_weight() = Pango::WEIGHT_NORMAL;
    text_renderer_->render(cr, widget, prog_area, prog_area, flags);

    progress_renderer_->property_value() = static_cast<int>(percentDone * 100.0);
    progress_renderer_->property_text() = Glib::ustring();
    progress_renderer_->property_sensitive() = sensitive;
    progress_renderer_->render(cr, widget, prct_area, prct_area, flags);

    text_renderer_->property_text() = gstr_stat;
    text_renderer_->property_foreground_rgba() = text_color;
    text_renderer_->render(cr, widget, stat_area, stat_area, flags);
}

void TorrentCellRenderer::render_vfunc(
    Cairo::RefPtr<Cairo::Context> const& cr,
    Gtk::Widget& widget,
    Gdk::Rectangle const& background_area,
    Gdk::Rectangle const& /*cell_area*/,
    Gtk::CellRendererState flags)
{
#ifdef TEST_RTL
    auto const real_dir = widget.get_direction();
    widget.set_direction(Gtk::TEXT_DIR_RTL);
#endif

    if (impl_->torrent.get_value() != nullptr)
    {
        if (impl_->compact.get_value())
        {
            impl_->render_compact(cr, widget, background_area, flags);
        }
        else
        {
            impl_->render_full(cr, widget, background_area, flags);
        }
    }

#ifdef TEST_RTL
    widget.set_direction(real_dir);
#endif
}

TorrentCellRenderer::Impl::~Impl()
{
    text_renderer_->unreference();
    progress_renderer_->unreference();
    icon_renderer_->unreference();
}

TorrentCellRenderer::TorrentCellRenderer()
    : Glib::ObjectBase(typeid(TorrentCellRenderer))
    , Gtk::CellRenderer()
    , impl_(std::make_unique<Impl>(*this))
{
}

TorrentCellRenderer::~TorrentCellRenderer() = default;

TorrentCellRenderer::Impl::Impl(TorrentCellRenderer& renderer)
    : torrent(renderer, "torrent", nullptr)
    , bar_height(renderer, "bar-height", DefaultBarHeight)
    , upload_speed_KBps(renderer, "piece-upload-speed", 0)
    , download_speed_KBps(renderer, "piece-download-speed", 0)
    , compact(renderer, "compact", false)
    , renderer_(renderer)
{
    text_renderer_ = Gtk::make_managed<Gtk::CellRendererText>();
    text_renderer_->property_xpad() = 0;
    text_renderer_->property_ypad() = 0;

    progress_renderer_ = Gtk::make_managed<Gtk::CellRendererProgress>();
    icon_renderer_ = Gtk::make_managed<Gtk::CellRendererPixbuf>();
}

Glib::PropertyProxy<gpointer> TorrentCellRenderer::property_torrent()
{
    return impl_->torrent.get_proxy();
}

Glib::PropertyProxy<double> TorrentCellRenderer::property_piece_upload_speed()
{
    return impl_->upload_speed_KBps.get_proxy();
}

Glib::PropertyProxy<double> TorrentCellRenderer::property_piece_download_speed()
{
    return impl_->download_speed_KBps.get_proxy();
}

Glib::PropertyProxy<int> TorrentCellRenderer::property_bar_height()
{
    return impl_->bar_height.get_proxy();
}

Glib::PropertyProxy<bool> TorrentCellRenderer::property_compact()
{
    return impl_->compact.get_proxy();
}
