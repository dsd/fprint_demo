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

static GtkWidget *mwin_verify_img;
static GtkWidget *mwin_fingcombo;
static GtkListStore *mwin_fingmodel;
static GtkWidget *mwin_vfy_status;
static GtkWidget *mwin_vfy_button;
static GtkWidget *mwin_non_img_label;
static GtkWidget *mwin_radio_normal;
static GtkWidget *mwin_radio_bin;
static GtkWidget *mwin_img_save_btn;
static GtkWidget *ctrl_frame;

static GdkPixbuf *pixbuf_normal = NULL;
static GdkPixbuf *pixbuf_bin = NULL;

static struct fp_dscv_print **discovered_prints = NULL;
static struct fp_print_data *enroll_data = NULL;

static const char *fingerstr(enum fp_finger finger)
{
	const char *names[] = {
		[LEFT_THUMB] = "Left Thumb",
		[LEFT_INDEX] = "Left Index Finger",
		[LEFT_MIDDLE] = "Left Middle Finger",
		[LEFT_RING] = "Left Ring Finger",
		[LEFT_LITTLE] = "Left Little Finger",
		[RIGHT_THUMB] = "Right Thumb",
		[RIGHT_INDEX] = "Right Index Finger",
		[RIGHT_MIDDLE] = "Right Middle Finger",
		[RIGHT_RING] = "Right Ring Finger",
		[RIGHT_LITTLE] = "Right Little Finger",
	};
	if (finger < LEFT_THUMB || finger > RIGHT_LITTLE)
		return "UNKNOWN";
	return names[finger];
}

static void mwin_vfy_status_no_print()
{
	gtk_label_set_markup(GTK_LABEL(mwin_vfy_status),
		"<b>Status:</b> No prints detected for this device.");
	gtk_widget_set_sensitive(mwin_vfy_button, FALSE);
}

static void mwin_fingcombo_select_first(void)
{
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(mwin_fingmodel), &iter))
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(mwin_fingcombo), &iter);
	else
		mwin_vfy_status_no_print();
}

static void vwin_clear(void)
{
	if (pixbuf_normal) {
		g_object_unref(pixbuf_normal);
		pixbuf_normal = NULL;
	}
	if (pixbuf_bin) {
		g_object_unref(pixbuf_bin);
		pixbuf_bin = NULL;
	}

	gtk_image_clear(GTK_IMAGE(mwin_verify_img));
	gtk_widget_set_sensitive(mwin_img_save_btn, FALSE);
	gtk_list_store_clear(GTK_LIST_STORE(mwin_fingmodel));
	fp_dscv_prints_free(discovered_prints);
	discovered_prints = NULL;

	gtk_label_set_text(GTK_LABEL(mwin_vfy_status), NULL);
	gtk_widget_set_sensitive(mwin_fingcombo, FALSE);
	gtk_widget_set_sensitive(mwin_vfy_button, FALSE);
}

static void vwin_activate_dev(void)
{
	struct fp_dscv_print *print;
	int i = 0;
	g_assert(fpdev);

	discovered_prints = fp_discover_prints();
	if (!discovered_prints) {
		vwin_clear();
		gtk_label_set_text(GTK_LABEL(mwin_vfy_status),
			"Error loading enrolled prints.");
		return;
	}

	while (print = discovered_prints[i++]) {
		GtkTreeIter iter;
		if (!fp_dev_supports_dscv_print(fpdev, print))
			continue;

		gtk_list_store_append(mwin_fingmodel, &iter);
		gtk_list_store_set(mwin_fingmodel, &iter, 0,
			fingerstr(fp_dscv_print_get_finger(print)), 1, print, -1);
	}

	gtk_widget_set_sensitive(mwin_fingcombo, TRUE);
	mwin_fingcombo_select_first();

	if (fp_dev_supports_imaging(fpdev)) {
		int width = fp_dev_get_img_width(fpdev);
		int height = fp_dev_get_img_height(fpdev);
		gtk_widget_set_size_request(mwin_verify_img,
			(width == 0) ? 192 : width,
			(height == 0) ? 192 : height);
		gtk_widget_hide(mwin_non_img_label);
		gtk_widget_show(mwin_verify_img);
		gtk_widget_set_sensitive(ctrl_frame, TRUE);
	} else {
		gtk_widget_show(mwin_non_img_label);
		gtk_widget_hide(mwin_verify_img);
		gtk_widget_set_sensitive(ctrl_frame, FALSE);
	}
}

static void mwin_vfy_status_print_loaded(int status)
{
	if (status == 0) {
		gtk_label_set_markup(GTK_LABEL(mwin_vfy_status),
			"<b>Status:</b> Ready for verify scan.");
		gtk_widget_set_sensitive(mwin_vfy_button, TRUE);
	} else {
		gchar *msg = g_strdup_printf("<b>Status:</b> Error %d, print corrupt?",
			status);
		gtk_label_set_markup(GTK_LABEL(mwin_vfy_status), msg);
		gtk_widget_set_sensitive(mwin_vfy_button, FALSE);
		g_free(msg);
	}
}

static void mwin_cb_fing_changed(GtkWidget *widget, gpointer user_data)
{
	struct fp_dscv_print *dprint;
	GtkTreeIter iter;
	int r;

	fp_print_data_free(enroll_data);
	enroll_data = NULL;

	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(mwin_fingcombo), &iter))
		return;

	gtk_tree_model_get(GTK_TREE_MODEL(mwin_fingmodel), &iter, 1, &dprint, -1);
	r = fp_print_data_from_dscv_print(dprint, &enroll_data);
	mwin_vfy_status_print_loaded(r);
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

static void mwin_vfy_status_verify_result(int code)
{
	const char *msgs[] = {
		[FP_VERIFY_NO_MATCH] = "Finger does not match.",
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

	gtk_label_set_markup(GTK_LABEL(mwin_vfy_status), msg);
	g_free(msg);
}

static unsigned char *img_to_rgb(struct fp_img *img)
{
	int size = fp_img_get_width(img) * fp_img_get_height(img);
	unsigned char *imgdata = fp_img_get_data(img);
	unsigned char *rgbdata = g_malloc(size * 3);
	size_t i;
	size_t rgb_offset = 0;

	for (i = 0; i < size; i++) {
		unsigned char pixel = imgdata[i];
		rgbdata[rgb_offset++] = pixel;
		rgbdata[rgb_offset++] = pixel;
		rgbdata[rgb_offset++] = pixel;
	}

	return rgbdata;
}

static void mwin_cb_imgfmt_toggled(GtkWidget *widget, gpointer data)
{
	if (!pixbuf_normal || !pixbuf_bin)
		return;

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(mwin_radio_normal)))
		gtk_image_set_from_pixbuf(GTK_IMAGE(mwin_verify_img), pixbuf_normal);
	else
		gtk_image_set_from_pixbuf(GTK_IMAGE(mwin_verify_img), pixbuf_bin);
	gtk_widget_set_sensitive(mwin_img_save_btn, TRUE);
}

static void pixbuf_destroy(guchar *pixels, gpointer data)
{
	g_free(pixels);
}

static void mwin_cb_verify(GtkWidget *widget, gpointer user_data)
{
	struct fp_print_data *data;
	struct fp_img *img = NULL;
	GtkWidget *dialog;
	int r;

	gtk_widget_set_sensitive(mwin_img_save_btn, FALSE);

	dialog = scan_finger_dialog_new(NULL);
	gtk_widget_show_all(dialog);
	while (gtk_events_pending())
		gtk_main_iteration();

	r = fp_verify_finger_img(fpdev, enroll_data, &img);
	gtk_widget_hide(dialog);
	gtk_widget_destroy(dialog);

	mwin_vfy_status_verify_result(r);

	if (pixbuf_normal) {
		g_object_unref(pixbuf_normal);
		pixbuf_normal = NULL;
	}
	if (pixbuf_bin) {
		g_object_unref(pixbuf_bin);
		pixbuf_bin = NULL;
	}

	if (img) {
		struct fp_img *img_bin = fp_img_binarize(img);
		unsigned char *rgbdata = img_to_rgb(img);
		unsigned char *rgbdata_bin = img_to_rgb(img_bin);
		int width = fp_img_get_width(img);
		int height = fp_img_get_height(img);

		fp_img_free(img);
		fp_img_free(img_bin);
		gtk_widget_set_size_request(mwin_verify_img, width, height);

		pixbuf_normal = gdk_pixbuf_new_from_data(rgbdata, GDK_COLORSPACE_RGB,
			FALSE, 8, width, height, width * 3, pixbuf_destroy, NULL);
		pixbuf_bin = gdk_pixbuf_new_from_data(rgbdata_bin, GDK_COLORSPACE_RGB,
			FALSE, 8, width, height, width * 3, pixbuf_destroy, NULL);
		mwin_cb_imgfmt_toggled(mwin_radio_normal, NULL);
	}
}

static void mwin_cb_img_save(GtkWidget *widget, gpointer user_data)
{
	GdkPixbuf *pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(mwin_verify_img));
	GtkWidget *dialog;
	gchar *filename;
	GError *error = NULL;

	g_assert(pixbuf);

	dialog = gtk_file_chooser_dialog_new("Save Image", GTK_WINDOW(mwin_window),
		GTK_FILE_CHOOSER_ACTION_SAVE,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL,
		GTK_STOCK_SAVE, GTK_RESPONSE_ACCEPT, NULL);

	gtk_file_chooser_set_do_overwrite_confirmation(GTK_FILE_CHOOSER(dialog),
		TRUE);
	gtk_file_chooser_set_current_name(GTK_FILE_CHOOSER(dialog),
		"fingerprint.png");

	if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy(dialog);
		return;
	}

	filename = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
	gtk_widget_destroy(dialog);
	if (!gdk_pixbuf_save(pixbuf, filename, "png", &error, NULL)) {
		dialog = gtk_message_dialog_new(GTK_WINDOW(mwin_window),
			GTK_DIALOG_DESTROY_WITH_PARENT, GTK_MESSAGE_ERROR,
			GTK_BUTTONS_CLOSE, error->message);
		gtk_dialog_run(GTK_DIALOG(dialog));
		gtk_widget_destroy(dialog);
		g_error_free(error);
	}
	g_free(filename);
}

static GtkWidget *vwin_create(void)
{
	GtkCellRenderer *renderer;
	GtkWidget *main_vbox, *dev_vbox, *lower_hbox, *ui_vbox;
	GtkWidget *button, *label, *vfy_vbox, *vfy_frame, *scan_frame, *img_vbox;
	GtkWidget *mwin_ctrl_vbox;
	GtkWidget *notebook, *vwin_main_hbox;

	vwin_main_hbox = gtk_hbox_new(FALSE, 1);

	/* Image frame */
	scan_frame = gtk_frame_new("Scanned Image");
	gtk_box_pack_start(GTK_BOX(vwin_main_hbox), scan_frame, FALSE, FALSE, 0);

	/* Image vbox */
	img_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(scan_frame), img_vbox);

	/* Image */
	mwin_verify_img = gtk_image_new();
	gtk_box_pack_start(GTK_BOX(img_vbox), mwin_verify_img, FALSE, FALSE, 0);

	/* Non-imaging device */
	mwin_non_img_label = gtk_label_new("This device does not have imaging "
		"capabilities, no images will be displayed.");
	gtk_box_pack_start_defaults(GTK_BOX(img_vbox), mwin_non_img_label);

	/* vbox for verification status and image control frames */
	ui_vbox = gtk_vbox_new(FALSE, 1);
	gtk_box_pack_start_defaults(GTK_BOX(vwin_main_hbox), ui_vbox);

	/* Verification status */
	vfy_frame = gtk_frame_new("Verification");
	gtk_box_pack_start_defaults(GTK_BOX(ui_vbox), vfy_frame);

	vfy_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(vfy_frame), vfy_vbox);

	/* Discovered prints list */
	label = gtk_label_new("Select a finger to verify:");
	gtk_box_pack_start(GTK_BOX(vfy_vbox), label, FALSE, FALSE, 0);
	mwin_fingmodel = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
	mwin_fingcombo =
		gtk_combo_box_new_with_model(GTK_TREE_MODEL(mwin_fingmodel));
	g_signal_connect(G_OBJECT(mwin_fingcombo), "changed",
		G_CALLBACK(mwin_cb_fing_changed), NULL);

	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(mwin_fingcombo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(mwin_fingcombo), renderer,
		"text", 0, NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), mwin_fingcombo, FALSE, FALSE, 0);

	/* Verify button */
	mwin_vfy_button = gtk_button_new_with_label("Verify");
	g_signal_connect(G_OBJECT(mwin_vfy_button), "clicked",
		G_CALLBACK(mwin_cb_verify), NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), mwin_vfy_button, FALSE, FALSE, 0);

	/* Verify status */
	mwin_vfy_status = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), mwin_vfy_status, FALSE, FALSE, 0);

	/* Image controls frame */
	ctrl_frame = gtk_frame_new("Image control");
	gtk_box_pack_end_defaults(GTK_BOX(ui_vbox), ctrl_frame);

	mwin_ctrl_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(ctrl_frame), mwin_ctrl_vbox);

	/* Image format radio buttons */
	mwin_radio_normal = gtk_radio_button_new_with_label(NULL, "Normal");
	g_signal_connect(G_OBJECT(mwin_radio_normal), "toggled",
		G_CALLBACK(mwin_cb_imgfmt_toggled), NULL);
	gtk_box_pack_start(GTK_BOX(mwin_ctrl_vbox), mwin_radio_normal, FALSE,
		FALSE, 0);

	mwin_radio_bin = gtk_radio_button_new_with_label_from_widget(
		GTK_RADIO_BUTTON(mwin_radio_normal), "Binarized");
	gtk_box_pack_start(GTK_BOX(mwin_ctrl_vbox), mwin_radio_bin, FALSE,
		FALSE, 0);

	/* Save image */
	mwin_img_save_btn = gtk_button_new_from_stock(GTK_STOCK_SAVE);
	g_signal_connect(G_OBJECT(mwin_img_save_btn), "clicked",
		G_CALLBACK(mwin_cb_img_save), NULL);
	gtk_box_pack_end(GTK_BOX(mwin_ctrl_vbox), mwin_img_save_btn, FALSE,
		FALSE, 0);

	return vwin_main_hbox;
}

static void vwin_exit(void)
{
	fp_print_data_free(enroll_data);
}

struct fpd_tab verify_tab = {
	.name = "Verify",
	.create = vwin_create,
	.activate_dev = vwin_activate_dev,
	.clear = vwin_clear,
	.exit = vwin_exit,
};

