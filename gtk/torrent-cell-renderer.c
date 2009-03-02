/*
 * This file Copyright (C) 2007-2009 Charles Kerr <charles@transmissionbt.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license.
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 *
 * $Id$
 */

#include "assert.h"
#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <libtransmission/transmission.h>
#include "hig.h"
#include "icons.h"
#include "torrent-cell-renderer.h"
#include "tr-torrent.h"
#include "util.h"

/* #define TEST_RTL */

enum
{
    P_TORRENT = 1,
    P_BAR_HEIGHT,
    P_MINIMAL
};

#define DEFAULT_BAR_HEIGHT 12
#define SMALL_SCALE 0.9
#define MINIMAL_ICON_SIZE GTK_ICON_SIZE_MENU
#define FULL_ICON_SIZE GTK_ICON_SIZE_DND

/***
****
***/

static char*
getProgressString( const tr_torrent * tor,
                   const tr_info    * info,
                   const tr_stat    * torStat )
{
    const int      isDone = torStat->leftUntilDone == 0;
    const uint64_t haveTotal = torStat->haveUnchecked + torStat->haveValid;
    const int      isSeed = torStat->haveValid >= info->totalSize;
    char           buf1[32], buf2[32], buf3[32], buf4[32];
    char *         str;
    double         seedRatio; 
    gboolean       hasSeedRatio;

    if( !isDone )
    {
        str = g_strdup_printf(
            /* %1$s is how much we've got,
               %2$s is how much we'll have when done,
               %3$.2f%% is a percentage of the two */
            _( "%1$s of %2$s (%3$.2f%%)" ),
            tr_strlsize( buf1, haveTotal, sizeof( buf1 ) ),
            tr_strlsize( buf2, torStat->sizeWhenDone, sizeof( buf2 ) ),
            torStat->percentDone * 100.0 );
    }
    else if( !isSeed )
    {
        str = g_strdup_printf(
            /* %1$s is how much we've got,
               %2$s is the torrent's total size,
               %3$.2f%% is a percentage of the two,
               %4$s is how much we've uploaded,
               %5$s is our upload-to-download ratio */
            _( "%1$s of %2$s (%3$.2f%%), uploaded %4$s (Ratio: %5$s)" ),
            tr_strlsize( buf1, haveTotal, sizeof( buf1 ) ),
            tr_strlsize( buf2, info->totalSize, sizeof( buf2 ) ),
            torStat->percentComplete * 100.0,
            tr_strlsize( buf3, torStat->uploadedEver, sizeof( buf3 ) ),
            tr_strlratio( buf4, torStat->ratio, sizeof( buf4 ) ) );
    }
    else if(( hasSeedRatio = tr_torrentGetSeedRatio( tor, &seedRatio )))
    {
        str = g_strdup_printf(
            /* %1$s is the torrent's total size,
               %2$s is how much we've uploaded,
               %3$s is our upload-to-download ratio,
               $4$s is the ratio we want to reach before we stop uploading */
            _( "%1$s, uploaded %2$s (Ratio: %3$s Goal: %4$s)" ),
            tr_strlsize( buf1, info->totalSize, sizeof( buf1 ) ),
            tr_strlsize( buf2, torStat->uploadedEver, sizeof( buf2 ) ),
            tr_strlratio( buf3, torStat->ratio, sizeof( buf3 ) ),
            tr_strlratio( buf4, seedRatio, sizeof( buf4 ) ) );
    }
    else /* seeding w/o a ratio */
    {
        str = g_strdup_printf(
            /* %1$s is the torrent's total size,
               %2$s is how much we've uploaded,
               %3$s is our upload-to-download ratio */
            _( "%1$s, uploaded %2$s (Ratio: %3$s)" ),
            tr_strlsize( buf1, info->totalSize, sizeof( buf1 ) ),
            tr_strlsize( buf2, torStat->uploadedEver, sizeof( buf2 ) ),
            tr_strlratio( buf3, torStat->ratio, sizeof( buf3 ) ) );
    }

    /* add time when downloading */
    if( hasSeedRatio || ( torStat->activity == TR_STATUS_DOWNLOAD ) )
    {
        const int eta = torStat->eta;
        GString * gstr = g_string_new( str );
        g_string_append( gstr, " - " );
        if( eta < 0 )
            g_string_append( gstr, _( "Remaining time unknown" ) );
        else
        {
            char timestr[128];
            tr_strltime( timestr, eta, sizeof( timestr ) );
            /* time remaining */
            g_string_append_printf( gstr, _( "%s remaining" ), timestr );
        }
        g_free( str );
        str = g_string_free( gstr, FALSE );
    }

    return str;
}

static char*
getShortTransferString( const tr_stat * torStat,
                        char *          buf,
                        size_t          buflen )
{
    char      downStr[32], upStr[32];
    const int haveDown = torStat->peersSendingToUs > 0;
    const int haveUp = torStat->peersGettingFromUs > 0;

    if( haveDown )
        tr_strlspeed( downStr, torStat->pieceDownloadSpeed, sizeof( downStr ) );
    if( haveUp )
        tr_strlspeed( upStr, torStat->pieceUploadSpeed, sizeof( upStr ) );

    if( haveDown && haveUp )
        /* Translators: "speed|" is here for disambiguation.  Please remove it from your translation.
           %1$s is the download speed
           %2$s is the upload speed */
        g_snprintf( buf, buflen, Q_(
                        "speed|Down: %1$s, Up: %2$s" ), downStr, upStr );
    else if( haveDown )
        /* download speed */
        g_snprintf( buf, buflen, _( "Down: %s" ), downStr );
    else if( haveUp )
        /* upload speed */
        g_snprintf( buf, buflen, _( "Up: %s" ), upStr );
    else
        /* the torrent isn't uploading or downloading */
        g_strlcpy( buf, _( "Idle" ), buflen );

    return buf;
}

static char*
getShortStatusString( const tr_stat * torStat )
{
    GString * gstr = g_string_new( NULL );

    switch( torStat->activity )
    {
        case TR_STATUS_STOPPED:
            g_string_assign( gstr, _( "Paused" ) );
            break;

        case TR_STATUS_CHECK_WAIT:
            g_string_assign( gstr, _( "Waiting to verify local data" ) );
            break;

        case TR_STATUS_CHECK:
            g_string_append_printf( gstr,
                                    _(
                                        "Verifying local data (%.1f%% tested)" ),
                                    torStat->recheckProgress * 100.0 );
            break;

        case TR_STATUS_DOWNLOAD:
        case TR_STATUS_SEED:
        {
            char buf[128];
            if( torStat->activity != TR_STATUS_DOWNLOAD )
            {
                tr_strlratio( buf, torStat->ratio, sizeof( buf ) );
                g_string_append_printf( gstr, _( "Ratio: %s" ), buf );
                g_string_append( gstr, ", " );
            }
            getShortTransferString( torStat, buf, sizeof( buf ) );
            g_string_append( gstr, buf );
            break;
        }

        default:
            break;
    }

    return g_string_free( gstr, FALSE );
}

static char*
getStatusString( const tr_stat * torStat )
{
    const int isActive = torStat->activity != TR_STATUS_STOPPED;
    const int isChecking = torStat->activity == TR_STATUS_CHECK
                        || torStat->activity == TR_STATUS_CHECK_WAIT;

    GString * gstr = g_string_new( NULL );

    if( torStat->error )
    {
        g_string_assign( gstr, torStat->errorString );
    }
    else switch( torStat->activity )
        {
            case TR_STATUS_STOPPED:
            case TR_STATUS_CHECK_WAIT:
            case TR_STATUS_CHECK:
            {
                char * pch = getShortStatusString( torStat );
                g_string_assign( gstr, pch );
                g_free( pch );
                break;
            }

            case TR_STATUS_DOWNLOAD:
                g_string_append_printf( gstr,
                    ngettext( "Downloading from %1$'d of %2$'d connected peer",
                              "Downloading from %1$'d of %2$'d connected peers",
                              torStat->peersConnected ),
                    torStat->peersSendingToUs +
                    torStat->webseedsSendingToUs,
                    torStat->peersConnected +
                    torStat->webseedsSendingToUs );
                break;

            case TR_STATUS_SEED:
                g_string_append_printf( gstr,
                    ngettext( "Seeding to %1$'d of %2$'d connected peer",
                              "Seeding to %1$'d of %2$'d connected peers",
                              torStat->peersConnected ),
                    torStat->peersGettingFromUs,
                    torStat->peersConnected );
                break;
        }

    if( isActive && !isChecking )
    {
        char buf[256];
        getShortTransferString( torStat, buf, sizeof( buf ) );
        g_string_append_printf( gstr, " - %s", buf );
    }

    return g_string_free( gstr, FALSE );
}

/***
****
***/

static GtkCellRendererClass * parent_class = NULL;

struct TorrentCellRendererPrivate
{
    tr_torrent       * tor;
    GtkCellRenderer  * text_renderer;
    GtkCellRenderer  * text_renderer_err;
    GtkCellRenderer  * progress_renderer;
    GtkCellRenderer  * icon_renderer;
    int                bar_height;
    gboolean           minimal;
};

/***
****
***/

static GdkPixbuf*
get_icon( const tr_torrent * tor, GtkIconSize icon_size, GtkWidget * for_widget )
{
    const char * mime_type;
    const tr_info * info = tr_torrentInfo( tor );

    /* a very basic cache, but hits about 80% of the time... */
    static GdkPixbuf   * prev_icon = NULL;
    static GtkWidget   * prev_widget = NULL;
    static const char  * prev_mime_type = NULL;
    static GtkIconSize   prev_size = 0;

    if( info->fileCount > 1 )
        mime_type = DIRECTORY_MIME_TYPE;
    else
        mime_type = get_mime_type_from_filename( info->files[0].name );

    if( ( for_widget == prev_widget ) && ( prev_size == icon_size ) && !strcmp( prev_mime_type, mime_type ) )
        return prev_icon;

    prev_mime_type = mime_type;
    prev_size = icon_size;
    prev_widget = for_widget;
    prev_icon = get_mime_type_icon( mime_type, icon_size, for_widget );
    return prev_icon;
}

static GtkCellRenderer*
get_text_renderer( const tr_stat * st, TorrentCellRenderer * r )
{
    return st->error ? r->priv->text_renderer_err : r->priv->text_renderer;
}

/***
****
***/

static void
get_size_minimal( TorrentCellRenderer * cell,
                  GtkWidget           * widget,
                  gint                * width,
                  gint                * height )
{
    int w, h;
    GdkRectangle icon_area;
    GdkRectangle name_area;
    GdkRectangle stat_area;
    const char * name;
    char * status;
    GdkPixbuf * icon;
    GtkCellRenderer * text_renderer;

    struct TorrentCellRendererPrivate * p = cell->priv;
    const tr_torrent * tor = p->tor;
    const tr_stat * st = tr_torrentStatCached( (tr_torrent*)tor );

    icon = get_icon( tor, MINIMAL_ICON_SIZE, widget );
    name = tr_torrentInfo( tor )->name;
    status = getShortStatusString( st );

    /* get the idealized cell dimensions */
    g_object_set( p->icon_renderer, "pixbuf", icon, NULL );
    gtk_cell_renderer_get_size( p->icon_renderer, widget, NULL, NULL, NULL, &w, &h );
    icon_area.width = w;
    icon_area.height = h;
    text_renderer = get_text_renderer( st, cell );
    g_object_set( text_renderer, "text", name, "ellipsize", PANGO_ELLIPSIZE_NONE,  "scale", 1.0, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    name_area.width = w;
    name_area.height = h;
    g_object_set( text_renderer, "text", status, "scale", SMALL_SCALE, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    stat_area.width = w;
    stat_area.height = h;
    
    /**
    *** LAYOUT
    **/

    if( width != NULL )
        *width = cell->parent.xpad * 2 + icon_area.width + GUI_PAD + name_area.width + GUI_PAD + stat_area.width;
    if( height != NULL )
        *height = cell->parent.ypad * 2 + name_area.height + p->bar_height;

    /* cleanup */
    g_free( status );
}

#define MAX3(a,b,c) MAX(a,MAX(b,c))

static void
get_size_full( TorrentCellRenderer * cell,
               GtkWidget           * widget,
               gint                * width,
               gint                * height )
{
    int w, h;
    GdkRectangle icon_area;
    GdkRectangle name_area;
    GdkRectangle stat_area;
    GdkRectangle prog_area;
    const char * name;
    char * status;
    char * progress;
    GdkPixbuf * icon;
    GtkCellRenderer * text_renderer;

    struct TorrentCellRendererPrivate * p = cell->priv;
    const tr_torrent * tor = p->tor;
    const tr_stat * st = tr_torrentStatCached( (tr_torrent*)tor );
    const tr_info * inf = tr_torrentInfo( tor );

    icon = get_icon( tor, FULL_ICON_SIZE, widget );
    name = inf->name;
    status = getStatusString( st );
    progress = getProgressString( tor, inf, st );

    /* get the idealized cell dimensions */
    g_object_set( p->icon_renderer, "pixbuf", icon, NULL );
    gtk_cell_renderer_get_size( p->icon_renderer, widget, NULL, NULL, NULL, &w, &h );
    icon_area.width = w;
    icon_area.height = h;
    text_renderer = get_text_renderer( st, cell );
    g_object_set( text_renderer, "text", name, "weight", PANGO_WEIGHT_BOLD, "scale", 1.0, "ellipsize", PANGO_ELLIPSIZE_NONE, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    name_area.width = w;
    name_area.height = h;
    g_object_set( text_renderer, "text", progress, "weight", PANGO_WEIGHT_NORMAL, "scale", SMALL_SCALE, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    prog_area.width = w;
    prog_area.height = h;
    g_object_set( text_renderer, "text", status, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    stat_area.width = w;
    stat_area.height = h;
    
    /**
    *** LAYOUT
    **/

    if( width != NULL )
        *width = cell->parent.xpad * 2 + icon_area.width + GUI_PAD + MAX3( name_area.width, prog_area.width, stat_area.width );
    if( height != NULL )
        *height = cell->parent.ypad * 2 + name_area.height + prog_area.height + GUI_PAD_SMALL + p->bar_height + GUI_PAD_SMALL + stat_area.height;

    /* cleanup */
    g_free( status );
    g_free( progress );
}


static void
torrent_cell_renderer_get_size( GtkCellRenderer  * cell,
                                GtkWidget        * widget,
                                GdkRectangle     * cell_area,
                                gint             * x_offset,
                                gint             * y_offset,
                                gint             * width,
                                gint             * height )
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( cell );

    if( self && self->priv->tor )
    {
        struct TorrentCellRendererPrivate * p = self->priv;
        int w, h;

        if( p->minimal )
            get_size_minimal( TORRENT_CELL_RENDERER( cell ), widget, &w, &h );
        else
            get_size_full( TORRENT_CELL_RENDERER( cell ), widget, &w, &h );

        if( width )
            *width = w;

        if( height )
            *height = h;

        if( cell_area ) {
            if( x_offset ) *x_offset = 0;
            if( y_offset ) {
                *y_offset = 0.5 * ( cell_area->height - ( h + ( 2 * cell->ypad ) ) );
                *y_offset = MAX( *y_offset, 0 );
            }
        }
    }
}

static void
render_minimal( TorrentCellRenderer   * cell,
                GdkDrawable           * window,
                GtkWidget             * widget,
                GdkRectangle          * background_area,
                GdkRectangle          * cell_area UNUSED,
                GdkRectangle          * expose_area UNUSED,
                GtkCellRendererState    flags )
{
    int w, h;
    GdkRectangle icon_area;
    GdkRectangle name_area;
    GdkRectangle stat_area;
    GdkRectangle prog_area;
    GdkRectangle fill_area;
    const char * name;
    char * status;
    GdkPixbuf * icon;
    GtkCellRenderer * text_renderer;

    struct TorrentCellRendererPrivate * p = cell->priv;
    const tr_torrent * tor = p->tor;
    const tr_stat * st = tr_torrentStatCached( (tr_torrent*)tor );
    const gboolean active = st->activity != TR_STATUS_STOPPED;
    const double percentDone = MAX( 0.0, st->percentDone );

    icon = get_icon( tor, MINIMAL_ICON_SIZE, widget );
    name = tr_torrentInfo( tor )->name;
    status = getShortStatusString( st );

    /* get the cell dimensions */
    g_object_set( p->icon_renderer, "pixbuf", icon, NULL );
    gtk_cell_renderer_get_size( p->icon_renderer, widget, NULL, NULL, NULL, &w, &h );
    icon_area.width = w;
    icon_area.height = h;
    text_renderer = get_text_renderer( st, cell );
    g_object_set( text_renderer, "text", name, "ellipsize", PANGO_ELLIPSIZE_NONE, "scale", 1.0, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    name_area.width = w;
    name_area.height = h;
    g_object_set( text_renderer, "text", status, "scale", SMALL_SCALE, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    stat_area.width = w;
    stat_area.height = h;

    /**
    *** LAYOUT
    **/

    fill_area = *background_area;
    fill_area.x += cell->parent.xpad;
    fill_area.y += cell->parent.ypad;
    fill_area.width -= cell->parent.xpad * 2;
    fill_area.height -= cell->parent.ypad * 2;

    /* icon */
    icon_area.x = fill_area.x;
    icon_area.y = fill_area.y + ( fill_area.height - icon_area.height ) / 2;

    /* short status (right justified) */
    stat_area.x = fill_area.x + fill_area.width - stat_area.width;
    stat_area.y = fill_area.y + ( name_area.height - stat_area.height ) / 2;

    /* name */
    name_area.x = icon_area.x + icon_area.width + GUI_PAD;
    name_area.y = fill_area.y;
    name_area.width = stat_area.x - GUI_PAD - name_area.x;

    /* progressbar */
    prog_area.x = name_area.x;
    prog_area.y = name_area.y + name_area.height;
    prog_area.width = name_area.width + GUI_PAD + stat_area.width;
    prog_area.height = p->bar_height;

    /**
    *** RENDER
    **/
   
    g_object_set( p->icon_renderer, "pixbuf", icon, "sensitive", active, NULL );
    gtk_cell_renderer_render( p->icon_renderer, window, widget, &icon_area, &icon_area, &icon_area, flags );
    g_object_set( text_renderer, "text", status, "scale", SMALL_SCALE, "sensitive", active, "ellipsize", PANGO_ELLIPSIZE_END, NULL );
    gtk_cell_renderer_render( text_renderer, window, widget, &stat_area, &stat_area, &stat_area, flags );
    g_object_set( text_renderer, "text", name, "scale", 1.0, NULL );
    gtk_cell_renderer_render( text_renderer, window, widget, &name_area, &name_area, &name_area, flags );
    g_object_set( p->progress_renderer, "value", (int)(percentDone*100.0), "text", "", "sensitive", active, NULL );
    gtk_cell_renderer_render( p->progress_renderer, window, widget, &prog_area, &prog_area, &prog_area, flags );

    /* cleanup */
    g_free( status );
}

static void
render_full( TorrentCellRenderer   * cell,
             GdkDrawable           * window,
             GtkWidget             * widget,
             GdkRectangle          * background_area,
             GdkRectangle          * cell_area UNUSED,
             GdkRectangle          * expose_area UNUSED,
             GtkCellRendererState    flags )
{
    int w, h;
    GdkRectangle fill_area;
    GdkRectangle icon_area;
    GdkRectangle name_area;
    GdkRectangle stat_area;
    GdkRectangle prog_area;
    GdkRectangle prct_area;
    const char * name;
    char * status;
    char * progress;
    GdkPixbuf * icon;
    GtkCellRenderer * text_renderer;

    struct TorrentCellRendererPrivate * p = cell->priv;
    const tr_torrent * tor = p->tor;
    const tr_stat * st = tr_torrentStatCached( (tr_torrent*)tor );
    const tr_info * inf = tr_torrentInfo( tor );
    const gboolean active = st->activity != TR_STATUS_STOPPED;
    const double percentDone = MAX( 0.0, st->percentDone );

    icon = get_icon( tor, FULL_ICON_SIZE, widget );
    name = inf->name;
    status = getStatusString( st );
    progress = getProgressString( tor, inf, st );

    /* get the idealized cell dimensions */
    g_object_set( p->icon_renderer, "pixbuf", icon, NULL );
    gtk_cell_renderer_get_size( p->icon_renderer, widget, NULL, NULL, NULL, &w, &h );
    icon_area.width = w;
    icon_area.height = h;
    text_renderer = get_text_renderer( st, cell );
    g_object_set( text_renderer, "text", name, "weight", PANGO_WEIGHT_BOLD, "ellipsize", PANGO_ELLIPSIZE_NONE, "scale", 1.0, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    name_area.width = w;
    name_area.height = h;
    g_object_set( text_renderer, "text", progress, "weight", PANGO_WEIGHT_NORMAL, "scale", SMALL_SCALE, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    prog_area.width = w;
    prog_area.height = h;
    g_object_set( text_renderer, "text", status, NULL );
    gtk_cell_renderer_get_size( text_renderer, widget, NULL, NULL, NULL, &w, &h );
    stat_area.width = w;
    stat_area.height = h;
    
    /**
    *** LAYOUT
    **/

    fill_area = *background_area;
    fill_area.x += cell->parent.xpad;
    fill_area.y += cell->parent.ypad;
    fill_area.width -= cell->parent.xpad * 2;
    fill_area.height -= cell->parent.ypad * 2;

    /* icon */
    icon_area.x = fill_area.x;
    icon_area.y = fill_area.y + ( fill_area.height - icon_area.height ) / 2;

    /* name */
    name_area.x = icon_area.x + icon_area.width + GUI_PAD;
    name_area.y = fill_area.y;
    name_area.width = fill_area.width - GUI_PAD - icon_area.width - GUI_PAD_SMALL;

    /* prog */
    prog_area.x = name_area.x;
    prog_area.y = name_area.y + name_area.height;
    prog_area.width = name_area.width;

    /* progressbar */
    prct_area.x = prog_area.x;
    prct_area.y = prog_area.y + prog_area.height + GUI_PAD_SMALL;
    prct_area.width = prog_area.width;
    prct_area.height = p->bar_height;

    /* status */
    stat_area.x = prct_area.x;
    stat_area.y = prct_area.y + prct_area.height + GUI_PAD_SMALL;
    stat_area.width = prct_area.width;

    /**
    *** RENDER
    **/
   
    g_object_set( p->icon_renderer, "pixbuf", icon, "sensitive", active, NULL );
    gtk_cell_renderer_render( p->icon_renderer, window, widget, &icon_area, &icon_area, &icon_area, flags );
    g_object_set( text_renderer, "text", name, "scale", 1.0, "sensitive", active, "ellipsize", PANGO_ELLIPSIZE_END, "weight", PANGO_WEIGHT_BOLD, NULL );
    gtk_cell_renderer_render( text_renderer, window, widget, &name_area, &name_area, &name_area, flags );
    g_object_set( text_renderer, "text", progress, "scale", SMALL_SCALE, "weight", PANGO_WEIGHT_NORMAL, NULL );
    gtk_cell_renderer_render( text_renderer, window, widget, &prog_area, &prog_area, &prog_area, flags );
    g_object_set( p->progress_renderer, "value", (int)(percentDone*100.0), "text", "", "sensitive", active, NULL );
    gtk_cell_renderer_render( p->progress_renderer, window, widget, &prct_area, &prct_area, &prct_area, flags );
    g_object_set( text_renderer, "text", status, NULL );
    gtk_cell_renderer_render( text_renderer, window, widget, &stat_area, &stat_area, &stat_area, flags );

    /* cleanup */
    g_free( status );
    g_free( progress );
}

static void
torrent_cell_renderer_render( GtkCellRenderer       * cell,
                              GdkDrawable           * window,
                              GtkWidget             * widget,
                              GdkRectangle          * background_area,
                              GdkRectangle          * cell_area,
                              GdkRectangle          * expose_area,
                              GtkCellRendererState    flags )
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( cell );

#ifdef TEST_RTL
    GtkTextDirection      real_dir = gtk_widget_get_direction( widget );
    gtk_widget_set_direction( widget, GTK_TEXT_DIR_RTL );
#endif

    if( self && self->priv->tor )
    {
        struct TorrentCellRendererPrivate * p = self->priv;
        if( p->minimal )
            render_minimal( self, window, widget, background_area, cell_area, expose_area, flags );
        else
            render_full( self, window, widget, background_area, cell_area, expose_area, flags );
    }

#ifdef TEST_RTL
    gtk_widget_set_direction( widget, real_dir );
#endif
}

static void
torrent_cell_renderer_set_property( GObject *      object,
                                    guint          property_id,
                                    const GValue * v,
                                    GParamSpec *   pspec )
{
    TorrentCellRenderer *               self = TORRENT_CELL_RENDERER(
        object );
    struct TorrentCellRendererPrivate * p = self->priv;

    switch( property_id )
    {
        case P_TORRENT:
            p->tor = g_value_get_pointer( v ); break;

        case P_BAR_HEIGHT:
            p->bar_height = g_value_get_int( v ); break;

        case P_MINIMAL:
            p->minimal  = g_value_get_boolean( v ); break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
torrent_cell_renderer_get_property( GObject *    object,
                                    guint        property_id,
                                    GValue *     v,
                                    GParamSpec * pspec )
{
    const TorrentCellRenderer *         self = TORRENT_CELL_RENDERER(
        object );
    struct TorrentCellRendererPrivate * p = self->priv;

    switch( property_id )
    {
        case P_TORRENT:
            g_value_set_pointer( v, p->tor ); break;

        case P_BAR_HEIGHT:
            g_value_set_int( v, p->bar_height ); break;

        case P_MINIMAL:
            g_value_set_boolean( v, p->minimal ); break;

        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
torrent_cell_renderer_dispose( GObject * o )
{
    TorrentCellRenderer * r = TORRENT_CELL_RENDERER( o );
    GObjectClass *        parent;

    if( r && r->priv )
    {
        g_object_unref( G_OBJECT( r->priv->text_renderer ) );
        g_object_unref( G_OBJECT( r->priv->text_renderer_err ) );
        g_object_unref( G_OBJECT( r->priv->progress_renderer ) );
        g_object_unref( G_OBJECT( r->priv->icon_renderer ) );
        r->priv = NULL;
    }

    parent = g_type_class_peek( g_type_parent( TORRENT_CELL_RENDERER_TYPE ) );
    parent->dispose( o );
}

static void
torrent_cell_renderer_class_init( TorrentCellRendererClass * klass )
{
    GObjectClass *         gobject_class = G_OBJECT_CLASS( klass );
    GtkCellRendererClass * cell_class = GTK_CELL_RENDERER_CLASS( klass );

    g_type_class_add_private( klass,
                             sizeof( struct TorrentCellRendererPrivate ) );

    parent_class = (GtkCellRendererClass*) g_type_class_peek_parent( klass );

    cell_class->render = torrent_cell_renderer_render;
    cell_class->get_size = torrent_cell_renderer_get_size;
    gobject_class->set_property = torrent_cell_renderer_set_property;
    gobject_class->get_property = torrent_cell_renderer_get_property;
    gobject_class->dispose = torrent_cell_renderer_dispose;

    g_object_class_install_property( gobject_class, P_TORRENT,
                                    g_param_spec_pointer( "torrent", NULL,
                                                          "tr_torrent*",
                                                          G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_BAR_HEIGHT,
                                    g_param_spec_int( "bar-height", NULL,
                                                      "Bar Height",
                                                      1, INT_MAX,
                                                      DEFAULT_BAR_HEIGHT,
                                                      G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_MINIMAL,
                                    g_param_spec_boolean( "minimal", NULL,
                                                          "Minimal Mode",
                                                          FALSE,
                                                          G_PARAM_READWRITE ) );
}

static void
torrent_cell_renderer_init( GTypeInstance *  instance,
                            gpointer g_class UNUSED )
{
    TorrentCellRenderer *               self = TORRENT_CELL_RENDERER(
        instance );
    struct TorrentCellRendererPrivate * p;

    p = self->priv = G_TYPE_INSTANCE_GET_PRIVATE(
            self,
            TORRENT_CELL_RENDERER_TYPE,
            struct
            TorrentCellRendererPrivate );

    p->tor = NULL;
    p->text_renderer = gtk_cell_renderer_text_new( );
    g_object_set( p->text_renderer, "xpad", 0, "ypad", 0, NULL );
    p->text_renderer_err = gtk_cell_renderer_text_new(  );
    g_object_set( p->text_renderer_err, "xpad", 0, "ypad", 0, NULL );
    p->progress_renderer = gtk_cell_renderer_progress_new(  );
    p->icon_renderer = gtk_cell_renderer_pixbuf_new(  );
    g_object_set( p->text_renderer_err, "foreground", "red", NULL );
    tr_object_ref_sink( p->text_renderer );
    tr_object_ref_sink( p->text_renderer_err );
    tr_object_ref_sink( p->progress_renderer );
    tr_object_ref_sink( p->icon_renderer );

    p->bar_height = DEFAULT_BAR_HEIGHT;
}

GType
torrent_cell_renderer_get_type( void )
{
    static GType type = 0;

    if( !type )
    {
        static const GTypeInfo info =
        {
            sizeof( TorrentCellRendererClass ),
            NULL,                                            /* base_init */
            NULL,                                            /* base_finalize */
            (GClassInitFunc)torrent_cell_renderer_class_init,
            NULL,                                            /* class_finalize
                                                               */
            NULL,                                            /* class_data */
            sizeof( TorrentCellRenderer ),
            0,                                               /* n_preallocs */
            (GInstanceInitFunc)torrent_cell_renderer_init,
            NULL
        };

        type = g_type_register_static( GTK_TYPE_CELL_RENDERER,
                                       "TorrentCellRenderer",
                                       &info, (GTypeFlags)0 );
    }

    return type;
}

GtkCellRenderer *
torrent_cell_renderer_new( void )
{
    return (GtkCellRenderer *) g_object_new( TORRENT_CELL_RENDERER_TYPE,
                                             NULL );
}

