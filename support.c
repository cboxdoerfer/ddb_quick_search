#include <gtk/gtk.h>

#include "support.h"

#if !GTK_CHECK_VERSION(2,24,0)
#define GTK_COMBO_BOX_TEXT GTK_COMBO_BOX
GtkWidget *
gtk_combo_box_text_new () {
    return gtk_combo_box_new_text ();
}

GtkWidget *
gtk_combo_box_text_new_with_entry   (void) {
    return gtk_combo_box_entry_new ();
}

void
gtk_combo_box_text_append_text (GtkComboBoxText *combo_box, const gchar *text) {
    gtk_combo_box_append_text (combo_box, text);
}

void
gtk_combo_box_text_insert_text (GtkComboBoxText *combo_box, gint position, const gchar *text) {
    gtk_combo_box_insert_text (combo_box, position, text);
}

void
gtk_combo_box_text_prepend_text (GtkComboBoxText *combo_box, const gchar *text) {
    gtk_combo_box_prepend_text (combo_box, text);
}
gchar *
gtk_combo_box_text_get_active_text  (GtkComboBoxText *combo_box) {
    return gtk_combo_box_get_active_text (combo_box);
}

#endif
