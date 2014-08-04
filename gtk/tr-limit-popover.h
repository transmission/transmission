/*
 * tr-limit-popover.h
 * This file is part of Transmission
 *
 * Copyright (C) 2014 - Derek Willian Stavis
 *
 * transmission is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * transmission is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with transmission. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <glib-object.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

#define TR_TYPE_LIMIT_POPOVER (tr_limit_popover_get_type())
#define TR_LIMIT_POPOVER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), TR_TYPE_LIMIT_POPOVER, TrLimitPopover))
#define TR_LIMIT_POPOVER_CONST(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),TR_TYPE_LIMIT_POPOVER, TrLimitPopover const))
#define TR_LIMIT_POPOVER_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass), TR_TYPE_LIMIT_POPOVER, TrLimitPopoverClass))
#define TR_IS_LIMIT_POPOVER(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj), TR_TYPE_LIMIT_POPOVER))
#define TR_IS_LIMIT_POPOVER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass), TR_TYPE_LIMIT_POPOVER))
#define TR_LIMIT_POPOVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), TR_TYPE_LIMIT_POPOVER, TrLimitPopoverClass))

typedef struct _TrLimitPopover TrLimitPopover;
typedef struct _TrLimitPopoverClass TrLimitPopoverClass;
typedef struct _TrLimitPopoverPrivate TrLimitPopoverPrivate;

struct _TrLimitPopover
{
    GtkGrid parent;

    TrLimitPopoverPrivate* priv;
};

struct _TrLimitPopoverClass
{
    GtkGridClass parent_class;

    void (* speed_limit_up)(TrLimitPopover* widget);
    void (* speed_limit_down)(TrLimitPopover* widget);
    void (* ratio_limit)(TrLimitPopover* widget);
};

GType tr_limit_popover_get_type(void) G_GNUC_CONST;

TrLimitPopover* tr_limit_popover_new(void);

int tr_limit_popover_get_speed_limit_up(TrLimitPopover* popover);
int tr_limit_popover_get_speed_limit_down(TrLimitPopover* popover);
double tr_limit_popover_get_ratio_limit(TrLimitPopover* popover);

void tr_limit_popover_set_speed_limit_up(TrLimitPopover* popover, int value);
void tr_limit_popover_set_speed_limit_down(TrLimitPopover* popover, int value);
void tr_limit_popover_set_ratio_limit(TrLimitPopover* popover, double value);

void tr_limit_popover_set_speed_limit_up_enabled(TrLimitPopover* popover, gboolean enabled);
void tr_limit_popover_set_speed_limit_down_enabled(TrLimitPopover* popover, gboolean enabled);
void tr_limit_popover_set_ratio_limit_enabled(TrLimitPopover* popover, gboolean enabled);

G_END_DECLS

/* ex:set ts=4 noet: */
