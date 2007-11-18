/*
 * fprint_demo: Demonstration of libfprint's capabilities
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <gtk/gtk.h>
#include <libfprint/fprint.h>

#include "fprint_demo.h"

static GtkWidget *ewin_enroll_btn[RIGHT_LITTLE+1];
static GtkWidget *ewin_status_lbl[RIGHT_LITTLE+1];

static struct fp_dscv_print **discovered_prints = NULL;

static void ewin_clear(void)
{
	int i;
	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		gtk_widget_set_sensitive(ewin_enroll_btn[i], FALSE);
		gtk_label_set_text(GTK_LABEL(ewin_status_lbl[i]), "Not enrolled");
	}
}

static void ewin_activate_dev(void)
{
	struct fp_dscv_print *print;
	int i = 0;

	g_assert(fpdev);
	g_assert(fp_dscv_prints);

	while (print = fp_dscv_prints[i++]) {
		int fnum;
		if (!fp_dev_supports_dscv_print(fpdev, print))
			continue;

		fnum = fp_dscv_print_get_finger(print);
		gtk_label_set_text(GTK_LABEL(ewin_status_lbl[fnum]), "Enrolled");
	}

	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++)
		gtk_widget_set_sensitive(ewin_enroll_btn[i], TRUE);
}

static GtkWidget *ewin_create(void)
{
	GtkWidget *vbox;
	GtkWidget *table;
	int i;

	vbox = gtk_vbox_new(FALSE, 0);
	table = gtk_table_new(10, 4, FALSE);
	gtk_table_set_row_spacings(GTK_TABLE(table), 5);

	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		GtkWidget *button, *label;
		gchar *tmp;

		label = gtk_label_new(NULL);
		tmp = g_strdup_printf("<b>%s</b>", fingerstr(i));
		gtk_label_set_markup(GTK_LABEL(label), tmp);
		g_free(tmp);
		gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, i, i + 1);

		ewin_status_lbl[i] = gtk_label_new(NULL);
		gtk_table_attach_defaults(GTK_TABLE(table), ewin_status_lbl[i],
			1, 2, i, i + 1);

		button = gtk_button_new_with_label("Enroll");
		gtk_table_attach_defaults(GTK_TABLE(table), button, 2, 3, i, i + 1);
		ewin_enroll_btn[i] = button;

		button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
		gtk_widget_set_sensitive(button, FALSE);
		gtk_table_attach_defaults(GTK_TABLE(table), button, 3, 4, i, i + 1);
	}

	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);
	return vbox;
}

struct fpd_tab enroll_tab = {
	.name = "Enroll",
	.create = ewin_create,
	.clear = ewin_clear,
	.activate_dev = ewin_activate_dev,
};

