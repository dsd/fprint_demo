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

static GtkWidget *vwin_verify_img;
static GtkWidget *vwin_fingcombo;
static GtkListStore *vwin_fingmodel;
static GtkWidget *vwin_vfy_status;
static GtkWidget *vwin_vfy_button;
static GtkWidget *vwin_non_img_label;
static GtkWidget *vwin_radio_normal;
static GtkWidget *vwin_radio_bin;
static GtkWidget *vwin_img_save_btn;
static GtkWidget *vwin_ctrl_frame;
static GtkWidget *vwin_show_minutiae;
static GtkWidget *vwin_minutiae_cnt;

static struct fp_img *img_normal = NULL;
static struct fp_img *img_bin = NULL;
static struct fp_print_data *enroll_data = NULL;

static void vwin_vfy_status_no_print()
{
	gtk_label_set_markup(GTK_LABEL(vwin_vfy_status),
		"<b>Status:</b> No prints detected for this device.");
	gtk_widget_set_sensitive(vwin_vfy_button, FALSE);
}

static void vwin_fingcombo_select_first(void)
{
	GtkTreeIter iter;
	if (gtk_tree_model_get_iter_first(GTK_TREE_MODEL(vwin_fingmodel), &iter))
		gtk_combo_box_set_active_iter(GTK_COMBO_BOX(vwin_fingcombo), &iter);
	else
		vwin_vfy_status_no_print();
}

static void vwin_clear(void)
{
	fp_img_free(img_normal);
	img_normal = NULL;
	fp_img_free(img_bin);
	img_bin = NULL;

	fp_print_data_free(enroll_data);
	enroll_data = NULL;

	gtk_image_clear(GTK_IMAGE(vwin_verify_img));
	gtk_widget_set_sensitive(vwin_img_save_btn, FALSE);
	gtk_list_store_clear(GTK_LIST_STORE(vwin_fingmodel));

	gtk_label_set_text(GTK_LABEL(vwin_vfy_status), NULL);
	gtk_label_set_text(GTK_LABEL(vwin_minutiae_cnt), NULL);
	gtk_widget_set_sensitive(vwin_fingcombo, FALSE);
	gtk_widget_set_sensitive(vwin_vfy_button, FALSE);
}

enum fingcombo_cols {
	FC_COL_PRINT,
	FC_COL_FINGNUM,
	FC_COL_FINGSTR,
};

static void vwin_refresh(void)
{
	struct fp_dscv_print *print;
	GtkTreeIter iter;
	int orig_fnum = -1;
	int i = 0;
	int fnum;

	/* find and remember currently selected finger */
	if (gtk_combo_box_get_active_iter(GTK_COMBO_BOX(vwin_fingcombo), &iter)) {
		gtk_tree_model_get(GTK_TREE_MODEL(vwin_fingmodel), &iter,
			FC_COL_FINGNUM, &orig_fnum, -1);
	}

	/* re-populate list */
	gtk_list_store_clear(GTK_LIST_STORE(vwin_fingmodel));
	while (print = fp_dscv_prints[i++]) {
		if (!fp_dev_supports_dscv_print(fpdev, print))
			continue;

		fnum = fp_dscv_print_get_finger(print);
		gtk_list_store_append(vwin_fingmodel, &iter);
		gtk_list_store_set(vwin_fingmodel, &iter, FC_COL_PRINT, print,
			FC_COL_FINGSTR, fingerstr(fnum), FC_COL_FINGNUM, fnum, -1);
	}

	/* try and select original again */
	if (!gtk_tree_model_get_iter_first(GTK_TREE_MODEL(vwin_fingmodel), &iter)
			|| orig_fnum == -1) {
		vwin_fingcombo_select_first();
		return;
	}

	do {
		gtk_tree_model_get(GTK_TREE_MODEL(vwin_fingmodel), &iter,
			FC_COL_FINGNUM, &fnum, -1);
		if (fnum == orig_fnum) {
			gtk_combo_box_set_active_iter(GTK_COMBO_BOX(vwin_fingcombo),
				&iter);
			return;
		}
	} while (gtk_tree_model_iter_next(GTK_TREE_MODEL(vwin_fingmodel), &iter));

	/* could not find original -- it may have been deleted */
	vwin_fingcombo_select_first();
}

static void vwin_activate_dev(void)
{
	struct fp_dscv_print *print;
	int i = 0;
	g_assert(fpdev);
	g_assert(fp_dscv_prints);

	while (print = fp_dscv_prints[i++]) {
		GtkTreeIter iter;
		int fnum;

		if (!fp_dev_supports_dscv_print(fpdev, print))
			continue;

		fnum = fp_dscv_print_get_finger(print);
		gtk_list_store_append(vwin_fingmodel, &iter);
		gtk_list_store_set(vwin_fingmodel, &iter, FC_COL_PRINT, print,
			FC_COL_FINGSTR, fingerstr(fnum), FC_COL_FINGNUM, fnum, -1);
	}

	gtk_widget_set_sensitive(vwin_fingcombo, TRUE);
	vwin_fingcombo_select_first();

	if (fp_dev_supports_imaging(fpdev)) {
		int width = fp_dev_get_img_width(fpdev);
		int height = fp_dev_get_img_height(fpdev);
		gtk_widget_set_size_request(vwin_verify_img,
			(width == 0) ? 192 : width,
			(height == 0) ? 192 : height);
		gtk_widget_hide(vwin_non_img_label);
		gtk_widget_show(vwin_verify_img);
		gtk_widget_set_sensitive(vwin_ctrl_frame, TRUE);
	} else {
		gtk_widget_show(vwin_non_img_label);
		gtk_widget_hide(vwin_verify_img);
		gtk_widget_set_sensitive(vwin_ctrl_frame, FALSE);
	}
}

static void vwin_vfy_status_print_loaded(int status)
{
	if (status == 0) {
		gtk_label_set_markup(GTK_LABEL(vwin_vfy_status),
			"<b>Status:</b> Ready for verify scan.");
		gtk_widget_set_sensitive(vwin_vfy_button, TRUE);
	} else {
		gchar *msg = g_strdup_printf("<b>Status:</b> Error %d, print corrupt?",
			status);
		gtk_label_set_markup(GTK_LABEL(vwin_vfy_status), msg);
		gtk_widget_set_sensitive(vwin_vfy_button, FALSE);
		g_free(msg);
	}
}

static void vwin_cb_fing_changed(GtkWidget *widget, gpointer user_data)
{
	struct fp_dscv_print *dprint;
	GtkTreeIter iter;
	int r;

	fp_print_data_free(enroll_data);
	enroll_data = NULL;

	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(vwin_fingcombo), &iter))
		return;

	gtk_tree_model_get(GTK_TREE_MODEL(vwin_fingmodel), &iter,
		FC_COL_PRINT, &dprint, -1);
	r = fp_print_data_from_dscv_print(dprint, &enroll_data);
	vwin_vfy_status_print_loaded(r);
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

static void vwin_vfy_status_verify_result(int code)
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

	if (code < 0) {
		msg = g_strdup_printf("<b>Status:</b> Scan failed, error %d", code);
		gtk_label_set_text(GTK_LABEL(vwin_minutiae_cnt), NULL);
	} else {
		msg = g_strdup_printf("<b>Status:</b> %s", msgs[code]);
	}

	gtk_label_set_markup(GTK_LABEL(vwin_vfy_status), msg);
	g_free(msg);
}

static void plot_minutiae(unsigned char *rgbdata, int width, int height,
	struct fp_minutia **minlist, int nr_minutiae)
{
	int i;
#define write_pixel(num) do { \
		rgbdata[((num) * 3)] = 0xff; \
		rgbdata[((num) * 3) + 1] = 0; \
		rgbdata[((num) * 3) + 2] = 0; \
	} while(0)

	for (i = 0; i < nr_minutiae; i++) {
		struct fp_minutia *min = minlist[i];
		size_t pixel_offset = (min->y * width) + min->x;
		write_pixel(pixel_offset - 2);
		write_pixel(pixel_offset - 1);
		write_pixel(pixel_offset);
		write_pixel(pixel_offset + 1);
		write_pixel(pixel_offset + 2);

		write_pixel(pixel_offset - (width * 2));
		write_pixel(pixel_offset - (width * 1) - 1);
		write_pixel(pixel_offset - (width * 1));
		write_pixel(pixel_offset - (width * 1) + 1);
		write_pixel(pixel_offset + (width * 1) - 1);
		write_pixel(pixel_offset + (width * 1));
		write_pixel(pixel_offset + (width * 1) + 1);
		write_pixel(pixel_offset + (width * 2));
	}
}

static void vwin_img_draw()
{
	struct fp_minutia **minlist;
	unsigned char *rgbdata;
	GdkPixbuf *pixbuf;
	gchar *tmp;
	int nr_minutiae;
	int width;
	int height;

	if (!img_normal || !img_bin)
		return;

	minlist = fp_img_get_minutiae(img_normal, &nr_minutiae);

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vwin_radio_normal)))
		rgbdata = img_to_rgbdata(img_normal);
	else
		rgbdata = img_to_rgbdata(img_bin);

	width = fp_img_get_width(img_normal);
	height = fp_img_get_height(img_normal);
	gtk_widget_set_size_request(vwin_verify_img, width, height);

	tmp = g_strdup_printf("Detected %d minutiae.", nr_minutiae);
	gtk_label_set_text(GTK_LABEL(vwin_minutiae_cnt), tmp);
	g_free(tmp);

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(vwin_show_minutiae)))
		plot_minutiae(rgbdata, width, height, minlist, nr_minutiae);

	pixbuf = gdk_pixbuf_new_from_data(rgbdata, GDK_COLORSPACE_RGB,
			FALSE, 8, width, height, width * 3, pixbuf_destroy, NULL);
	gtk_image_set_from_pixbuf(GTK_IMAGE(vwin_verify_img), pixbuf);
	g_object_unref(pixbuf);

	gtk_widget_set_sensitive(vwin_img_save_btn, TRUE);
}

static void vwin_cb_imgfmt_toggled(GtkWidget *widget, gpointer data)
{
	vwin_img_draw();
}

static void vwin_cb_verify(GtkWidget *widget, gpointer user_data)
{
	struct fp_print_data *data;
	struct fp_img *img = NULL;
	GtkWidget *dialog;
	int r;

	gtk_widget_set_sensitive(vwin_img_save_btn, FALSE);

	dialog = scan_finger_dialog_new(NULL);
	gtk_widget_show_all(dialog);
	while (gtk_events_pending())
		gtk_main_iteration();

	r = fp_verify_finger_img(fpdev, enroll_data, &img);
	gtk_widget_hide(dialog);
	gtk_widget_destroy(dialog);

	vwin_vfy_status_verify_result(r);

	fp_img_free(img_normal);
	img_normal = NULL;
	fp_img_free(img_bin);
	img_bin = NULL;

	if (img) {
		img_normal = img;
		img_bin = fp_img_binarize(img);
		vwin_img_draw();
	}
}

static void vwin_cb_img_save(GtkWidget *widget, gpointer user_data)
{
	GdkPixbuf *pixbuf = gtk_image_get_pixbuf(GTK_IMAGE(vwin_verify_img));
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

static gint fing_sort(GtkTreeModel *model, GtkTreeIter *a, GtkTreeIter *b,
	gpointer data)
{
	int num1, num2;
	gtk_tree_model_get(model, a, FC_COL_FINGNUM, &num1, -1);
	gtk_tree_model_get(model, b, FC_COL_FINGNUM, &num2, -1);
	return num1 - num2;
}

static GtkWidget *vwin_create(void)
{
	GtkCellRenderer *renderer;
	GtkWidget *main_vbox, *dev_vbox, *lower_hbox, *ui_vbox;
	GtkWidget *button, *label, *vfy_vbox, *vfy_frame, *scan_frame, *img_vbox;
	GtkWidget *vwin_ctrl_vbox;
	GtkWidget *notebook, *vwin_main_hbox;

	vwin_main_hbox = gtk_hbox_new(FALSE, 1);

	/* Image frame */
	scan_frame = gtk_frame_new("Scanned Image");
	gtk_box_pack_start(GTK_BOX(vwin_main_hbox), scan_frame, TRUE, TRUE, 0);

	/* Image vbox */
	img_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(scan_frame), img_vbox);

	/* Image */
	vwin_verify_img = gtk_image_new();
	gtk_box_pack_start(GTK_BOX(img_vbox), vwin_verify_img, TRUE, FALSE, 0);

	/* Non-imaging device */
	vwin_non_img_label = gtk_label_new("This device does not have imaging "
		"capabilities, no images will be displayed.");
	gtk_box_pack_start_defaults(GTK_BOX(img_vbox), vwin_non_img_label);

	/* vbox for verification status and image control frames */
	ui_vbox = gtk_vbox_new(FALSE, 1);
	gtk_box_pack_end(GTK_BOX(vwin_main_hbox), ui_vbox, FALSE, FALSE, 0);

	/* Verification status */
	vfy_frame = gtk_frame_new("Verification");
	gtk_box_pack_start_defaults(GTK_BOX(ui_vbox), vfy_frame);

	vfy_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(vfy_frame), vfy_vbox);

	/* Discovered prints list */
	label = gtk_label_new("Select a finger to verify:");
	gtk_box_pack_start(GTK_BOX(vfy_vbox), label, FALSE, FALSE, 0);
	vwin_fingmodel = gtk_list_store_new(3, G_TYPE_POINTER, G_TYPE_INT,
		G_TYPE_STRING);
	vwin_fingcombo =
		gtk_combo_box_new_with_model(GTK_TREE_MODEL(vwin_fingmodel));
	g_signal_connect(G_OBJECT(vwin_fingcombo), "changed",
		G_CALLBACK(vwin_cb_fing_changed), NULL);
	gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(vwin_fingmodel), 0,
		fing_sort, NULL, NULL);
	gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(vwin_fingmodel),
		0, GTK_SORT_ASCENDING);

	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(vwin_fingcombo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(vwin_fingcombo), renderer,
		"text", FC_COL_FINGSTR, NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), vwin_fingcombo, FALSE, FALSE, 0);

	/* Verify button */
	vwin_vfy_button = gtk_button_new_with_label("Verify");
	g_signal_connect(G_OBJECT(vwin_vfy_button), "clicked",
		G_CALLBACK(vwin_cb_verify), NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), vwin_vfy_button, FALSE, FALSE, 0);

	/* Verify status */
	vwin_vfy_status = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), vwin_vfy_status, FALSE, FALSE, 0);

	/* Minutiae count */
	vwin_minutiae_cnt = gtk_label_new(NULL);
	gtk_box_pack_start(GTK_BOX(vfy_vbox), vwin_minutiae_cnt, FALSE, FALSE, 0);

	/* Image controls frame */
	vwin_ctrl_frame = gtk_frame_new("Image control");
	gtk_box_pack_end_defaults(GTK_BOX(ui_vbox), vwin_ctrl_frame);

	vwin_ctrl_vbox = gtk_vbox_new(FALSE, 1);
	gtk_container_add(GTK_CONTAINER(vwin_ctrl_frame), vwin_ctrl_vbox);

	/* Image format radio buttons */
	vwin_radio_normal = gtk_radio_button_new_with_label(NULL, "Normal");
	g_signal_connect(G_OBJECT(vwin_radio_normal), "toggled",
		G_CALLBACK(vwin_cb_imgfmt_toggled), NULL);
	gtk_box_pack_start(GTK_BOX(vwin_ctrl_vbox), vwin_radio_normal, FALSE,
		FALSE, 0);

	vwin_radio_bin = gtk_radio_button_new_with_label_from_widget(
		GTK_RADIO_BUTTON(vwin_radio_normal), "Binarized");
	gtk_box_pack_start(GTK_BOX(vwin_ctrl_vbox), vwin_radio_bin, FALSE,
		FALSE, 0);

	/* Minutiae plotting */
	vwin_show_minutiae = gtk_check_button_new_with_label("Show minutiae");
	g_signal_connect(GTK_OBJECT(vwin_show_minutiae), "toggled",
		G_CALLBACK(vwin_cb_imgfmt_toggled), NULL);
	gtk_box_pack_start(GTK_BOX(vwin_ctrl_vbox), vwin_show_minutiae, FALSE,
		FALSE, 0);

	/* Save image */
	vwin_img_save_btn = gtk_button_new_from_stock(GTK_STOCK_SAVE);
	g_signal_connect(G_OBJECT(vwin_img_save_btn), "clicked",
		G_CALLBACK(vwin_cb_img_save), NULL);
	gtk_box_pack_end(GTK_BOX(vwin_ctrl_vbox), vwin_img_save_btn, FALSE,
		FALSE, 0);

	return vwin_main_hbox;
}

struct fpd_tab verify_tab = {
	.name = "Verify",
	.create = vwin_create,
	.activate_dev = vwin_activate_dev,
	.clear = vwin_clear,
	.refresh = vwin_refresh,
};

