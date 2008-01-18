/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
 *
 * This file is licensed by the GPL version 2.  Works owned by the
 * Transmission project are granted a special exemption to clause 2(b)
 * so that the bulk of its code can remain under the MIT license. 
 * This exemption does not extend to derived works not owned by
 * the Transmission project.
 * 
 * $Id:$
 */

#include "assert.h"
#include <gtk/gtk.h>
#include <gtk/gtkcellrenderertext.h>
#include <glib/gi18n.h>
#include <libtransmission/transmission.h>
#include "hig.h"
#include "torrent-cell-renderer.h"
#include "tr_torrent.h"
#include "util.h"

enum
{
    P_TORRENT = 1,
    P_BAR_HEIGHT,
    P_MINIMAL,
    P_SHOW_UNAVAILABLE,
    P_GRADIENT,
    P_COLOR_VERIFIED,
    P_COLOR_VERIFIED_2,
    P_COLOR_MISSING,
    P_COLOR_MISSING_2,
    P_COLOR_UNWANTED,
    P_COLOR_UNWANTED_2,
    P_COLOR_UNAVAILABLE,
    P_COLOR_UNAVAILABLE_2,
    P_COLOR_PAUSED,
    P_COLOR_PAUSED_2,
    P_COLOR_VERIFYING,
    P_COLOR_VERIFYING_2,
    P_COLOR_SEEDING,
    P_COLOR_SEEDING_2
};

#define DEFAULT_BAR_HEIGHT 12

#define DEFAULT_COLOR_VERIFIED       "#77aaff"
#define DEFAULT_COLOR_VERIFIED_2     "#002277"
#define DEFAULT_COLOR_SEEDING        "#77ff88"
#define DEFAULT_COLOR_SEEDING_2      "#007700"
#define DEFAULT_COLOR_MISSING        "#fcfcfc" 
#define DEFAULT_COLOR_MISSING_2      "#acacac" 
#define DEFAULT_COLOR_UNWANTED       "#e0e0e0" 
#define DEFAULT_COLOR_UNWANTED_2     "#808080" 
#define DEFAULT_COLOR_UNAVAILABLE    "#ff7788"
#define DEFAULT_COLOR_UNAVAILABLE_2  "#770000"
#define DEFAULT_COLOR_PAUSED         "#959595"
#define DEFAULT_COLOR_PAUSED_2       "#555555"
#define DEFAULT_COLOR_VERIFYING      "#ffff77"
#define DEFAULT_COLOR_VERIFYING_2    "#777700"

/***
****
***/

static char*
getProgressString( const tr_info * info, const tr_stat * torStat )
{
    const int isDone = torStat->leftUntilDone == 0;
    const uint64_t haveTotal = torStat->haveUnchecked + torStat->haveValid;
    const int isSeed = torStat->haveValid >= info->totalSize;
    char buf1[32], buf2[32], buf3[32], buf4[32];
    char * str;

    if( !isDone )
        str = g_strdup_printf(
                  _("%s of %s (%.2f%%)"),
                  tr_strlsize( buf1, haveTotal, sizeof(buf1) ),
                  tr_strlsize( buf2, torStat->desiredSize, sizeof(buf2) ),
                  torStat->percentDone * 100.0 );
    else if( !isSeed )
        str = g_strdup_printf(
                  _("%s of %s (%.2f%%), uploaded %s (Ratio: %s)"),
                  tr_strlsize( buf1, haveTotal, sizeof(buf1) ),
                  tr_strlsize( buf2, info->totalSize, sizeof(buf2) ),
                  torStat->percentComplete * 100.0,
                  tr_strlsize( buf3, torStat->uploadedEver, sizeof(buf3) ),
                  tr_strlratio( buf4, torStat->ratio, sizeof( buf4 ) ) );
    else
        str = g_strdup_printf(
                  _("%s, uploaded %s (Ratio: %s)"),
                  tr_strlsize( buf1, info->totalSize, sizeof(buf1) ),
                  tr_strlsize( buf2, torStat->uploadedEver, sizeof(buf2) ),
                  tr_strlratio( buf3, torStat->ratio, sizeof( buf3 ) ) );

    return str;
}

static char*
getShortTransferString( const tr_stat * torStat, char * buf, size_t buflen )
{
    char downStr[32], upStr[32];
    const int haveDown = ( torStat->rateDownload * 1024 ) > 1.0;
    const int haveUp = ( torStat->rateUpload * 1024 ) > 1.0;

    if( haveDown )
        tr_strlspeed( downStr, torStat->rateDownload, sizeof(downStr) );
    if( haveUp )
        tr_strlspeed( upStr, torStat->rateUpload, sizeof(upStr) );

    if( haveDown && haveUp )
        g_snprintf( buf, buflen, _( "Down: %s, Up: %s"), downStr, upStr );
    else if( haveDown )
        g_snprintf( buf, buflen, _( "Down: %s" ), downStr );
    else if( haveUp )
        g_snprintf( buf, buflen, _( "Up: %s" ), upStr );
    else
        g_strlcpy( buf, _( "Idle" ), buflen );

    return buf;
}

static char*
getShortStatusString( const tr_stat * torStat )
{
    GString * gstr = g_string_new( NULL );

    switch( torStat->status )
    {
        case TR_STATUS_STOPPED:
            g_string_assign( gstr, _("Paused") );
            break;

        case TR_STATUS_CHECK_WAIT:
            g_string_assign( gstr, _( "Waiting to Verify local data" ) );
            break;

        case TR_STATUS_CHECK:
            g_string_append_printf( gstr, _("Verifying local data (%.1f%% tested)"),
                                    torStat->recheckProgress * 100.0 );

        case TR_STATUS_DOWNLOAD:
        case TR_STATUS_SEED:
        case TR_STATUS_DONE: {
            char buf[128];
            if( torStat->status != TR_STATUS_DOWNLOAD ) {
                tr_strlratio( buf, torStat->ratio, sizeof( buf ) );
                g_string_append_printf( gstr, _("Ratio: %s, " ), buf );
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
    const int isActive = torStat->status != TR_STATUS_STOPPED;
    const int isChecking = torStat->status == TR_STATUS_CHECK
                        || torStat->status == TR_STATUS_CHECK_WAIT;

    GString * gstr = g_string_new( NULL );

    if( torStat->error )
    {
        g_string_assign( gstr, torStat->errorString );
    }
    else switch( torStat->status )
    {
        case TR_STATUS_STOPPED:
        case TR_STATUS_CHECK_WAIT:
        case TR_STATUS_CHECK: {
            char * pch = getShortStatusString( torStat );
            g_string_assign( gstr, pch );
            g_free( pch );
            break;
        }

        case TR_STATUS_DOWNLOAD:
            g_string_append_printf( gstr,
                ngettext( "Downloading from %d of %d connected peer",
                          "Downloading from %d of %d connected peers",
                          torStat->peersConnected ),
                torStat->peersSendingToUs,
                torStat->peersConnected );
            break;

        case TR_STATUS_DONE:
        case TR_STATUS_SEED:
            g_string_append_printf( gstr,
                ngettext( "Seeding to %d of %d connected peer",
                          "Seeding to %d of %d connected peers",
                          torStat->peersConnected ),
                torStat->peersGettingFromUs,
                torStat->peersConnected );
            break;
    }

    if( isActive && !isChecking )
    {
        char buf[256];
        getShortTransferString( torStat, buf, sizeof(buf) );
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
    tr_torrent * tor;
    GtkCellRenderer * text_renderer;
    GtkCellRenderer * text_renderer_err;
    int bar_height;
    gboolean minimal;
    gboolean show_unavailable;
    gboolean gradient;
    GdkColor color_paused[2];
    GdkColor color_verified[2];
    GdkColor color_verifying[2];
    GdkColor color_missing[2];
    GdkColor color_unwanted[2];
    GdkColor color_unavailable[2];
    GdkColor color_seeding[2];
};

static void
torrent_cell_renderer_get_size( GtkCellRenderer  * cell,
                                GtkWidget        * widget,
                                GdkRectangle     * cell_area,
                                gint             * x_offset,
                                gint             * y_offset,
                                gint             * width,
                                gint             * height)
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( cell );
    int xpad, ypad;
    g_object_get( self, "xpad", &xpad, "ypad", &ypad, NULL );

    if( self && self->priv->tor )
    {
        const tr_torrent * tor = self->priv->tor;
        const tr_info * info = tr_torrentInfo( tor );
        const char * name = info->name;
        const tr_stat * torStat = tr_torrentStatCached( (tr_torrent*)tor );
        char * str;
        int w=0, h=0;
        struct TorrentCellRendererPrivate * p = self->priv;
        GtkCellRenderer * text_renderer = torStat->error != 0
            ? p->text_renderer_err
            : p->text_renderer;

        g_object_set( text_renderer, "ellipsize", PANGO_ELLIPSIZE_NONE, NULL );

        /* above the progressbar */
        if( p->minimal )
        {
            int w1, w2, h1, h2;
            char * shortStatus = getShortStatusString( torStat );
            g_object_set( text_renderer, "text", name, NULL );
            gtk_cell_renderer_get_size( text_renderer,
                                        widget, NULL, NULL, NULL, &w1, &h1 );
            str = g_markup_printf_escaped( "<small>%s</small>", shortStatus );
            g_object_set( text_renderer, "markup", str, NULL );
            gtk_cell_renderer_get_size( text_renderer,
                                        widget, NULL, NULL, NULL, &w2, &h2 );
            h += MAX( h1, h2 );
            w = MAX( w, w1+GUI_PAD_BIG+w2 );
            g_free( str );
            g_free( shortStatus );
        }
        else
        {
            int w1, h1;
            char * progressString = getProgressString( info, torStat );
            str = g_markup_printf_escaped( "<b>%s</b>\n<small>%s</small>",
                                           name, progressString );
            g_object_set( text_renderer, "markup", str, NULL );
            gtk_cell_renderer_get_size( text_renderer,
                                        widget, NULL, NULL, NULL, &w1, &h1 );
            h += h1;
            w = MAX( w, w1 );
            g_free( str );
            g_free( progressString );
        }

        /* below the progressbar */
        if( !p->minimal )
        {
            int w1, h1;
            char * statusString = getStatusString( torStat );
            str = g_markup_printf_escaped( "<small>%s</small>", statusString );
            g_object_set( text_renderer, "markup", str, NULL );
            gtk_cell_renderer_get_size( text_renderer,
                                        widget, NULL, NULL, NULL, &w1, &h1 );
            h += h1;
            w = MAX( w, w1 );
            g_free( str );
            g_free( statusString );
        }

        h += p->bar_height;

        if( cell_area ) {
            if( x_offset ) *x_offset = 0;
            if( y_offset ) {
                *y_offset = 0.5 * (cell_area->height - (h + (2 * ypad)));
                *y_offset = MAX( *y_offset, 0 );
            }
        }

        *width = w + xpad*2;
        *height = h + ypad*2;
    }
}

static void
fillRect( TorrentCellRenderer * self,
          GdkGC               * gc,
          GdkDrawable         * drawable,
          const GdkRectangle  * area_in,
          const GdkColor      * colors,
          size_t                n_colors )
{
    const int drawGradient = self->priv->gradient && ( n_colors > 1 );
    assert( n_colors==1 || n_colors==2 );

    if( !drawGradient )
    {
        gdk_gc_set_rgb_fg_color( gc, colors );
        gdk_draw_rectangle( drawable, gc, TRUE,
                            area_in->x, area_in->y,
                            area_in->width, area_in->height );
    }
    else
    {
        int i;
        const int steps = area_in->height;
        const int step_height = area_in->height / steps;
        const int r_inc = ((int)colors[1].red   - (int)colors[0].red) / steps;
        const int g_inc = ((int)colors[1].green - (int)colors[0].green) / steps;
        const int b_inc = ((int)colors[1].blue  - (int)colors[0].blue) / steps;

        GdkRectangle area = *area_in;
        GdkColor color = colors[0];
     
        area.height = step_height;
        for( i=0; i<steps; ++i ) {
            gdk_gc_set_rgb_fg_color( gc, &color );
            gdk_draw_rectangle( drawable, gc, TRUE,
                                area.x, area.y, area.width, area.height );
            area.y += step_height;
            color.red   += r_inc;
            color.green += g_inc;
            color.blue  += b_inc;
        }
    }
}

static void
drawRegularBar( TorrentCellRenderer * self,
                const tr_info       * info,
                const tr_stat       * torStat,
                GdkDrawable         * drawable,
                GtkWidget           * widget,
                const GdkRectangle  * area )
{
#if 1
    const double verified = torStat->haveValid / (double)info->totalSize;
    const double unverified = torStat->haveUnchecked / (double)info->totalSize;
    const double unavailable = ( torStat->desiredSize
                       - torStat->desiredAvailable ) / (double)info->totalSize;
    const double unwanted = ( info->totalSize
                            - torStat->desiredSize ) / (double)info->totalSize;
#else /* for testing */
    const double verified = 0.5;
    const double unverified = 0.1;
    const double unavailable = 0.1;
    const double unwanted = 0.1;
#endif
    const double missing = 1.0 - verified - unverified - unavailable - unwanted;
    const int verifiedWidth = (int)( verified * area->width );
    const int unverifiedWidth = (int)( unverified * area->width );
    const int unavailableWidth = (int)( unavailable * area->width );
    const int unwantedWidth = (int)( unwanted * area->width );
    const int missingWidth = (int)( missing * area->width );

    const gboolean isActive = torStat->status == TR_STATUS_DOWNLOAD
                           || torStat->status == TR_STATUS_DONE
                           || torStat->status == TR_STATUS_SEED;
    const gboolean isChecking = torStat->status == TR_STATUS_CHECK
                             || torStat->status == TR_STATUS_CHECK_WAIT;

    int x = area->x;
    int w = 0;
    GdkGC * gc = gdk_gc_new( drawable );
    GdkRectangle rect = *area;

    if(( w = verifiedWidth )) {
        const GdkColor * colors;
        if( !isActive )
            colors = self->priv->color_paused;
        else if( torStat->status == TR_STATUS_DOWNLOAD )
            colors = self->priv->color_verified;
        else
            colors = self->priv->color_seeding;
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, colors, 2 );
        x += w;
    }

    if(( w = unverifiedWidth )) {
        const GdkColor * colors = isActive ? self->priv->color_verifying
                                           : self->priv->color_paused;
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, colors, 2 );
        x += w;
    }

    if(( w = missingWidth )) {
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, self->priv->color_missing, 2 );
        x += w;
    }

    if(( w = unwantedWidth )) {
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, self->priv->color_unwanted, 2 );
        x += w;
    }

    if(( w = unavailableWidth )) {
        const GdkColor * colors = isActive && self->priv->show_unavailable
                                ? self->priv->color_unavailable
                                : self->priv->color_missing;
        rect.x = x;
        rect.width = w;
        fillRect( self, gc, drawable, &rect, colors, 2 );
        x += w;
    }

    if( isChecking ) {
        const int checkedWidth = torStat->recheckProgress * area->width;
        const int h2 = area->height / 2;
        rect = *area;
        rect.y += h2;
        rect.height -= h2;
        fillRect( self, gc, drawable, &rect, self->priv->color_missing, 2 );
        rect.width = checkedWidth;
        fillRect( self, gc, drawable, &rect, self->priv->color_verifying, 2 );
    }

    gtk_paint_shadow( gtk_widget_get_style( widget ),
                      drawable,
                      GTK_STATE_NORMAL,
                      GTK_SHADOW_IN,
                      NULL,
                      widget,
                      NULL,
                      area->x, area->y, area->width, area->height );

    gdk_gc_unref( gc );
}

static void
torrent_cell_renderer_render( GtkCellRenderer      * cell,
                              GdkDrawable          * window,
                              GtkWidget            * widget,
                              GdkRectangle         * background_area,
                              GdkRectangle         * cell_area UNUSED,
                              GdkRectangle         * expose_area UNUSED,
                              GtkCellRendererState   flags)
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( cell );
    if( self && self->priv->tor )
    {
        const tr_torrent * tor = self->priv->tor;
        const tr_info * info = tr_torrentInfo( tor );
        const char * name = info->name;
        const tr_stat * torStat = tr_torrentStatCached( (tr_torrent*)tor );
        GdkRectangle my_bg;
        GdkRectangle my_cell;
        GdkRectangle my_expose;
        int xpad, ypad;
        int w, h;
        struct TorrentCellRendererPrivate * p = self->priv;
        GtkCellRenderer * text_renderer = torStat->error != 0
            ? p->text_renderer_err
            : p->text_renderer;

        g_object_get( self, "xpad", &xpad, "ypad", &ypad, NULL );

        my_bg = *background_area; 
        my_bg.x += xpad;
        my_bg.y += ypad;
        my_bg.width -= xpad*2;
        my_cell = my_expose = my_bg;

        /* above the progressbar */
        if( !p->minimal )
        {
            char * progressString = getProgressString( info, torStat );
            char * str = g_markup_printf_escaped( "<b>%s</b>\n<small>%s</small>",
                                                  name, progressString );
            g_object_set( text_renderer, "markup", str,
                                            "ellipsize", PANGO_ELLIPSIZE_NONE,
                                            NULL );
            gtk_cell_renderer_get_size( text_renderer,
                                        widget, NULL, NULL, NULL, &w, &h );
            my_bg.height     = 
            my_cell.height   =
            my_expose.height = h;
            g_object_set( text_renderer, "ellipsize", PANGO_ELLIPSIZE_END,
                                            NULL );
            gtk_cell_renderer_render( text_renderer,
                                      window, widget,
                                      &my_bg, &my_cell, &my_expose, flags );
            my_bg.y += h;
            my_cell.y += h;
            my_expose.y += h;

            g_free( str );
            g_free( progressString );
        }
        else
        {
            char * statusStr = getShortStatusString( torStat );
            char * str = g_markup_printf_escaped( "<small>%s</small>", statusStr );
            int w1, w2, h1, h2, tmp_h;
            GdkRectangle tmp_bg, tmp_cell, tmp_expose;

            /* get the dimensions for the name */
            g_object_set( text_renderer, "text", name,
                                         "ellipsize", PANGO_ELLIPSIZE_NONE,
                                         NULL );
            gtk_cell_renderer_get_size( text_renderer,
                                        widget, NULL, NULL, NULL, &w1, &h1 );

            /* get the dimensions for the short status string */
            g_object_set( text_renderer, "markup", str,
                                         "ellipsize", PANGO_ELLIPSIZE_NONE,
                                         NULL );
            gtk_cell_renderer_get_size( text_renderer,
                                        widget, NULL, NULL, NULL, &w2, &h2 );

            tmp_h = MAX( h1, h2 );

            /* short status */
            tmp_bg.x = my_bg.width - w2;
            tmp_bg.y = my_bg.y + (h2-h1)/2;
            tmp_bg.width = w2;
            tmp_bg.height = tmp_h;
            tmp_expose = tmp_cell = tmp_bg;
            g_object_set( text_renderer, "markup", str,
                                         "ellipsize", PANGO_ELLIPSIZE_END,
                                         NULL );
            gtk_cell_renderer_render( text_renderer,
                                      window, widget,
                                      &tmp_bg, &tmp_cell, &tmp_expose, flags );

            /* name */
            tmp_bg.x = my_bg.x;
            tmp_bg.width = my_bg.width - w2 - GUI_PAD_BIG;
            tmp_expose = tmp_cell = tmp_bg;
            g_object_set( text_renderer, "text", name,
                                         "ellipsize", PANGO_ELLIPSIZE_END,
                                         NULL );
            gtk_cell_renderer_render( text_renderer,
                                      window, widget,
                                      &tmp_bg, &tmp_cell, &tmp_expose, flags );

            my_bg.y = tmp_bg.y + tmp_bg.height;
            my_cell.y = tmp_cell.y + tmp_cell.height;
            my_expose.y += tmp_expose.y + tmp_cell.height;

            g_free( str );
            g_free( statusStr );
        }

        /* the progressbar */
        my_cell.height = p->bar_height;
        drawRegularBar( self, info, torStat, window, widget, &my_cell );
        my_bg.y     += my_cell.height;
        my_cell.y   += my_cell.height;
        my_expose.y += my_cell.height;

        /* below progressbar */
        if( !p->minimal )
        {
            char * statusString = getStatusString( torStat );
            char * str = g_markup_printf_escaped( "<small>%s</small>",
                                                  statusString );
            g_object_set( text_renderer, "markup", str,
                                         "ellipsize", PANGO_ELLIPSIZE_END,
                                         NULL );
            gtk_cell_renderer_get_size( text_renderer,
                                        widget, NULL, NULL, NULL, &w, &h );
            my_bg.height      =
            my_cell.height    =
            my_expose.height  = h;
            gtk_cell_renderer_render( text_renderer,
                                      window, widget,
                                      &my_bg, &my_cell, &my_expose, flags );

            g_free( str );
            g_free( statusString );
        }
    }
}

static void
v2c( GdkColor * color, const GValue * value )
{
    gdk_color_parse( g_value_get_string( value ), color );
}

static void
torrent_cell_renderer_set_property( GObject      * object,
                                    guint          property_id,
                                    const GValue * v,
                                    GParamSpec   * pspec)
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( object );
    struct TorrentCellRendererPrivate * p = self->priv;

    switch( property_id )
    {
        case P_COLOR_MISSING:       v2c( &p->color_missing[0],     v ); break;
        case P_COLOR_MISSING_2:     v2c( &p->color_missing[1],     v ); break;
        case P_COLOR_UNWANTED:      v2c( &p->color_unwanted[0],    v ); break;
        case P_COLOR_UNWANTED_2:    v2c( &p->color_unwanted[1],    v ); break;
        case P_COLOR_PAUSED:        v2c( &p->color_paused[0],      v ); break;
        case P_COLOR_PAUSED_2:      v2c( &p->color_paused[1],      v ); break;
        case P_COLOR_VERIFIED:      v2c( &p->color_verified[0],    v ); break;
        case P_COLOR_VERIFIED_2:    v2c( &p->color_verified[1],    v ); break;
        case P_COLOR_UNAVAILABLE:   v2c( &p->color_unavailable[0], v ); break;
        case P_COLOR_UNAVAILABLE_2: v2c( &p->color_unavailable[1], v ); break;
        case P_COLOR_VERIFYING:     v2c( &p->color_verifying[0],   v ); break;
        case P_COLOR_VERIFYING_2:   v2c( &p->color_verifying[1],   v ); break;
        case P_COLOR_SEEDING:       v2c( &p->color_seeding[0],     v ); break;
        case P_COLOR_SEEDING_2:     v2c( &p->color_seeding[1],     v ); break;
        case P_TORRENT:     p->tor = g_value_get_pointer( v ); break;
        case P_BAR_HEIGHT:  p->bar_height = g_value_get_int( v ); break;
        case P_MINIMAL:     p->minimal  = g_value_get_boolean( v ); break;
        case P_GRADIENT:    p->gradient = g_value_get_boolean( v ); break;
        case P_SHOW_UNAVAILABLE:
                            p->show_unavailable = g_value_get_boolean( v ); break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
c2v( GValue * value, const GdkColor * color )
{
    char buf[16];
    g_snprintf( buf, sizeof(buf), "#%2.2x%2.2x%2.2x",
                (color->red >> 8) & 0xff,
                (color->green >> 8) & 0xff,
                (color->blue >> 8) & 0xff );
    g_value_set_string( value, buf );
}

static void
torrent_cell_renderer_get_property( GObject      * object,
                                    guint          property_id,
                                    GValue       * v,
                                    GParamSpec   * pspec)
{
    const TorrentCellRenderer * self = TORRENT_CELL_RENDERER( object );
    struct TorrentCellRendererPrivate * p = self->priv;

    switch( property_id )
    {
        case P_COLOR_MISSING:       c2v( v, &p->color_missing[0] ); break;
        case P_COLOR_MISSING_2:     c2v( v, &p->color_missing[1] ); break;
        case P_COLOR_UNWANTED:      c2v( v, &p->color_unwanted[0] ); break;
        case P_COLOR_UNWANTED_2:    c2v( v, &p->color_unwanted[1] ); break;
        case P_COLOR_PAUSED:        c2v( v, &p->color_paused[0] ); break;
        case P_COLOR_PAUSED_2:      c2v( v, &p->color_paused[1] ); break;
        case P_COLOR_VERIFIED:      c2v( v, &p->color_verified[0] ); break;
        case P_COLOR_VERIFIED_2:    c2v( v, &p->color_verified[1] ); break;
        case P_COLOR_UNAVAILABLE:   c2v( v, &p->color_unavailable[0] ); break;
        case P_COLOR_UNAVAILABLE_2: c2v( v, &p->color_unavailable[1] ); break;
        case P_COLOR_VERIFYING:     c2v( v, &p->color_verifying[0] ); break;
        case P_COLOR_VERIFYING_2:   c2v( v, &p->color_verifying[1] ); break;
        case P_COLOR_SEEDING:       c2v( v, &p->color_seeding[0] ); break;
        case P_COLOR_SEEDING_2:     c2v( v, &p->color_seeding[1] ); break;
        case P_TORRENT:     g_value_set_pointer( v, p->tor ); break;
        case P_BAR_HEIGHT:  g_value_set_int( v, p->bar_height ); break;
        case P_MINIMAL:     g_value_set_boolean( v, p->minimal ); break;
        case P_GRADIENT:    g_value_set_boolean( v, p->gradient ); break;
        case P_SHOW_UNAVAILABLE:
                            g_value_set_boolean( v, p->show_unavailable ); break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
torrent_cell_renderer_dispose( GObject * o )
{
    TorrentCellRenderer * r = TORRENT_CELL_RENDERER( o );
    if( r && r->priv )
    {
        g_object_unref( G_OBJECT( r->priv->text_renderer ) );
        g_object_unref( G_OBJECT( r->priv->text_renderer_err ) );
        r->priv = NULL;
    }
}

static void
torrent_cell_renderer_class_init( TorrentCellRendererClass * klass )
{
    GObjectClass * gobject_class = G_OBJECT_CLASS( klass );
    GtkCellRendererClass * cell_class = GTK_CELL_RENDERER_CLASS( klass );

    g_type_class_add_private( klass,
                              sizeof(struct TorrentCellRendererPrivate) );

    parent_class = (GtkCellRendererClass*) g_type_class_peek_parent( klass );

    cell_class->render = torrent_cell_renderer_render;
    cell_class->get_size = torrent_cell_renderer_get_size;
    gobject_class->set_property = torrent_cell_renderer_set_property;
    gobject_class->get_property = torrent_cell_renderer_get_property;
    gobject_class->dispose = torrent_cell_renderer_dispose;

    g_object_class_install_property( gobject_class, P_TORRENT,
        g_param_spec_pointer( "torrent", NULL, "tr_torrent*",
                              G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_BAR_HEIGHT,
        g_param_spec_int( "bar-height", NULL, "Bar Height",
                          1, INT_MAX, DEFAULT_BAR_HEIGHT, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_MINIMAL,
        g_param_spec_boolean( "minimal", NULL, "Minimal Mode",
                              FALSE, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_GRADIENT,
        g_param_spec_boolean( "gradient", NULL, "Render Progress as a Gradient",
                              TRUE, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_SHOW_UNAVAILABLE,
        g_param_spec_boolean( "unavailable", NULL, "Show Unavailable",
                              FALSE, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_MISSING,
        g_param_spec_string( "missing-color", NULL, "Color for Missing Data",
                             DEFAULT_COLOR_MISSING, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_MISSING,
        g_param_spec_string( "missing-color-2", NULL, "Gradient Color for Missing Data",
                             DEFAULT_COLOR_MISSING_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_UNWANTED,
        g_param_spec_string( "unwanted-color", NULL, "Color for Unwanted Data",
                             DEFAULT_COLOR_UNWANTED, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_UNWANTED_2,
        g_param_spec_string( "unwanted-color-2", NULL, "Gradient Color for Unwanted Data",
                             DEFAULT_COLOR_UNWANTED_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_PAUSED,
        g_param_spec_string( "paused-color", NULL, "Color for Paused Data",
                             DEFAULT_COLOR_PAUSED, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_PAUSED_2,
        g_param_spec_string( "paused-color-2", NULL, "Gradient Color for Paused Data",
                             DEFAULT_COLOR_PAUSED_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_VERIFIED,
        g_param_spec_string( "verified-color", NULL, "Color for Verified Data",
                             DEFAULT_COLOR_VERIFIED, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_VERIFIED_2,
        g_param_spec_string( "verified-color-2", NULL, "Gradient Color for Verified Data",
                             DEFAULT_COLOR_VERIFIED_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_UNAVAILABLE,
        g_param_spec_string( "unavailable-color", NULL, "Color for Unavailable Data",
                             DEFAULT_COLOR_UNAVAILABLE, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_UNAVAILABLE_2,
        g_param_spec_string( "unavailable-color-2", NULL, "Gradient Color for Unavailable Data",
                             DEFAULT_COLOR_UNAVAILABLE_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_VERIFYING,
        g_param_spec_string( "verifying-color", NULL, "Color for Verifying Data",
                             DEFAULT_COLOR_VERIFYING, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_VERIFYING_2,
        g_param_spec_string( "verifying-color-2", NULL, "Gradient Color for Verifying Data",
                             DEFAULT_COLOR_VERIFYING_2, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_SEEDING,
        g_param_spec_string( "seeding-color", NULL, "Color for Seeding Data",
                             DEFAULT_COLOR_SEEDING, G_PARAM_READWRITE ) );

    g_object_class_install_property( gobject_class, P_COLOR_SEEDING_2,
        g_param_spec_string( "seeding-color-2", NULL, "Second Color for Seeding Data",
                             DEFAULT_COLOR_SEEDING_2, G_PARAM_READWRITE ) );
}

static void
torrent_cell_renderer_init( GTypeInstance * instance, gpointer g_class UNUSED )
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( instance );
    struct TorrentCellRendererPrivate * p;
    
    p = self->priv = G_TYPE_INSTANCE_GET_PRIVATE( self,
                         TORRENT_CELL_RENDERER_TYPE,
                         struct TorrentCellRendererPrivate );

    p->tor = NULL;
    p->text_renderer = gtk_cell_renderer_text_new( );
    p->text_renderer_err = gtk_cell_renderer_text_new(  );
    g_object_set( p->text_renderer_err, "foreground", "red", NULL );
    tr_object_ref_sink( p->text_renderer );
    tr_object_ref_sink( p->text_renderer_err );

    p->gradient = TRUE;
    p->show_unavailable = TRUE;
    p->bar_height = DEFAULT_BAR_HEIGHT;

    gdk_color_parse( DEFAULT_COLOR_VERIFIED,      &p->color_verified[0] );
    gdk_color_parse( DEFAULT_COLOR_VERIFIED_2,    &p->color_verified[1] );
    gdk_color_parse( DEFAULT_COLOR_MISSING,       &p->color_missing[0] );
    gdk_color_parse( DEFAULT_COLOR_MISSING_2,     &p->color_missing[1] );
    gdk_color_parse( DEFAULT_COLOR_UNWANTED,      &p->color_unwanted[0] );
    gdk_color_parse( DEFAULT_COLOR_UNWANTED_2,    &p->color_unwanted[1] );
    gdk_color_parse( DEFAULT_COLOR_UNAVAILABLE,   &p->color_unavailable[0] );
    gdk_color_parse( DEFAULT_COLOR_UNAVAILABLE_2, &p->color_unavailable[1] );
    gdk_color_parse( DEFAULT_COLOR_VERIFYING,     &p->color_verifying[0] );
    gdk_color_parse( DEFAULT_COLOR_VERIFYING_2,   &p->color_verifying[1] );
    gdk_color_parse( DEFAULT_COLOR_SEEDING,       &p->color_seeding[0] );
    gdk_color_parse( DEFAULT_COLOR_SEEDING_2,     &p->color_seeding[1] );
    gdk_color_parse( DEFAULT_COLOR_PAUSED,        &p->color_paused[0] );
    gdk_color_parse( DEFAULT_COLOR_PAUSED_2,      &p->color_paused[1] );
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
            NULL, /* base_init */
            NULL, /* base_finalize */
            (GClassInitFunc)torrent_cell_renderer_class_init,
            NULL, /* class_finalize */
            NULL, /* class_data */
            sizeof( TorrentCellRenderer ),
            0, /* n_preallocs */
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
    return (GtkCellRenderer *) g_object_new( TORRENT_CELL_RENDERER_TYPE, NULL );
}
