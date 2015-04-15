/*
    Quick Search Plugin for DeaDBeeF audio player
    Copyright (C) 2014 Christian Boxdörfer <christian.boxdoerfer@posteo.de>

    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License
    as published by the Free Software Foundation; either version 2
    of the License, or (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <gtk/gtk.h>
#include <deadbeef/deadbeef.h>
#include <deadbeef/gtkui_api.h>

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

static DB_misc_t plugin;
static DB_functions_t *deadbeef = NULL;
static ddb_gtkui_t *gtkui_plugin = NULL;
static GtkWidget *searchentry = NULL;

typedef struct {
    ddb_gtkui_widget_t base;
} w_quick_search_t;

static void
search_process (const char *text) {
    ddb_playlist_t *plt = deadbeef->plt_get_curr ();
    deadbeef->plt_search_process (plt, text);
    deadbeef->plt_unref (plt);

    int row = deadbeef->pl_get_cursor (PL_SEARCH);
    if (row >= deadbeef->pl_getcount (PL_SEARCH)) {
        deadbeef->pl_set_cursor (PL_SEARCH, deadbeef->pl_getcount (PL_SEARCH) - 1);
    }
}

void
on_searchentry_changed                 (GtkEditable     *editable,
                                        gpointer         user_data)
{
    GtkEntry *entry = GTK_ENTRY (editable);
    const gchar *text = gtk_entry_get_text (entry);
    search_process (text);
#if (DDB_API_LEVEL >= 8)
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_SELECTION, 0);
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_SEARCHRESULT, 0);
    deadbeef->sendmessage (DB_EV_FOCUS_SELECTION, 0, PL_MAIN, 0);
#else
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, 0, 0);
#endif
}

static int
quick_search_on_action (DB_plugin_action_t *action, int ctx)
{
    if (searchentry) {
        gtk_widget_grab_focus (searchentry);
    }
    return 0;
}

static DB_plugin_action_t
quick_search_action = {
    .title = "Quick search",
    .name = "quick_search",
    .flags = DB_ACTION_COMMON,
    .callback2 = quick_search_on_action,
    .next = NULL
};

static DB_plugin_action_t *
quick_search_get_actions (DB_playItem_t *it)
{
    return &quick_search_action;
}

static void
quick_search_init (ddb_gtkui_widget_t *ww) {
    w_quick_search_t *w = (w_quick_search_t *)ww;

    searchentry = gtk_entry_new ();
    gtk_entry_set_icon_from_icon_name (GTK_ENTRY (searchentry), GTK_ENTRY_ICON_PRIMARY, "edit-find");
    gtk_entry_set_invisible_char (GTK_ENTRY (searchentry), 8226);
    gtk_entry_set_activates_default (GTK_ENTRY (searchentry), TRUE);
    gtk_widget_show (searchentry);

    gtk_container_add (GTK_CONTAINER (w->base.widget), searchentry);
    g_signal_connect ((gpointer) searchentry, "changed",
            G_CALLBACK (on_searchentry_changed),
            NULL);
}

static void
quick_search_destroy (ddb_gtkui_widget_t *w) {
}

static ddb_gtkui_widget_t *
w_quick_search_create (void) {
    w_quick_search_t *w = malloc (sizeof (w_quick_search_t));
    memset (w, 0, sizeof (w_quick_search_t));

    w->base.widget = gtk_event_box_new ();
    w->base.destroy  = quick_search_destroy;
    w->base.init = quick_search_init;
    gtkui_plugin->w_override_signals (w->base.widget, w);

    return (ddb_gtkui_widget_t *)w;
}

static int
quick_search_connect (void)
{
    gtkui_plugin = (ddb_gtkui_t *) deadbeef->plug_get_for_id (DDB_GTKUI_PLUGIN_ID);
    if (gtkui_plugin) {
        //trace("using '%s' plugin %d.%d\n", DDB_GTKUI_PLUGIN_ID, gtkui_plugin->gui.plugin.version_major, gtkui_plugin->gui.plugin.version_minor );
        if (gtkui_plugin->gui.plugin.version_major == 2) {
            //printf ("fb api2\n");
            // 0.6+, use the new widget API
            gtkui_plugin->w_reg_widget ("Quick search", DDB_WF_SINGLE_INSTANCE, w_quick_search_create, "quick_search", NULL);
            return 0;
        }
    }
    return -1;
}

static int
quick_search_disconnect (void)
{
    gtkui_plugin = NULL;
    return 0;
}

// define plugin interface
static DB_misc_t plugin = {
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 5,
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
    .plugin.type = DB_PLUGIN_MISC,
    .plugin.name = "Quick search",
    .plugin.descr = "A widget to perform a quick search",
    .plugin.copyright =
        "Copyright (C) 2015 Christian Boxdörfer <christian.boxdoerfer@posteo.de>\n"
        "\n"
        "This program is free software; you can redistribute it and/or\n"
        "modify it under the terms of the GNU General Public License\n"
        "as published by the Free Software Foundation; either version 2\n"
        "of the License, or (at your option) any later version.\n"
        "\n"
        "This program is distributed in the hope that it will be useful,\n"
        "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
        "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
        "GNU General Public License for more details.\n"
        "\n"
        "You should have received a copy of the GNU General Public License\n"
        "along with this program; if not, write to the Free Software\n"
        "Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.\n"
    ,
    .plugin.website = "http://www.github.com/cboxdoerfer/ddb_quick_search",
    .plugin.connect  = quick_search_connect,
    .plugin.disconnect  = quick_search_disconnect,
    .plugin.get_actions     = quick_search_get_actions,
};

#if !GTK_CHECK_VERSION(3,0,0)
DB_plugin_t *
ddb_misc_quick_search_GTK2_load (DB_functions_t *ddb) {
    deadbeef = ddb;
    return &plugin.plugin;
}
#else
DB_plugin_t *
ddb_misc_quick_search_GTK3_load (DB_functions_t *ddb) {
    deadbeef = ddb;
    return &plugin.plugin;
}
#endif
