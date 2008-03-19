/*
 * This file Copyright (C) 2007-2008 Charles Kerr <charles@rebelbase.com>
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
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkcellrendererprogress.h>
#include <glib/gi18n.h>
#include <libtransmission/transmission.h>
#include "hig.h"
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
                  /* %1$s is how much we've got,
                     %2$s is how much we'll have when done,
                     %3$.2f%% is a percentage of the two */
                  _("%1$s of %2$s (%3$.2f%%)"),
                  tr_strlsize( buf1, haveTotal, sizeof(buf1) ),
                  tr_strlsize( buf2, torStat->desiredSize, sizeof(buf2) ),
                  torStat->percentDone * 100.0 );
    else if( !isSeed )
        str = g_strdup_printf(
                  /* %1$s is how much we've got,
                     %2$s is the torrent's total size,
                     %3$.2f%% is a percentage of the two,
                     %4$s is how much we've uploaded,
                     %5$s is our upload-to-download ratio */
                  _("%1$s of %2$s (%3$.2f%%), uploaded %4$s (Ratio: %5$s)"),
                  tr_strlsize( buf1, haveTotal, sizeof(buf1) ),
                  tr_strlsize( buf2, info->totalSize, sizeof(buf2) ),
                  torStat->percentComplete * 100.0,
                  tr_strlsize( buf3, torStat->uploadedEver, sizeof(buf3) ),
                  tr_strlratio( buf4, torStat->ratio, sizeof( buf4 ) ) );
    else
        str = g_strdup_printf(
                  /* %1$s is the torrent's total size,
                     %2$s is how much we've uploaded,
                     %3$s is our upload-to-download ratio */
                  _("%1$s, uploaded %2$s (Ratio: %3$s)"),
                  tr_strlsize( buf1, info->totalSize, sizeof(buf1) ),
                  tr_strlsize( buf2, torStat->uploadedEver, sizeof(buf2) ),
                  tr_strlratio( buf3, torStat->ratio, sizeof( buf3 ) ) );

    /* add time when downloading */
    if( torStat->status == TR_STATUS_DOWNLOAD )
    {
        const int eta = torStat->eta;
        GString * gstr = g_string_new( str );
        g_string_append( gstr, " - " );
        if( eta < 0 )
            g_string_append( gstr, _( "Stalled" ) );
        else {
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
getShortTransferString( const tr_stat * torStat, char * buf, size_t buflen )
{
    char downStr[32], upStr[32];
    const int haveDown = torStat->peersSendingToUs > 0;
    const int haveUp = torStat->peersGettingFromUs > 0;

    if( haveDown )
        tr_strlspeed( downStr, torStat->rateDownload, sizeof(downStr) );
    if( haveUp )
        tr_strlspeed( upStr, torStat->rateUpload, sizeof(upStr) );

    if( haveDown && haveUp )
        /* Translators: do not translate the "speed|" disambiguation prefix.
           %1$s is the download speed
           %2$s is the upload speed */
        g_snprintf( buf, buflen, Q_( "speed|Down: %1$s, Up: %2$s"), downStr, upStr );
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

    switch( torStat->status )
    {
        case TR_STATUS_STOPPED:
            g_string_assign( gstr, _("Paused") );
            break;

        case TR_STATUS_CHECK_WAIT:
            g_string_assign( gstr, _( "Waiting to verify local data" ) );
            break;

        case TR_STATUS_CHECK:
            g_string_append_printf( gstr, _("Verifying local data (%.1f%% tested)"),
                                    torStat->recheckProgress * 100.0 );
            break;

        case TR_STATUS_DOWNLOAD:
        case TR_STATUS_SEED:
        case TR_STATUS_DONE: {
            char buf[128];
            if( torStat->status != TR_STATUS_DOWNLOAD ) {
                tr_strlratio( buf, torStat->ratio, sizeof( buf ) );
                g_string_append_printf( gstr, _("Ratio: %s" ), buf );
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
                ngettext( "Downloading from %1$'d of %2$'d connected peer",
                          "Downloading from %1$'d of %2$'d connected peers",
                          torStat->peersConnected ),
                torStat->peersSendingToUs,
                torStat->peersConnected );
            break;

        case TR_STATUS_DONE:
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
    GtkCellRenderer * progress_renderer;
    int bar_height;
    gboolean minimal;
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
torrent_cell_renderer_render( GtkCellRenderer      * cell,
                              GdkDrawable          * window,
                              GtkWidget            * widget,
                              GdkRectangle         * background_area,
                              GdkRectangle         * cell_area UNUSED,
                              GdkRectangle         * expose_area UNUSED,
                              GtkCellRendererState   flags)
{
    TorrentCellRenderer * self = TORRENT_CELL_RENDERER( cell );

#ifdef TEST_RTL
    GtkTextDirection real_dir = gtk_widget_get_direction( widget );
    gtk_widget_set_direction( widget, GTK_TEXT_DIR_RTL );
#endif

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
        const gboolean isActive = torStat->status != TR_STATUS_STOPPED;

        g_object_get( self, "xpad", &xpad, "ypad", &ypad, NULL );

        my_bg = *background_area; 
        my_bg.x += xpad;
        my_bg.y += ypad;
        my_bg.width -= xpad*2;
        my_cell = my_expose = my_bg;

        g_object_set( text_renderer, "sensitive", isActive, NULL );
        g_object_set( p->progress_renderer, "sensitive", isActive, NULL );

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
        if( 1 )
        {
            const double havePercent = ( torStat->haveValid + torStat->haveUnchecked )
                                                              / (double)info->totalSize;
            g_object_set( p->progress_renderer, "value", (int)(havePercent*100.0), 
                                                "text", "",
                                                NULL );
            gtk_cell_renderer_render( p->progress_renderer,
                                      window, widget,
                                      &my_cell, &my_cell, &my_cell, flags );
 
        }
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

#ifdef TEST_RTL
    gtk_widget_set_direction( widget, real_dir );
#endif
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
        case P_TORRENT:     p->tor = g_value_get_pointer( v ); break;
        case P_BAR_HEIGHT:  p->bar_height = g_value_get_int( v ); break;
        case P_MINIMAL:     p->minimal  = g_value_get_boolean( v ); break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
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
        case P_TORRENT:     g_value_set_pointer( v, p->tor ); break;
        case P_BAR_HEIGHT:  g_value_set_int( v, p->bar_height ); break;
        case P_MINIMAL:     g_value_set_boolean( v, p->minimal ); break;
        default:
            G_OBJECT_WARN_INVALID_PROPERTY_ID( object, property_id, pspec );
            break;
    }
}

static void
torrent_cell_renderer_dispose( GObject * o )
{
    TorrentCellRenderer * r = TORRENT_CELL_RENDERER( o );
    GObjectClass * parent;

    if( r && r->priv )
    {
        g_object_unref( G_OBJECT( r->priv->text_renderer ) );
        g_object_unref( G_OBJECT( r->priv->text_renderer_err ) );
        g_object_unref( G_OBJECT( r->priv->progress_renderer ) );
        r->priv = NULL;
    }

    parent = g_type_class_peek( g_type_parent( TORRENT_CELL_RENDERER_TYPE ) );
    parent->dispose( o );
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
    p->progress_renderer = gtk_cell_renderer_progress_new(  );
    g_object_set( p->text_renderer_err, "foreground", "red", NULL );
    tr_object_ref_sink( p->text_renderer );
    tr_object_ref_sink( p->text_renderer_err );
    tr_object_ref_sink( p->progress_renderer );

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
