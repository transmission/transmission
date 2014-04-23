/*
 * tr-status-menu-button.c
 * This file is part of tr
 *
 * Copyright (C) 2008 - Jesse van den Kieboom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include "tr-status-menu-button.h"

struct _TrStatusMenuButtonPrivate
{
    GtkWidget* label;
};

struct _TrStatusMenuButtonClassPrivate
{
    GtkCssProvider* css;
};

/* Properties */
enum
{
    PROP_0,
    PROP_LABEL
};

G_DEFINE_TYPE_WITH_CODE(TrStatusMenuButton, tr_status_menu_button, GTK_TYPE_MENU_BUTTON,
    G_ADD_PRIVATE(TrStatusMenuButton)
    g_type_add_class_private(g_define_type_id, sizeof(TrStatusMenuButtonClassPrivate)))

static void tr_status_menu_button_get_property(GObject* object, guint prop_id, GValue* value, GParamSpec* pspec)
{
    TrStatusMenuButton* obj = TR_STATUS_MENU_BUTTON(object);

    switch (prop_id)
    {
    case PROP_LABEL:
        g_value_set_string(value, tr_status_menu_button_get_label(obj));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void tr_status_menu_button_set_property(GObject* object, guint prop_id, GValue const* value, GParamSpec* pspec)
{
    TrStatusMenuButton* obj = TR_STATUS_MENU_BUTTON(object);

    switch (prop_id)
    {
    case PROP_LABEL:
        tr_status_menu_button_set_label(obj, g_value_get_string(value));
        break;

    default:
        G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
        break;
    }
}

static void tr_status_menu_button_class_init(TrStatusMenuButtonClass* klass)
{
    static gchar const style[] =
        "* {\n"
            "-GtkButton-default-border : 0;\n"
            "-GtkButton-default-outside-border : 0;\n"
            "-GtkButton-inner-border: 0;\n"
            "-GtkWidget-focus-line-width : 0;\n"
            "-GtkWidget-focus-padding : 0;\n"
            "padding: 1px 8px 2px 4px;\n"
        "}";

    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GtkWidgetClass* widget_class = GTK_WIDGET_CLASS(klass);

    object_class->get_property = &tr_status_menu_button_get_property;
    object_class->set_property = &tr_status_menu_button_set_property;

    g_object_class_override_property(object_class, PROP_LABEL, "label");

    /* Bind class to template */
    gtk_widget_class_set_template_from_resource(widget_class, "/com/transmissionbt/transmission/tr-status-menu-button.ui");
    gtk_widget_class_bind_template_child_private(widget_class, TrStatusMenuButton, label);

    klass->priv = G_TYPE_CLASS_GET_PRIVATE(klass, TR_TYPE_STATUS_MENU_BUTTON, TrStatusMenuButtonClassPrivate);

    klass->priv->css = gtk_css_provider_new();
    gtk_css_provider_load_from_data(klass->priv->css, style, -1, NULL);
}

static void tr_status_menu_button_init(TrStatusMenuButton* self)
{
    GtkStyleContext* context;

    self->priv = tr_status_menu_button_get_instance_private(self);

    gtk_widget_init_template(GTK_WIDGET(self));

    /* make it as small as possible */
    context = gtk_widget_get_style_context(GTK_WIDGET(self));
    gtk_style_context_add_provider(context, GTK_STYLE_PROVIDER(TR_STATUS_MENU_BUTTON_GET_CLASS(self)->priv->css),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

/**
 * tr_status_menu_button_new:
 * @label: (allow-none):
 */
GtkWidget* tr_status_menu_button_new(void)
{
    return g_object_new(TR_TYPE_STATUS_MENU_BUTTON, NULL);
}

/* we cannot rely on gtk_button_set_label since it manually replaces
 * the internal child instead of just setting the property :( */

void tr_status_menu_button_set_label(TrStatusMenuButton* button, gchar const* label)
{
    gtk_label_set_markup(GTK_LABEL(button->priv->label), label);
}

gchar const* tr_status_menu_button_get_label(TrStatusMenuButton* button)
{
    return gtk_label_get_label(GTK_LABEL(button->priv->label));
}
