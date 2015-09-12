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
#include <gdk/gdkkeysyms.h>
#include <deadbeef/deadbeef.h>
#include <deadbeef/gtkui_api.h>

//#define trace(...) { fprintf(stderr, __VA_ARGS__); }
#define trace(fmt,...)

#define CONFSTR_APPEND_SEARCH_STRING "quick_search.append_search_string"
#define CONFSTR_SEARCH_IN "quick_search.search_in"
#define CONFSTR_AUTOSEARCH "quick_search.autosearch"

static DB_misc_t plugin;
static DB_functions_t *deadbeef = NULL;
static ddb_gtkui_t *gtkui_plugin = NULL;
static GtkWidget *searchentry = NULL;
static int resizetimer = 0;
static ddb_playlist_t *last_active_plt = NULL;

// search in modes
enum search_in_mode_t {
    SEARCH_INLINE = 0,
    SEARCH_PLAYLIST = 1,
    SEARCH_ALL_PLAYLISTS = 2,
};

static int config_search_in = SEARCH_INLINE;
static int config_autosearch = TRUE;
static int config_append_search_string = FALSE;

typedef struct {
    ddb_gtkui_widget_t base;
    GtkWidget *popup;
} w_quick_search_t;

static gboolean
is_quick_search_playlist (ddb_playlist_t *plt)
{
    if (plt && deadbeef->plt_find_meta (plt, "quick_search") != NULL) {
        return TRUE;
    }
    return FALSE;
}

static ddb_playlist_t *
get_last_active_playlist ()
{
    ddb_playlist_t *plt = NULL;
    if (!last_active_plt) {
        plt = deadbeef->plt_get_curr ();
    }
    else {
        plt = last_active_plt;
        deadbeef->plt_ref (plt);
    }
    return plt;
}

static void
set_last_active_playlist (ddb_playlist_t *plt)
{
    if (!is_quick_search_playlist (plt) && plt != last_active_plt) {
        if (last_active_plt) {
            deadbeef->plt_unref (last_active_plt);
        }
        last_active_plt = plt;
        deadbeef->plt_ref (last_active_plt);
    }
}

static int
get_quick_search_playlist () {
    // find existing one
    int plt_count = deadbeef->plt_get_count();
    for (int i = 0; i < plt_count; i++) {
        ddb_playlist_t *plt = deadbeef->plt_get_for_idx (i);
        if (plt) {
            if (is_quick_search_playlist (plt)) {
                deadbeef->plt_unref (plt);
                return i;
            }
            deadbeef->plt_unref (plt);
        }
    }

    // add new playlist
    int idx = deadbeef->plt_add (plt_count, "Quick Search");
    ddb_playlist_t *plt = deadbeef->plt_get_for_idx (idx);
    deadbeef->plt_add_meta (plt, "quick_search", "test");
    deadbeef->plt_unref (plt);
    return idx;
}

static void
set_default_quick_search_playlist_title ()
{
    deadbeef->pl_lock ();
    int plt_idx = get_quick_search_playlist ();
    if (plt_idx >= 0) {
        ddb_playlist_t *plt = deadbeef->plt_get_for_idx (plt_idx);
        if (plt) {
            char new_title[1024] = "";
            snprintf (new_title, sizeof (new_title), "%s", "Quick Search");
            deadbeef->plt_set_title (plt, new_title);
            deadbeef->plt_unref (plt);
        }
    }
    deadbeef->pl_unlock ();
}

static void
append_search_string_to_plt_title (const char *search_string)
{
    if (!search_string) {
        return;
    }
    deadbeef->pl_lock ();
    int plt_idx = get_quick_search_playlist ();
    if (plt_idx >= 0) {
        ddb_playlist_t *plt = deadbeef->plt_get_for_idx (plt_idx);
        if (plt) {
            char new_title[1024] = "";
            if (!strcmp (search_string, "")) {
                snprintf (new_title, sizeof (new_title), "%s", "Quick Search");
            }
            else {
                snprintf (new_title, sizeof (new_title), "%s [%s]", "Quick Search", search_string);
            }
            deadbeef->plt_set_title (plt, new_title);
            deadbeef->plt_unref (plt);
        }
    }
    deadbeef->pl_unlock ();
}

static void
copy_selected_tracks (ddb_playlist_t *from, ddb_playlist_t *to)
{
    if (!from || !to) {
        return;
    }
    deadbeef->plt_set_curr (to);

    int sel_count = deadbeef->plt_get_sel_count (deadbeef->plt_get_idx (from));
    uint32_t *track_list = malloc ((sel_count) * sizeof (uint32_t));
    if (track_list) {
        int track_idx = 0;
        int i = 0;
        DB_playItem_t *it = deadbeef->plt_get_first (from, PL_MAIN);
        for (; it; track_idx++) {
            if (deadbeef->pl_is_selected (it)) {
                track_list[i] = track_idx;
                i++;
            }
            DB_playItem_t *next = deadbeef->pl_get_next (it, PL_MAIN);
            deadbeef->pl_item_unref (it);
            it = next;
        }
        DB_playItem_t *after = deadbeef->plt_get_first (to, PL_MAIN);
        deadbeef->plt_copy_items (to, PL_MAIN, from, after, track_list, sel_count);
        if (after) {
            deadbeef->pl_item_unref (after);
        }
        free (track_list);
    }
}

static void
on_add_quick_search_list ()
{
    int new_plt_idx = get_quick_search_playlist ();
    ddb_playlist_t *plt_to = deadbeef->plt_get_for_idx (new_plt_idx);

    if (config_search_in != SEARCH_ALL_PLAYLISTS) {
        ddb_playlist_t *plt_from = get_last_active_playlist ();
        if (is_quick_search_playlist (plt_from)) {
            deadbeef->plt_unref (plt_from);
            return;
        }
        deadbeef->plt_clear (plt_to);
        copy_selected_tracks (plt_from, plt_to);
        if (plt_from) {
            deadbeef->plt_unref (plt_from);
        }
    }
    else if (config_search_in == SEARCH_ALL_PLAYLISTS) {
        deadbeef->plt_clear (plt_to);
        int plt_count = deadbeef->plt_get_count ();
        for (int i = 0; i < plt_count; i++) {
            ddb_playlist_t *plt_from = deadbeef->plt_get_for_idx (i);
            if (!plt_from) {
                continue;
            }
            if (!is_quick_search_playlist (plt_from)) {
                copy_selected_tracks (plt_from, plt_to);
            }
            deadbeef->plt_unref (plt_from);
        }
    }

    if (plt_to) {
        deadbeef->plt_unref (plt_to);
    }
#if (DDB_API_LEVEL >= 8)
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_CONTENT, 0);
#else
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, 0, 0);
#endif
}

static void
on_searchentry_activate                (GtkEntry        *entry,
                                        gpointer         user_data)
{
    ddb_playlist_t *plt = get_last_active_playlist ();
    if (plt) {
        DB_playItem_t *it = deadbeef->plt_get_first (plt, PL_MAIN);
        while (it) {
            if (deadbeef->pl_is_selected (it)) {
                break;
            }
            DB_playItem_t *next = deadbeef->pl_get_next (it, PL_MAIN);
            deadbeef->pl_item_unref (it);
            it = next;
            if (!it) {
                deadbeef->sendmessage (DB_EV_PLAY_NUM, 0, 0, 0);
            }
        }
        if (it) {
            deadbeef->pl_item_unref (it);
        }
        deadbeef->plt_unref (plt);
    }
}

static void
on_searchentry_icon_press (GtkEntry            *entry,
                           GtkEntryIconPosition icon_pos,
                           GdkEvent            *event,
                           gpointer             user_data)
{
    w_quick_search_t *w = user_data;
    if (icon_pos == GTK_ENTRY_ICON_PRIMARY) {
        gtk_menu_popup (GTK_MENU (w->popup), NULL, NULL, NULL, NULL, 0, gtk_get_current_event_time ());
    }
    else {
        gtk_entry_set_text (entry, "");
    }
}

static void
searchentry_perform_autosearch ()
{
    switch (config_search_in) {
#if (DDB_API_LEVEL >= 8)
        case SEARCH_INLINE:
            deadbeef->sendmessage (DB_EV_FOCUS_SELECTION, 0, PL_MAIN, 0);
            break;
#endif
        case SEARCH_PLAYLIST:
        case SEARCH_ALL_PLAYLISTS:
            on_add_quick_search_list ();
            break;
    }
}

static gboolean
search_process (gpointer userdata);

static gboolean
on_searchentry_key_press_event           (GtkWidget       *widget,
                                        GdkEventKey     *event,
                                        gpointer         user_data)
{
#if GTK_CHECK_VERSION(3,0,0)
    if (event->keyval == GDK_KEY_Return) {
#else
    if (event->keyval == GDK_Return) {
#endif
        if (!config_autosearch) {
            GtkEntry *entry = GTK_ENTRY (widget);
            const gchar *text = gtk_entry_get_text (entry);
            if (resizetimer) {
                g_source_remove (resizetimer);
                resizetimer = 0;
            }
            resizetimer = g_timeout_add (100, search_process, (void *)text);
        }
        else {
            on_searchentry_activate (NULL, 0);
        }
        return TRUE;
    }
    return FALSE;
}

static void
update_list ()
{
#if (DDB_API_LEVEL >= 8)
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_SELECTION, 0);
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, DDB_PLAYLIST_CHANGE_SEARCHRESULT, 0);
#else
    deadbeef->sendmessage (DB_EV_PLAYLISTCHANGED, 0, 0, 0);
#endif
}

static gboolean
search_process (gpointer userdata) {
    if (resizetimer) {
        g_source_remove (resizetimer);
        resizetimer = 0;
    }
    g_return_val_if_fail (userdata != NULL, FALSE);

    const char *text = userdata;
    deadbeef->pl_lock ();
    if (config_search_in != SEARCH_ALL_PLAYLISTS) {
        ddb_playlist_t *plt = deadbeef->plt_get_curr ();
        if (plt) {
            if (is_quick_search_playlist (plt)) {
                deadbeef->plt_unref (plt);
                plt = get_last_active_playlist ();
            }
            else {
                set_last_active_playlist (plt);
            }
            if (plt) {
                deadbeef->plt_search_process (plt, text);
                deadbeef->plt_unref (plt);
            }
        }
    }
    else {
        ddb_playlist_t *plt_curr = deadbeef->plt_get_curr ();
        if (plt_curr) {
            set_last_active_playlist (plt_curr);
            deadbeef->plt_unref (plt_curr);
        }
        int plt_count = deadbeef->plt_get_count ();
        for (int i = 0; i < plt_count; i++) {
            ddb_playlist_t *plt = deadbeef->plt_get_for_idx (i);
            if (plt && !is_quick_search_playlist (plt)) {
                deadbeef->plt_deselect_all (plt);
                deadbeef->plt_search_process (plt, text);
            }
            deadbeef->plt_unref (plt);
        }
    }
    deadbeef->pl_unlock ();

    update_list ();
    searchentry_perform_autosearch ();
    if (config_append_search_string && config_search_in != SEARCH_INLINE) {
        append_search_string_to_plt_title (text);
    }

    if (config_autosearch && !strcmp (text, "")){
        ddb_playlist_t *plt = get_last_active_playlist ();
        if (plt) {
            deadbeef->plt_set_curr (plt);
            deadbeef->plt_unref (plt);
        }
    }

    return FALSE;
}

static void
on_searchentry_changed                 (GtkEditable     *editable,
                                        gpointer         user_data)
{
    if (config_autosearch) {
        GtkEntry *entry = GTK_ENTRY (editable);
        const gchar *text = gtk_entry_get_text (entry);
        if (resizetimer) {
            g_source_remove (resizetimer);
            resizetimer = 0;
        }
        resizetimer = g_timeout_add (100, search_process, (void *)text);
    }
}

static gboolean
on_searchentry_focus_in_event (GtkWidget *widget,
                               GdkEvent  *event,
                               gpointer   user_data)
{
    on_searchentry_changed (GTK_EDITABLE (widget), user_data);
    return FALSE;
}

static int initialized = 0;

static int
quick_search_on_action (DB_plugin_action_t *action, int ctx)
{
    if (initialized && searchentry) {
        gtk_widget_grab_focus (searchentry);
    }
    return 0;
}

static void
quick_search_set_placeholder_text ()
{
#if GTK_CHECK_VERSION(3,0,0)
    switch (config_search_in) {
        case SEARCH_INLINE:
            gtk_entry_set_placeholder_text (GTK_ENTRY (searchentry), "Search in playlist (inline)...");
            break;
        case SEARCH_PLAYLIST:
            gtk_entry_set_placeholder_text (GTK_ENTRY (searchentry), "Search in playlist...");
            break;
        case SEARCH_ALL_PLAYLISTS:
            gtk_entry_set_placeholder_text (GTK_ENTRY (searchentry), "Search in all playlists...");
            break;
        default:
            gtk_entry_set_placeholder_text (GTK_ENTRY (searchentry), "Search...");
            break;
    }
#endif
}

static void
on_search_playlist_inline_activate     (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    deadbeef->conf_set_int (CONFSTR_SEARCH_IN, SEARCH_INLINE);
    deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
    config_search_in = SEARCH_INLINE;
    quick_search_set_placeholder_text ();
}

static void
on_search_playlist_activate            (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    deadbeef->conf_set_int (CONFSTR_SEARCH_IN, SEARCH_PLAYLIST);
    deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
    config_search_in = SEARCH_PLAYLIST;
    quick_search_set_placeholder_text ();
}

static void
on_search_all_playlists_activate       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    deadbeef->conf_set_int (CONFSTR_SEARCH_IN, SEARCH_ALL_PLAYLISTS);
    deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
    config_search_in = SEARCH_ALL_PLAYLISTS;
    quick_search_set_placeholder_text ();
}

static void
on_autosearch_activate       (GtkMenuItem     *menuitem,
                                        gpointer         user_data)
{
    config_autosearch = config_autosearch ? FALSE : TRUE;
    deadbeef->conf_set_int (CONFSTR_AUTOSEARCH, config_autosearch);
    deadbeef->sendmessage (DB_EV_CONFIGCHANGED, 0, 0, 0);
}

static void
quick_search_create_popup_menu (gpointer user_data)
{
    w_quick_search_t *w = user_data;
    w->popup = gtk_menu_new ();
    gtk_widget_show (w->popup);

    GtkWidget *search_in = gtk_menu_item_new_with_mnemonic ("Search in");
    gtk_container_add (GTK_CONTAINER (w->popup), search_in);
    gtk_widget_show (search_in);

    GtkWidget *search_in_menu = gtk_menu_new ();
    gtk_menu_item_set_submenu (GTK_MENU_ITEM (search_in), search_in_menu);


    GSList *search_in_group = NULL;
    GtkWidget *search_playlist_inline = gtk_radio_menu_item_new_with_mnemonic (search_in_group, "Playlist (inline)");
    search_in_group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (search_playlist_inline));
    gtk_widget_show (search_playlist_inline);
    gtk_container_add (GTK_CONTAINER (search_in_menu), search_playlist_inline);

    g_signal_connect ((gpointer) search_playlist_inline, "activate",
            G_CALLBACK (on_search_playlist_inline_activate),
            NULL);

    GtkWidget *search_playlist = gtk_radio_menu_item_new_with_mnemonic (search_in_group, "Playlist");
    search_in_group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (search_playlist));
    gtk_widget_show (search_playlist);
    gtk_container_add (GTK_CONTAINER (search_in_menu), search_playlist);
    g_signal_connect ((gpointer) search_playlist, "activate",
            G_CALLBACK (on_search_playlist_activate),
            NULL);

    GtkWidget *search_all_playlists = gtk_radio_menu_item_new_with_mnemonic (search_in_group, "All Playlists");
    search_in_group = gtk_radio_menu_item_get_group (GTK_RADIO_MENU_ITEM (search_all_playlists));
    gtk_widget_show (search_all_playlists);
    gtk_container_add (GTK_CONTAINER (search_in_menu), search_all_playlists);
    g_signal_connect ((gpointer) search_all_playlists, "activate",
            G_CALLBACK (on_search_all_playlists_activate),
            NULL);

    GtkWidget *autosearch = gtk_check_menu_item_new_with_mnemonic ("Autosearch");
    gtk_widget_show (autosearch);
    gtk_container_add (GTK_CONTAINER (w->popup), autosearch);
    gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (autosearch), config_autosearch);
    g_signal_connect ((gpointer) autosearch, "activate",
            G_CALLBACK (on_autosearch_activate),
            NULL);

    if (config_search_in == SEARCH_INLINE) {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (search_playlist_inline), TRUE);
    }
    else if (config_search_in == SEARCH_PLAYLIST) {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (search_playlist), TRUE);
    }
    else if (config_search_in == SEARCH_ALL_PLAYLISTS) {
        gtk_check_menu_item_set_active (GTK_CHECK_MENU_ITEM (search_all_playlists), TRUE);
    }
}

static int
quick_search_message (ddb_gtkui_widget_t *widget, uint32_t id, uintptr_t ctx, uint32_t p1, uint32_t p2)
{
    w_quick_search_t *w = (w_quick_search_t *)widget;

    switch (id) {
        case DB_EV_CONFIGCHANGED:
            config_search_in = deadbeef->conf_get_int (CONFSTR_SEARCH_IN, FALSE);
            config_autosearch = deadbeef->conf_get_int (CONFSTR_AUTOSEARCH, TRUE);
            config_append_search_string = deadbeef->conf_get_int (CONFSTR_APPEND_SEARCH_STRING, FALSE);

            if (!config_append_search_string) {
                set_default_quick_search_playlist_title ();
            }
            break;
    }
    return 0;
}


#if GTK_CHECK_VERSION(3,0,0)
static DB_plugin_action_t
quick_search_action_gtk3 = {
    .title = "Quick search (GTK3)",
    .name = "quick_search_gtk3",
    .flags = DB_ACTION_COMMON,
    .callback2 = quick_search_on_action,
    .next = NULL
};
#else
static DB_plugin_action_t
quick_search_action = {
    .title = "Quick search",
    .name = "quick_search",
    .flags = DB_ACTION_COMMON,
    .callback2 = quick_search_on_action,
    .next = NULL
};
#endif

static DB_plugin_action_t *
quick_search_get_actions (DB_playItem_t *it)
{
#if GTK_CHECK_VERSION(3,0,0)
    return &quick_search_action_gtk3;
#else
    return &quick_search_action;
#endif
}

static void
quick_search_init (ddb_gtkui_widget_t *ww) {
    w_quick_search_t *w = (w_quick_search_t *)ww;

    searchentry = gtk_entry_new ();
#if GTK_CHECK_VERSION(3,0,0)
    gtk_entry_set_icon_from_icon_name (GTK_ENTRY (searchentry), GTK_ENTRY_ICON_PRIMARY, "edit-find-symbolic");
    gtk_entry_set_icon_from_icon_name (GTK_ENTRY (searchentry), GTK_ENTRY_ICON_SECONDARY, "edit-clear-symbolic");
#else
    gtk_entry_set_icon_from_icon_name (GTK_ENTRY (searchentry), GTK_ENTRY_ICON_PRIMARY, "edit-find");
    gtk_entry_set_icon_from_icon_name (GTK_ENTRY (searchentry), GTK_ENTRY_ICON_SECONDARY, "edit-clear");
#endif
    gtk_entry_set_invisible_char (GTK_ENTRY (searchentry), 8226);
    gtk_entry_set_activates_default (GTK_ENTRY (searchentry), TRUE);
    gtk_widget_show (searchentry);

    gtk_container_add (GTK_CONTAINER (w->base.widget), searchentry);
    g_signal_connect ((gpointer) searchentry, "changed",
            G_CALLBACK (on_searchentry_changed),
            NULL);
    g_signal_connect ((gpointer) searchentry, "key_press_event",
            G_CALLBACK (on_searchentry_key_press_event),
            NULL);
    g_signal_connect ((gpointer) searchentry, "focus_in_event",
            G_CALLBACK (on_searchentry_focus_in_event),
            NULL);
    g_signal_connect ((gpointer) searchentry, "icon_release",
            G_CALLBACK (on_searchentry_icon_press),
            w);

    config_search_in = deadbeef->conf_get_int (CONFSTR_SEARCH_IN, FALSE);
    config_autosearch = deadbeef->conf_get_int (CONFSTR_AUTOSEARCH, TRUE);
    config_append_search_string = deadbeef->conf_get_int (CONFSTR_APPEND_SEARCH_STRING, FALSE);
    quick_search_set_placeholder_text ();
    quick_search_create_popup_menu (w);

    initialized = 1;
}

static void
quick_search_destroy (ddb_gtkui_widget_t *w) {
    if (last_active_plt) {
        deadbeef->plt_unref (last_active_plt);
        last_active_plt = NULL;
    }
    if (resizetimer) {
        g_source_remove (resizetimer);
        resizetimer = 0;
    }
}

static ddb_gtkui_widget_t *
w_quick_search_create (void) {
    w_quick_search_t *w = malloc (sizeof (w_quick_search_t));
    memset (w, 0, sizeof (w_quick_search_t));

    w->base.widget = gtk_event_box_new ();
    w->base.destroy  = quick_search_destroy;
    w->base.init = quick_search_init;
    w->base.message = quick_search_message;
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

static const char settings_dlg[] =
    "property \"Append search string to playlist name \" checkbox " CONFSTR_APPEND_SEARCH_STRING " 0 ;\n"
;

static int
quick_search_disconnect (void)
{
    gtkui_plugin = NULL;
    return 0;
}

// define plugin interface
static DB_misc_t plugin = {
    .plugin.api_vmajor = 1,
    .plugin.api_vminor = 8,
    .plugin.version_major = 0,
    .plugin.version_minor = 1,
#if GTK_CHECK_VERSION(3,0,0)
    .plugin.id              = "quick_search-gtk3",
#else
    .plugin.id              = "quick_search",
#endif
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
    .plugin.configdialog    = settings_dlg,
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
