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
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libfprint/fprint.h>

#include "fprint_demo.h"

static GtkWidget *iwin_verify_img;
static GtkWidget *iwin_ify_status;
static GtkWidget *iwin_ify_button;
static GtkWidget *iwin_non_img_label;

static GtkWidget *iwin_fing_checkbox[RIGHT_LITTLE + 1];

static struct fp_img *img_normal = NULL;

static void iwin_ify_status_not_capable()
{
	gtk_label_set_markup(GTK_LABEL(iwin_ify_status),
		"<b>Status:</b> Device does not support identification.");
	gtk_widget_set_sensitive(iwin_ify_button, FALSE);
}

static void iwin_clear(void)
{
	int i;

	fp_img_free(img_normal);
	img_normal = NULL;

	gtk_image_clear(GTK_IMAGE(iwin_verify_img));

	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		gtk_widget_set_sensitive(iwin_fing_checkbox[i], FALSE);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(iwin_fing_checkbox[i]),
			FALSE);
	}

	gtk_label_set_text(GTK_LABEL(iwin_ify_status), NULL);
	gtk_widget_set_sensitive(iwin_ify_button, FALSE);
}

static void iwin_refresh(void)
{
	struct fp_dscv_print *print;
	int i;

	/* mark all fingers insensitive */
	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++)
		gtk_widget_set_sensitive(iwin_fing_checkbox[i], FALSE);

	/* resensitize detected fingers */
	i = 0;
	while (print = fp_dscv_prints[i++]) {
		GtkTreeIter iter;
		int fnum;

		if (!fp_dev_supports_dscv_print(fpdev, print))
			continue;

		fnum = fp_dscv_print_get_finger(print);
		gtk_widget_set_sensitive(iwin_fing_checkbox[fnum], TRUE);
	}

	/* untick any fingers that are not sensitive */
	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		if (!GTK_WIDGET_SENSITIVE(iwin_fing_checkbox[i]))
			gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(iwin_fing_checkbox[i]), FALSE);
	}
}

static void iwin_activate_dev(void)
{
	struct fp_dscv_print *print;
	int i = 0;
	g_assert(fpdev);
	g_assert(fp_dscv_prints);

	if (!fp_dev_supports_identification(fpdev)) {
		iwin_ify_status_not_capable();
		return;
	}

	while (print = fp_dscv_prints[i++]) {
		int fnum;

		if (!fp_dev_supports_dscv_print(fpdev, print))
			continue;

		fnum = fp_dscv_print_get_finger(print);
		gtk_widget_set_sensitive(iwin_fing_checkbox[fnum], TRUE);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(
			iwin_fing_checkbox[fnum]), TRUE);
	}

	if (fp_dev_supports_imaging(fpdev)) {
		int width = fp_dev_get_img_width(fpdev);
		int height = fp_dev_get_img_height(fpdev);
		gtk_widget_set_size_request(iwin_verify_img,
			(width == 0) ? 192 : width,
			(height == 0) ? 192 : height);
		gtk_widget_hide(iwin_non_img_label);
		gtk_widget_show(iwin_verify_img);
	} else {
		gtk_widget_show(iwin_non_img_label);
		gtk_widget_hide(iwin_verify_img);
	}
}

static void iwin_ify_status_print_loaded(int status)
{
	if (status == 0) {
		gtk_label_set_markup(GTK_LABEL(iwin_ify_status),
			"<b>Status:</b> Ready for verify scan.");
		gtk_widget_set_sensitive(iwin_ify_button, TRUE);
	} else {
		gchar *msg = g_strdup_printf("<b>Status:</b> Error %d, print corrupt?",
			status);
		gtk_label_set_markup(GTK_LABEL(iwin_ify_status), msg);
		gtk_widget_set_sensitive(iwin_ify_button, FALSE);
		g_free(msg);
	}
}

static GtkWidget *scan_finger_dialog_new(const char *msg)
{
	GtkWidget *dialog, *label;

	dialog = gtk_dialog_new_with_buttons(NULL, GTK_WINDOW(mwin_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR, NULL);
	gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);
	if (msg) {
		label = gtk_label_new(msg);
		gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label,
			FALSE, FALSE, 0);
	}
	label = gtk_label_new("Scan your finger now");
	gtk_box_pack_start(GTK_BOX(GTK_DIALOG(dialog)->vbox), label, FALSE, FALSE,
		0);
	return dialog;
}

static void iwin_ify_result_other(int code)
{
	const char *msgs[] = {
		[FP_VERIFY_NO_MATCH] = "Could not identify finger.",
		[FP_VERIFY_MATCH] = "Finger matches!",
		[FP_VERIFY_RETRY] = "Bad scan.",
		[FP_VERIFY_RETRY_TOO_SHORT] = "Swipe was too short.",
		[FP_VERIFY_RETRY_CENTER_FINGER] = "Finger was not centered on sensor.",
		[FP_VERIFY_RETRY_REMOVE_FINGER] = "Bad scan, remove finger.",
	};
	gchar *msg;

	if (code < 0)
		msg = g_strdup_printf("<b>Status:</b> Scan failed, error %d", code);
	else
		msg = g_strdup_printf("<b>Status:</b> %s", msgs[code]);

	gtk_label_set_markup(GTK_LABEL(iwin_ify_status), msg);
	g_free(msg);
}

static void iwin_ify_result_match(int fnum)
{
	gchar *tmp = g_ascii_strdown(fingerstr(fnum), -1);
	gchar *msg = g_strdup_printf("<b>Status:</b> Matched %s", tmp);
	g_free(tmp);
	gtk_label_set_markup(GTK_LABEL(iwin_ify_status), msg);
	g_free(msg);
}

static void iwin_img_draw()
{
	unsigned char *rgbdata;
	GdkPixbuf *pixbuf;
	int width;
	int height;

	if (!img_normal)
		return;

	rgbdata = img_to_rgbdata(img_normal);

	width = fp_img_get_width(img_normal);
	height = fp_img_get_height(img_normal);
	gtk_widget_set_size_request(iwin_verify_img, width, height);

	pixbuf = gdk_pixbuf_new_from_data(rgbdata, GDK_COLORSPACE_RGB,
			FALSE, 8, width, height, width * 3, pixbuf_destroy, NULL);
	gtk_image_set_from_pixbuf(GTK_IMAGE(iwin_verify_img), pixbuf);
	g_object_unref(pixbuf);
}

static void iwin_cb_fing_checkbox_toggled(GtkToggleButton *button,
	gpointer data)
{
	int i;
	if (gtk_toggle_button_get_active(button)) {
		gtk_widget_set_sensitive(iwin_ify_button, TRUE);
		return;
	}

	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		if (gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(iwin_fing_checkbox[i]))) {
			gtk_widget_set_sensitive(iwin_ify_button, TRUE);
			return;
		}
	}

	gtk_widget_set_sensitive(iwin_ify_button, FALSE);
}

static struct fp_dscv_print *dscv_print_for_finger(int fnum)
{
	int i = 0;
	struct fp_dscv_print *dprint;

	while (dprint = fp_dscv_prints[i++]) {
		if (!fp_dev_supports_dscv_print(fpdev, dprint))
			continue;
		if (fp_dscv_print_get_finger(dprint) == fnum)
			return dprint;
	}
	return NULL;
}

static void iwin_cb_identify(GtkWidget *widget, gpointer user_data)
{
	struct fp_print_data *print;
	struct fp_img *img = NULL;
	GtkWidget *dialog;
	int i;
	int r;
	size_t offset = 0;
	int selected_fingers = 0;
	struct fp_print_data **gallery;
	int *fingnum;

	/* count number of selected fingers */
	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++)
		if (gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(iwin_fing_checkbox[i])))
			selected_fingers++;
	g_assert(selected_fingers);

	/* allocate list for print gallery */
	gallery = g_malloc0(sizeof(*gallery) * (selected_fingers + 1));
	gallery[selected_fingers] = NULL; /* NULL-terminate */
	fingnum = g_malloc(sizeof(*fingnum) * selected_fingers);

	/* populate print gallery from selected fingers */
	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		struct fp_dscv_print *dprint;

		if (!gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(iwin_fing_checkbox[i])))
			continue;
	
		dprint = dscv_print_for_finger(i);
		g_assert(dprint);

		r = fp_print_data_from_dscv_print(dprint, &print);
		if (r < 0) {
			dprint = NULL;
			goto err;
		}

		gallery[offset] = print;
		fingnum[offset] = fp_dscv_print_get_finger(dprint);
		offset++;
	}

	/* do identification */

	dialog = scan_finger_dialog_new(NULL);
	gtk_widget_show_all(dialog);
	while (gtk_events_pending())
		gtk_main_iteration();

	r = fp_identify_finger_img(fpdev, gallery, &offset, &img);
	gtk_widget_hide(dialog);
	gtk_widget_destroy(dialog);

	if (r == FP_VERIFY_MATCH)
		iwin_ify_result_match(fingnum[offset]);
	else
		iwin_ify_result_other(r);

	fp_img_free(img_normal);
	img_normal = NULL;

	if (img) {
		img_normal = img;
		iwin_img_draw();
	}

	goto cleanup;
err:
	dialog = gtk_message_dialog_new_with_markup(GTK_WINDOW(mwin_window),
				GTK_DIALOG_DESTROY_WITH_PARENT | GTK_DIALOG_MODAL,
				GTK_MESSAGE_ERROR, GTK_BUTTONS_OK,
				"Could not load print for %s, error %d",
				fingerstr(i), r, NULL);
	gtk_dialog_run(GTK_DIALOG(dialog));
	gtk_widget_destroy(dialog);

cleanup:
	i = 0;
	while (print = gallery[i++])
		fp_print_data_free(print);
	g_free(gallery);
	g_free(fingnum);

}

static GtkWidget *iwin_create(void)
{
	GtkCellRenderer *renderer;
	GtkWidget *main_vbox, *dev_vbox, *lower_hbox, *ui_vbox;
	GtkWidget *button, *label, *vfy_vbox, *ify_frame, *scan_frame, *img_vbox;
	GtkWidget *iwin_ctrl_vbox;
	GtkWidget *notebook, *iwin_main_hbox;
	int i;

	iwin_main_hbox = gtk_hbox_new(FALSE, 1);

	/* Image frame */
	scan_frame = gtk_frame_new("Scanned Image");
	gtk_box_pack_start(GTK_BOX(iwin_main_hbox), scan_frame, TRUE, TRUE, 0);

	/* Image vbox */
	img_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(scan_frame), img_vbox);

	/* Image */
	iwin_verify_img = gtk_image_new();
	gtk_box_pack_start(GTK_BOX(img_vbox), iwin_verify_img, TRUE, FALSE, 0);

	/* Non-imaging device */
	iwin_non_img_label = gtk_label_new("This device does not have imaging "
		"capabilities, no images will be displayed.");
	gtk_box_pack_start_defaults(GTK_BOX(img_vbox), iwin_non_img_label);

	/* vbox for verification status and image control frames */
	ui_vbox = gtk_vbox_new(FALSE, 1);
	gtk_box_pack_end(GTK_BOX(iwin_main_hbox), ui_vbox, FALSE, FALSE, 0);

	/* Identification status */
	ify_frame = gtk_frame_new("Identification");
	gtk_box_pack_start_defaults(GTK_BOX(ui_vbox), ify_frame);

	vfy_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(ify_frame), vfy_vbox);

	/* Discovered prints list */
	label = gtk_label_new("Select fingers to identify against:");
	gtk_box_pack_start(GTK_BOX(vfy_vbox), label, FALSE, FALSE, 0);

	for (i = LEFT_THUMB; i <= RIGHT_LITTLE; i++) {
		GtkWidget *checkbox = gtk_check_button_new_with_label(fingerstr(i));
		gtk_box_pack_start(GTK_BOX(vfy_vbox), checkbox, FALSE, FALSE, 0);
		g_signal_connect(GTK_OBJECT(checkbox), "toggled",
			G_CALLBACK(iwin_cb_fing_checkbox_toggled), NULL);
		iwin_fing_checkbox[i] = checkbox;
	}

	/* Identify button */
	iwin_ify_button = gtk_button_new_with_label("Identify");
	g_signal_connect(G_OBJECT(iwin_ify_button), "clicked",
		G_CALLBACK(iwin_cb_identify), NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), iwin_ify_button, FALSE, FALSE, 0);

	/* Identify status */
	iwin_ify_status = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), iwin_ify_status, FALSE, FALSE, 0);

	return iwin_main_hbox;
}

struct fpd_tab identify_tab = {
	.name = "Identify",
	.create = iwin_create,
	.activate_dev = iwin_activate_dev,
	.clear = iwin_clear,
	.refresh = iwin_refresh,
};

