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

static GtkWidget *edlg_window;
static GtkWidget *edlg_please_wait;
static GtkWidget *edlg_button_ok;
static GtkWidget *edlg_progress_lbl;
static GtkWidget *edlg_instr_lbl;
static GtkWidget *edlg_progress_bar;
static GtkWidget *edlg_img_hbox;
static GtkWidget *edlg_last_image = NULL;

/* the GSource ID for the timeout that pulses the progress bar */
static guint edlg_pulse_timeout = 0;

static struct fp_img *edlg_last_fp_img = NULL;
static struct fp_print_data *edlg_enroll_data = NULL;

static int nr_enroll_stages = 0;
static int enroll_stage = 0;

/* the numeric index of the finger being enrolled */
static int edlg_finger = 0;

/* status code for enroll dialog termination.
 * -1 means enroll in progress
 *  0 means successful enroll
 *  positive means error/cancellation
 */
static int edlg_result = -1;

static void __enroll_stopped(int result)
{
	if (edlg_please_wait) {
		gtk_widget_destroy(edlg_please_wait);
		edlg_please_wait = NULL;
	}

	if (result < 0) {
		GtkWidget *dialog =
			gtk_message_dialog_new_with_markup(GTK_WINDOW(edlg_window),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Enroll failed with error %d", result, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		edlg_result = 1;
		gtk_widget_destroy(edlg_window);
	} else if (result == FP_ENROLL_FAIL) {
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(edlg_progress_bar),
			"Enrollment failed due to bad scan data.");
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(edlg_progress_bar),
			0.0);
		gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Click Cancel to "
			"continue.");
		edlg_result = 1;
	} else if (result == FP_ENROLL_COMPLETE) {
		gtk_progress_bar_set_text(GTK_PROGRESS_BAR(edlg_progress_bar),
			"Enrollment complete!");
		gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(edlg_progress_bar),
			1.0);
		gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Click OK to save "
			"and continue.");
		edlg_result = 0;
		gtk_widget_set_sensitive(edlg_button_ok, TRUE);
		gtk_widget_grab_focus(edlg_button_ok);
	} else {
		g_error("unknown enroll result %d", result);
	}
}

/* libfprint callback when enroll stop operation has completed */
static void enroll_stopped(struct fp_dev *dev, void *user_data)
{
	__enroll_stopped(GPOINTER_TO_INT(user_data));
}

static void cancel_progress_pulse(void)
{
	g_source_remove(edlg_pulse_timeout);
	edlg_pulse_timeout = 0;
}

static void enroll_stage_cb(struct fp_dev *dev, int result,
	struct fp_print_data *print, struct fp_img *img, void *user_data)
{
	gboolean free_tmp = FALSE;
	gchar *tmp;
	int r;

	if (result < 0) {
		cancel_progress_pulse();
		edlg_please_wait = run_please_wait_dialog("Ending enrollment...");
		r = fp_async_enroll_stop(dev, enroll_stopped, GINT_TO_POINTER(result));
		if (r < 0)
			__enroll_stopped(result);
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
		cancel_progress_pulse();
		edlg_please_wait = run_please_wait_dialog("Ending enrollment...");
		r = fp_async_enroll_stop(dev, enroll_stopped, GINT_TO_POINTER(result));
		if (r < 0)
			__enroll_stopped(result);
	}

	/* FIXME show binarized images? */
}

/* called when user clicks OK in enroll dialog */
static void edlg_ok_clicked(GtkWidget *widget, gpointer data)
{
	gtk_widget_destroy(edlg_window);
}

static void enroll_cancelled(struct fp_dev *dev, void *user_data)
{
	if (edlg_please_wait) {
		gtk_widget_destroy(edlg_please_wait);
		edlg_please_wait = NULL;
	}
}

/* called when user clicks cancel in enroll dialog */
static void edlg_cancel_clicked(GtkWidget *widget, gpointer data)
{
	int r;

	if (edlg_result == -1) {
		/* enrollment is running */
		cancel_progress_pulse();
		edlg_please_wait = run_please_wait_dialog("Ending enrollment...");
		r = fp_async_enroll_stop(fpdev, enroll_cancelled, NULL);
		if (r < 0)
			enroll_cancelled(fpdev, NULL);
	}
	edlg_result = 1;
	gtk_widget_destroy(edlg_window);
}

/* called when enrollment dialog is closed. determine if it was cancelled
 * or if there is a print to be saved. */
static gboolean edlg_destroyed(GtkWidget *widget, GdkEvent *event,
	gpointer data)
{
	int r;

	if (edlg_result > 0)
		return FALSE;

	g_assert(edlg_enroll_data);
	r = fp_print_data_save(edlg_enroll_data, edlg_finger);
	fp_print_data_free(edlg_enroll_data);
	if (r < 0) {
		GtkWidget *dialog =
			gtk_message_dialog_new_with_markup(GTK_WINDOW(edlg_window),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Could not save enroll data, error %d", r, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
	}

	mwin_refresh_prints();
	return FALSE;
}

static void edlg_create(void)
{
	const char *fstr = fingerstr(edlg_finger);
	gchar *fstr_lower = g_ascii_strdown(fstr, -1);
	nr_enroll_stages = fp_dev_get_nr_enroll_stages(fpdev);
	gchar *tmp;
	GtkWidget *label, *vbox;
	GtkWidget *button_cancel;
	GtkWidget *buttonbox;

	edlg_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_modal(GTK_WINDOW(edlg_window), TRUE);
	gtk_window_set_transient_for(GTK_WINDOW(edlg_window),
		GTK_WINDOW(mwin_window));
	gtk_window_set_resizable(GTK_WINDOW(edlg_window), FALSE);
	g_signal_connect(edlg_window, "destroy", G_CALLBACK(edlg_destroyed),
		NULL);
	tmp = g_strdup_printf("Enroll %s", fstr_lower);
	gtk_window_set_title(GTK_WINDOW(edlg_window), tmp);
	g_free(tmp);

	vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(edlg_window), vbox);

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

	edlg_instr_lbl = gtk_label_new(NULL);
	gtk_box_pack_start_defaults(GTK_BOX(vbox), edlg_instr_lbl);
	gtk_label_set_text(GTK_LABEL(edlg_instr_lbl), "Scan your finger now");

	buttonbox = gtk_hbutton_box_new();
	gtk_button_box_set_layout(GTK_BUTTON_BOX(buttonbox), GTK_BUTTONBOX_END);  
	gtk_box_pack_end(GTK_BOX(vbox), buttonbox, FALSE, TRUE, 0);

	button_cancel = gtk_button_new_from_stock(GTK_STOCK_CANCEL);
	gtk_box_pack_end(GTK_BOX(buttonbox), button_cancel, FALSE, TRUE, 0);
	g_signal_connect(G_OBJECT(button_cancel), "clicked",
		G_CALLBACK(edlg_cancel_clicked), NULL);

	edlg_button_ok = gtk_button_new_from_stock(GTK_STOCK_OK);
	gtk_box_pack_end(GTK_BOX(buttonbox), edlg_button_ok, FALSE, TRUE, 0);
	gtk_widget_set_sensitive(edlg_button_ok, FALSE);
	g_signal_connect(G_OBJECT(edlg_button_ok), "clicked",
		G_CALLBACK(edlg_ok_clicked), NULL);

	gtk_widget_show_all(edlg_window);
}

/* timeout-invoked function which pulses the progress bar */
static gboolean edlg_pulse_progress_bar(gpointer data)
{
	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(edlg_progress_bar));
	return TRUE;
}

/* open enrollment dialog and start enrollment */
static void ewin_cb_enroll_clicked(GtkWidget *widget, gpointer data)
{
	int r;

	edlg_finger = GPOINTER_TO_INT(data);
	edlg_result = -1;
	edlg_enroll_data = NULL;
	enroll_stage = 1;
	edlg_create();

	r = fp_async_enroll_start(fpdev, enroll_stage_cb, NULL);
	if (r < 0) {
		GtkWidget *dialog =
			gtk_message_dialog_new_with_markup(GTK_WINDOW(edlg_window),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Failed to start enrollment, error %d", r, NULL);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		gtk_widget_destroy(edlg_window);
	}

	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(edlg_progress_bar));
	edlg_pulse_timeout = gdk_threads_add_timeout(100, edlg_pulse_progress_bar,
		NULL);
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

