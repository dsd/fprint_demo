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
static GtkWidget *ewin_delete_btn[RIGHT_LITTLE+1];
static GtkWidget *ewin_status_lbl[RIGHT_LITTLE+1];

static GtkWidget *edlg_dialog;
static GtkWidget *edlg_progress_lbl;
static GtkWidget *edlg_instr_lbl;
static GtkWidget *edlg_img_hbox;
static GtkWidget *edlg_last_image = NULL;

static struct fp_img *edlg_last_fp_img = NULL;
static struct fp_print_data *edlg_enroll_data = NULL;

static int nr_enroll_stages = 0;
static int enroll_stage = 0;

static gboolean scan_preview_complete(gpointer data);

static void edlg_run_enroll_stage()
{
	int r;
	struct fp_img *img = NULL;
	gchar *tmp;
	int passed = 0;

	tmp = g_strdup_printf("<b>Step %d of %d</b>", enroll_stage,
		nr_enroll_stages);
	gtk_label_set_markup(GTK_LABEL(edlg_progress_lbl), tmp);
	g_free(tmp);

	gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Scan your finger now");
	while (gtk_events_pending())
		gtk_main_iteration();

	r = fp_enroll_finger_img(fpdev, &edlg_enroll_data, &img);
	if (r < 0) {
		GtkWidget *dialog =
			gtk_message_dialog_new_with_markup(GTK_WINDOW(edlg_dialog),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Enroll failed with error %d", r, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		gtk_widget_hide(edlg_dialog);
		return;
	}

	if (img) {
		GdkPixbuf *pixbuf = img_to_pixbuf(img);
		GtkWidget *image = gtk_image_new_from_pixbuf(pixbuf);
		gtk_box_pack_start_defaults(GTK_BOX(edlg_img_hbox), image);
		gtk_widget_show(image);
		edlg_last_fp_img = img;
		edlg_last_image = image;
		g_object_unref(G_OBJECT(pixbuf));
	} else {
		edlg_last_fp_img = NULL;
		edlg_last_image = NULL;
	}

	switch (r) {
	case FP_ENROLL_COMPLETE:
	case FP_ENROLL_PASS:
		tmp = "<b>Scan successful!</b>";
		passed = 1;
		break;
	case FP_ENROLL_FAIL:
		tmp = "<b>Scan successful!</b>";
		passed = 2;
		break;
	case FP_ENROLL_RETRY:
		tmp = "<b>Bad scan.</b>";
		break;
	case FP_ENROLL_RETRY_TOO_SHORT:
		tmp = "<b>Bad scan: swipe was too short.</b>";
		break;
	case FP_ENROLL_RETRY_CENTER_FINGER:
		tmp = "<b>Bad scan: finger was not centered on scanner.</b>";
		break;
	case FP_ENROLL_RETRY_REMOVE_FINGER:
		tmp = "<b>Bad scan: please remove finger before retrying.</b>";
		break;
	}

	gtk_label_set_markup(GTK_LABEL(edlg_progress_lbl), tmp);
	gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Please wait...");
	g_timeout_add(1500, scan_preview_complete, GINT_TO_POINTER(passed));
}

static gboolean scan_preview_complete(gpointer data)
{
	int passed = GPOINTER_TO_INT(data);

	if (edlg_last_fp_img) {
		if (passed) {
			struct fp_img *img_bin = fp_img_binarize(edlg_last_fp_img);
			GdkPixbuf *pixbuf = img_to_pixbuf(img_bin);
			gtk_image_set_from_pixbuf(GTK_IMAGE(edlg_last_image), pixbuf);
			g_object_unref(G_OBJECT(pixbuf));
			fp_img_free(img_bin);
		} else {
			gtk_container_remove(GTK_CONTAINER(edlg_img_hbox), edlg_last_image);
			edlg_last_image = NULL;
		}
		fp_img_free(edlg_last_fp_img);
		edlg_last_fp_img = NULL;
	}

	if (edlg_enroll_data) {
		gtk_label_set_markup(GTK_LABEL(edlg_progress_lbl),
			"<b>Enrollment complete!</b>");
		gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Click OK to save "
			"and continue.");
		gtk_dialog_set_response_sensitive(GTK_DIALOG(edlg_dialog),
			GTK_RESPONSE_ACCEPT, TRUE);
		gtk_dialog_set_response_sensitive(GTK_DIALOG(edlg_dialog),
			GTK_RESPONSE_REJECT, TRUE);
	} else if (passed == 2) {
		gtk_label_set_markup(GTK_LABEL(edlg_progress_lbl),
			"<b>Enrollment failed due to bad scan data.</b>");
		gtk_label_set_text(GTK_LABEL(edlg_instr_lbl),
			"Click cancel to continue");
		gtk_dialog_set_response_sensitive(GTK_DIALOG(edlg_dialog),
			GTK_RESPONSE_REJECT, TRUE);
	} else {
		if (passed)
			enroll_stage++;
		edlg_run_enroll_stage();
	}

	return FALSE;
}

static GtkWidget *edlg_show(GtkWidget *widget, gpointer data)
{
	edlg_enroll_data = NULL;
	enroll_stage = 1;
	edlg_run_enroll_stage();
}

static GtkWidget *edlg_create(int fnum)
{
	const char *fstr = fingerstr(fnum);
	gchar *fstr_lower = g_ascii_strdown(fstr, -1);
	nr_enroll_stages = fp_dev_get_nr_enroll_stages(fpdev);
	gchar *tmp;
	GtkWidget *label, *vbox;

	tmp = g_strdup_printf("Enroll %s", fstr_lower);

	edlg_dialog = gtk_dialog_new_with_buttons(tmp, GTK_WINDOW(mwin_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
		GTK_STOCK_CANCEL, GTK_RESPONSE_REJECT,
		GTK_STOCK_OK, GTK_RESPONSE_ACCEPT,
		NULL);
	g_free(tmp);
	g_signal_connect(G_OBJECT(edlg_dialog), "show", G_CALLBACK(edlg_show),
		NULL);
	vbox = GTK_DIALOG(edlg_dialog)->vbox;

	gtk_dialog_set_response_sensitive(GTK_DIALOG(edlg_dialog),
		GTK_RESPONSE_ACCEPT, FALSE);
	gtk_dialog_set_response_sensitive(GTK_DIALOG(edlg_dialog),
		GTK_RESPONSE_REJECT, FALSE);

	tmp = g_strdup_printf("In order to enroll your %s you will have to "
		"successfully scan your finger %d time%s.",
		fstr_lower, nr_enroll_stages,
		(nr_enroll_stages == 1) ? "" : "s");
	label = gtk_label_new(tmp);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), label);

	edlg_progress_lbl = gtk_label_new(NULL);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), edlg_progress_lbl);

	edlg_instr_lbl = gtk_label_new(NULL);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), edlg_instr_lbl);

	edlg_img_hbox = gtk_hbox_new(FALSE, 2);
	gtk_box_pack_end_defaults(GTK_BOX(vbox), edlg_img_hbox);

	g_free(fstr_lower);
	gtk_widget_show_all(vbox);
	return edlg_dialog;
}

static void ewin_cb_delete_clicked(GtkWidget *widget, gpointer data)
{
	int finger = GPOINTER_TO_INT(data);
	int r;

	r = fp_print_data_delete(fpdev, finger);
	if (r < 0) {
		GtkWidget *dialog =
			gtk_message_dialog_new_with_markup(GTK_WINDOW(mwin_window),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Could not delete enroll data, error %d", r, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
	mwin_refresh_prints();
}

static void ewin_cb_enroll_clicked(GtkWidget *widget, gpointer data)
{
	int finger = GPOINTER_TO_INT(data);
	GtkWidget *dialog;
	int r;

	dialog = edlg_create(finger);
	r = gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

	if (r != GTK_RESPONSE_ACCEPT)	
		return;

	g_assert(edlg_enroll_data);
	r = fp_print_data_save(edlg_enroll_data, finger);
	fp_print_data_free(edlg_enroll_data);
	if (r < 0) {
		GtkWidget *dialog =
			gtk_message_dialog_new_with_markup(GTK_WINDOW(edlg_dialog),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Could not save enroll data, error %d", r, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}

	mwin_refresh_prints();
}

static void ewin_clear(void)
{
	int i;
	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		gtk_widget_set_sensitive(ewin_enroll_btn[i], FALSE);
		gtk_widget_set_sensitive(ewin_delete_btn[i], FALSE);
		gtk_label_set_text(GTK_LABEL(ewin_status_lbl[i]), "Not enrolled");
	}
}

static void ewin_refresh(void)
{
	struct fp_dscv_print *print;
	int i;

	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		gtk_label_set_text(GTK_LABEL(ewin_status_lbl[i]), "Not enrolled");
		gtk_widget_set_sensitive(ewin_delete_btn[i], FALSE);
	}

	i = 0;
	while (print = fp_dscv_prints[i++]) {
		int fnum;
		if (!fp_dev_supports_dscv_print(fpdev, print))
			continue;

		fnum = fp_dscv_print_get_finger(print);
		gtk_label_set_text(GTK_LABEL(ewin_status_lbl[fnum]), "Enrolled");
		gtk_widget_set_sensitive(ewin_delete_btn[fnum], TRUE);
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
		gtk_widget_set_sensitive(ewin_delete_btn[fnum], TRUE);
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
		gtk_table_attach_defaults(GTK_TABLE(table), label, 0, 1, i - 1, i);

		ewin_status_lbl[i] = gtk_label_new(NULL);
		gtk_table_attach_defaults(GTK_TABLE(table), ewin_status_lbl[i],
			1, 2, i - 1, i);

		button = gtk_button_new_with_label("Enroll");
		gtk_table_attach_defaults(GTK_TABLE(table), button, 2, 3, i - 1, i);
		g_signal_connect(G_OBJECT(button), "clicked",
			G_CALLBACK(ewin_cb_enroll_clicked), GINT_TO_POINTER(i));
		ewin_enroll_btn[i] = button;

		button = gtk_button_new_from_stock(GTK_STOCK_DELETE);
		g_signal_connect(G_OBJECT(button), "clicked",
			G_CALLBACK(ewin_cb_delete_clicked), GINT_TO_POINTER(i));
		gtk_table_attach_defaults(GTK_TABLE(table), button, 3, 4, i - 1, i);
		ewin_delete_btn[i] = button;
	}

	gtk_box_pack_start(GTK_BOX(vbox), table, FALSE, FALSE, 0);
	return vbox;
}

struct fpd_tab enroll_tab = {
	.name = "Enroll",
	.create = ewin_create,
	.clear = ewin_clear,
	.activate_dev = ewin_activate_dev,
	.refresh = ewin_refresh,
};

