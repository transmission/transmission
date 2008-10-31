/*
 * @file libsexy/sexy-icon-entry.h Entry widget
 *
 * @Copyright (C) 2004-2006 Christian Hammond.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA  02111-1307, USA.
 */
#ifndef _SEXY_ICON_ENTRY_H_
#define _SEXY_ICON_ENTRY_H_

typedef struct _SexyIconEntry      SexyIconEntry;
typedef struct _SexyIconEntryClass SexyIconEntryClass;
typedef struct _SexyIconEntryPriv  SexyIconEntryPriv;

#include <gtk/gtk.h>

#define SEXY_TYPE_ICON_ENTRY ( sexy_icon_entry_get_type( ) )
#define SEXY_ICON_ENTRY( obj ) \
    ( G_TYPE_CHECK_INSTANCE_CAST( ( obj ), SEXY_TYPE_ICON_ENTRY,\
                                 SexyIconEntry ) )
#define SEXY_ICON_ENTRY_CLASS( klass ) \
    ( G_TYPE_CHECK_CLASS_CAST( ( klass ), SEXY_TYPE_ICON_ENTRY,\
                              SexyIconEntryClass ) )
#define SEXY_IS_ICON_ENTRY( obj ) \
    ( G_TYPE_CHECK_INSTANCE_TYPE( ( obj ), SEXY_TYPE_ICON_ENTRY ) )
#define SEXY_IS_ICON_ENTRY_CLASS( klass ) \
    ( G_TYPE_CHECK_CLASS_TYPE( ( klass ), SEXY_TYPE_ICON_ENTRY ) )
#define SEXY_ICON_ENTRY_GET_CLASS( obj ) \
    ( G_TYPE_INSTANCE_GET_CLASS ( ( obj ), SEXY_TYPE_ICON_ENTRY,\
                                 SexyIconEntryClass ) )

typedef enum
{
    SEXY_ICON_ENTRY_PRIMARY,
    SEXY_ICON_ENTRY_SECONDARY
} SexyIconEntryPosition;

struct _SexyIconEntry
{
    GtkEntry             parent_object;

    SexyIconEntryPriv *  priv;

    void                 ( *gtk_reserved1 )( void );
    void                 ( *gtk_reserved2 )( void );
    void                 ( *gtk_reserved3 )( void );
    void                 ( *gtk_reserved4 )( void );
};

struct _SexyIconEntryClass
{
    GtkEntryClass    parent_class;

    /* Signals */
    void    ( *icon_pressed )( SexyIconEntry *       entry,
                               SexyIconEntryPosition icon_pos,
                               int                   button );
    void    ( *icon_released )( SexyIconEntry *       entry,
                                SexyIconEntryPosition icon_pos,
                                int                   button );

    void    ( *gtk_reserved1 )( void );
    void    ( *gtk_reserved2 )( void );
    void    ( *gtk_reserved3 )( void );
    void    ( *gtk_reserved4 )( void );
};

G_BEGIN_DECLS

GType      sexy_icon_entry_get_type( void );

GtkWidget *sexy_icon_entry_new( void );

void       sexy_icon_entry_set_icon( SexyIconEntry *       entry,
                                     SexyIconEntryPosition position,
                                     GtkImage *            icon );

void       sexy_icon_entry_set_icon_highlight(
    SexyIconEntry *entry,
    SexyIconEntryPosition
                   position,
    gboolean
                   highlight );

GtkImage * sexy_icon_entry_get_icon( const SexyIconEntry * entry,
                                     SexyIconEntryPosition position );

gboolean   sexy_icon_entry_get_icon_highlight(
    const SexyIconEntry *entry,
    SexyIconEntryPosition
                         position );

void       sexy_icon_entry_add_clear_button( SexyIconEntry *icon_entry );

G_END_DECLS

#endif /* _SEXY_ICON_ENTRY_H_ */
