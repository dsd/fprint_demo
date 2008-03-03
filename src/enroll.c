/*
 * fprint_demo: Demonstration of libfprint's capabilities
 * Copyright (C) 2007-2008 Daniel Drake <dsd@gentoo.org>
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
static GtkWidget *edlg_please_wait;
static GtkWidget *edlg_progress_lbl;
static GtkWidget *edlg_instr_lbl;
static GtkWidget *edlg_progress_bar;
static GtkWidget *edlg_img_hbox;
static GtkWidget *edlg_last_image = NULL;

static struct fp_img *edlg_last_fp_img = NULL;
static struct fp_print_data *edlg_enroll_data = NULL;

static int nr_enroll_stages = 0;
static int enroll_stage = 0;
static gboolean enroll_complete = FALSE;

/* the numeric index of the finger being enrolled */
static int edlg_finger = 0;

static GtkWidget *create_enroll_dialog(void)
{
	const char *fstr = fingerstr(edlg_finger);
	gchar *fstr_lower = g_ascii_strdown(fstr, -1);
	nr_enroll_stages = fp_dev_get_nr_enroll_stages(fpdev);
	gchar *tmp;
	GtkWidget *label, *vbox;

	tmp = g_strdup_printf("Enroll %s", fstr_lower);
	edlg_dialog = gtk_dialog_new_with_buttons(tmp, GTK_WINDOW(mwin_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR,
		GTK_STOCK_OK, GTK_RESPONSE_OK,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
	g_free(tmp);

	vbox = GTK_DIALOG(edlg_dialog)->vbox;

	tmp = g_strdup_printf("In order to enroll your %s you will have to "
		"successfully scan your finger %d time%s.",
		fstr_lower, nr_enroll_stages,
		(nr_enroll_stages == 1) ? "" : "s");
	label = gtk_label_new(tmp);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), label);
	g_free(fstr_lower);
	g_free(tmp);

	edlg_progress_lbl = gtk_label_new(NULL);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), edlg_progress_lbl);
	tmp = g_strdup_printf("<b>Step 1 of %d</b>", nr_enroll_stages);
	gtk_label_set_markup(GTK_LABEL(edlg_progress_lbl), tmp);
	g_free(tmp);

	edlg_img_hbox = gtk_hbox_new(FALSE, 2);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), edlg_img_hbox);

	edlg_progress_bar = gtk_progress_bar_new();
	gtk_box_pack_start_defaults(GTK_BOX(vbox), edlg_progress_bar);
	g_object_set_data(G_OBJECT(edlg_dialog), "progressbar", edlg_progress_bar);

	edlg_instr_lbl = gtk_label_new(NULL);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), edlg_instr_lbl);
	gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Scan your finger now");

	gtk_dialog_set_response_sensitive(GTK_DIALOG(edlg_dialog),
		GTK_RESPONSE_OK, FALSE);
	return edlg_dialog;
}

/* timeout-invoked function which pulses the progress bar */
static gboolean edlg_pulse_progress(gpointer data)
{
	GtkWidget *dialog = data;
	GtkWidget *progressbar = g_object_get_data(G_OBJECT(dialog), "progressbar");
	if (!progressbar)
		return FALSE;
	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progressbar));
	return TRUE;
}

static void run_enroll_dialog(GtkWidget *dialog)
{
	guint id;
	gtk_widget_show_all(dialog);
	id = gdk_threads_add_timeout(100, edlg_pulse_progress, dialog);
	g_object_set_data(G_OBJECT(dialog), "pulsesource", GUINT_TO_POINTER(id));
}

static void stop_edlg_progress_pulse(GtkWidget *dialog)
{
	gpointer source = g_object_get_data(G_OBJECT(dialog), "pulsesource");
	if (source) {
		g_source_remove(GPOINTER_TO_UINT(source));
		g_object_set_data(G_OBJECT(dialog), "pulsesource", NULL);
	}
}

static void destroy_enroll_dialog(GtkWidget *dialog)
{
	stop_edlg_progress_pulse(dialog);
	gtk_widget_destroy(dialog);
}

static void __enroll_stopped(int result)
{
	if (edlg_please_wait) {
		gtk_widget_destroy(edlg_please_wait);
		edlg_please_wait = NULL;
	}

	if (!enroll_complete)
		return;

	stop_edlg_progress_pulse(edlg_dialog);

	if (result < 0) {
		GtkWidget *dialog =
			gtk_message_dialog_new_with_markup(GTK_WINDOW(mwin_window),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Enroll failed with error %d", result, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		destroy_enroll_dialog(edlg_dialog);
	} else if (result == FP_ENROLL_FAIL) {
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(edlg_progress_bar),
			"Enrollment failed due to bad scan data.");
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(edlg_progress_bar),
			0.0);
		gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Click Cancel to "
			"continue.");
	} else if (result == FP_ENROLL_COMPLETE) {
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(edlg_progress_bar),
			"Enrollment complete!");
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(edlg_progress_bar),
			1.0);
		gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Click OK to save "
			"and continue.");
		gtk_dialog_set_response_sensitive(GTK_DIALOG(edlg_dialog),
			GTK_RESPONSE_OK, TRUE);
		/* FIXME: grab focus on OK button? */
	} else {
		g_error("unknown enroll result %d", result);
	}
}

static void enroll_stopped(struct fp_dev *dev, void *user_data)
{
	__enroll_stopped(GPOINTER_TO_INT(user_data));
}

static void edlg_cancel_enroll(int result)
{
	int r;
	void *data = GINT_TO_POINTER(result);

	enroll_stage = -1;
	r = fp_async_enroll_stop(fpdev, enroll_stopped, data);
	if (r < 0)
		__enroll_stopped(result);
}

static void enroll_stage_cb(struct fp_dev *dev, int result,
	struct fp_print_data *print, struct fp_img *img, void *user_data)
{
	gboolean free_tmp = FALSE;
	gchar *tmp;

	if (result < 0) {
		edlg_cancel_enroll(result);
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

	if (print)
		edlg_enroll_data = print;

	switch (result) {
	case FP_ENROLL_COMPLETE:
		tmp = "<b>Enrollment completed!</b>";
		break;
	case FP_ENROLL_PASS:
		tmp = g_strdup_printf("<b>Step %d of %d</b>", ++enroll_stage,
			nr_enroll_stages);
		free_tmp = TRUE;
		break;
	case FP_ENROLL_FAIL:
		tmp = "<b>Enrollment failed!</b>";
		break;
	case FP_ENROLL_RETRY:
		tmp = "<b>Bad scan. Please try again.</b>";
		break;
	case FP_ENROLL_RETRY_TOO_SHORT:
		tmp = "<b>Bad scan: swipe was too short. Please try again.</b>";
		break;
	case FP_ENROLL_RETRY_CENTER_FINGER:
		tmp = "<b>Bad scan: finger was not centered on scanner. Please "
			"try again.</b>";
		break;
	case FP_ENROLL_RETRY_REMOVE_FINGER:
		tmp = "<b>Bad scan: please remove finger before retrying.</b>";
		break;
	default:
		tmp = "Unknown state!";
	}

	gtk_label_set_markup(GTK_LABEL(edlg_progress_lbl), tmp);
	if (free_tmp)
		g_free(tmp);

	if (result == FP_ENROLL_COMPLETE || result == FP_ENROLL_FAIL) {
		enroll_complete = TRUE;
		edlg_cancel_enroll(result);
	}

	/* FIXME show binarized images? */
}

/* called when enrollment dialog is closed. determine if it was cancelled
 * or if there is a print to be saved. */
static void enroll_response(GtkWidget *widget, gint arg, gpointer data)
{
	int r;
	destroy_enroll_dialog(edlg_dialog);

	if (arg == GTK_RESPONSE_CANCEL) {
		if (!enroll_complete)
			edlg_cancel_enroll(0);
		return;
	}

	g_assert(edlg_enroll_data);
	r = fp_print_data_save(edlg_enroll_data, edlg_finger);
	fp_print_data_free(edlg_enroll_data);
	if (r < 0) {
		GtkWidget *dialog =
			gtk_message_dialog_new_with_markup(GTK_WINDOW(mwin_window),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Could not save enroll data, error %d", r, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}

	mwin_refresh_prints();
	return;
}

/* open enrollment dialog and start enrollment */
static void ewin_cb_enroll_clicked(GtkWidget *widget, gpointer data)
{
	int r;
	GtkWidget *dialog;

	edlg_finger = GPOINTER_TO_INT(data);
	edlg_enroll_data = NULL;
	enroll_stage = 1;
	enroll_complete = FALSE;

	dialog = create_enroll_dialog();
	r = fp_async_enroll_start(fpdev, enroll_stage_cb, dialog);
	if (r < 0) {
		destroy_enroll_dialog(dialog);
		dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(mwin_window),
			GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
			GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
			"Failed to start enrollment, error %d", r, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}
	g_signal_connect(dialog, "response", G_CALLBACK(enroll_response), NULL);
	run_enroll_dialog(dialog);
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
	while ((print = fp_dscv_prints[i++])) {
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

	while ((print = fp_dscv_prints[i++])) {
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

