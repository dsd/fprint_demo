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

static GtkListStore *mwin_devmodel;
static GtkWidget *mwin_window;
static GtkWidget *mwin_devcombo;
static GtkWidget *mwin_drvname_label;
static GtkWidget *mwin_imgcapa_label;
static GtkWidget *mwin_devstatus_label;
static GtkWidget *mwin_verify_img;
static GtkWidget *mwin_fingcombo;
static GtkListStore *mwin_fingmodel;
static GtkWidget *mwin_vfy_status;
static GtkWidget *mwin_vfy_button;

static struct fp_dev *fpdev = NULL;
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

static void mwin_cb_destroy(GtkWidget *widget, gpointer data)
{
    gtk_main_quit();
}

static void mwin_devstatus_update(char *status)
{
	gchar *msg = g_strdup_printf("<b>Status:</b> %s", status);
	gtk_label_set_markup(GTK_LABEL(mwin_devstatus_label), msg);
	g_free(msg);
}

static void mwin_cb_dev_changed(GtkWidget *widget, gpointer user_data)
{
	GtkTreeIter iter;
	struct fp_dscv_dev *ddev;
	struct fp_driver *drv;
	struct fp_dscv_print *print;
	gchar *tmp;
	int i = 0;

	gtk_image_clear(GTK_IMAGE(mwin_verify_img));

	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(mwin_devcombo), &iter)) {
		mwin_devstatus_update("No devices found.");
		goto err;
	}

	gtk_tree_model_get(GTK_TREE_MODEL(mwin_devmodel), &iter, 1, &ddev, -1);

	if (fpdev)
		fp_dev_close(fpdev);

	gtk_list_store_clear(GTK_LIST_STORE(mwin_fingmodel));
	fp_dscv_prints_free(discovered_prints);
	discovered_prints = fp_discover_prints();
	if (!discovered_prints) {
		mwin_devstatus_update("Error locating enrolled prints.");
		goto err;
	}

	fpdev = fp_dev_open(ddev);
	if (!fpdev) {
		mwin_devstatus_update("Could not open device.");
		goto err;
	}

	mwin_devstatus_update("Device ready for use.");

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

	drv = fp_dev_get_driver(fpdev);
	tmp = g_strdup_printf("<b>Driver:</b> %s", fp_driver_get_name(drv));
	gtk_label_set_markup(GTK_LABEL(mwin_drvname_label), tmp);
	g_free(tmp);

	if (fp_dev_supports_imaging(fpdev)) {
		int width = fp_dev_get_img_width(fpdev);
		int height = fp_dev_get_img_height(fpdev);
		gtk_widget_set_size_request(mwin_verify_img,
			(width == 0) ? -1 : width,
			(height == 0) ? -1 : height);
		gtk_label_set_markup(GTK_LABEL(mwin_imgcapa_label), "Imaging device");
	} else
		gtk_label_set_markup(GTK_LABEL(mwin_imgcapa_label),
			"Non-imaging device");
	return;

err:
	gtk_label_set_text(GTK_LABEL(mwin_drvname_label), NULL);
	gtk_label_set_text(GTK_LABEL(mwin_imgcapa_label), NULL);
	gtk_label_set_text(GTK_LABEL(mwin_vfy_status), NULL);
	gtk_widget_set_sensitive(mwin_fingcombo, FALSE);
	gtk_widget_set_sensitive(mwin_vfy_button, FALSE);
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

	dialog = scan_finger_dialog_new(NULL);
	gtk_widget_show_all(dialog);
	while (gtk_events_pending())
		gtk_main_iteration();

	r = fp_verify_finger_img(fpdev, enroll_data, &img);
	gtk_widget_hide(dialog);
	gtk_widget_destroy(dialog);

	mwin_vfy_status_verify_result(r);

	if (img) {
		GdkPixbuf *pixbuf;
		unsigned char *rgbdata = img_to_rgb(img);
		int width = fp_img_get_width(img);
		int height = fp_img_get_height(img);

		fp_img_free(img);
		gtk_widget_set_size_request(mwin_verify_img, width, height);

		pixbuf = gdk_pixbuf_new_from_data(rgbdata, GDK_COLORSPACE_RGB, FALSE,
			8, width, height, width * 3, pixbuf_destroy, NULL);
		gtk_image_set_from_pixbuf(GTK_IMAGE(mwin_verify_img), pixbuf);
		g_object_unref(G_OBJECT(pixbuf));
	}
}

static void mwin_create(void)
{
	GtkCellRenderer *renderer;
	GtkWidget *main_vbox, *dev_vbox, *lower_hbox, *upper_hbox;
	GtkWidget *button, *label, *vfy_vbox, *vfy_frame, *scan_frame;

	/* Window */
	mwin_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(mwin_window), "fprint project demo");
    g_signal_connect(G_OBJECT(mwin_window), "destroy",
		G_CALLBACK(mwin_cb_destroy), NULL);

	/* Top level vbox */
	main_vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(mwin_window), main_vbox);

	/* hbox for lower and upper halves */
	upper_hbox = gtk_hbox_new(FALSE, 3);
	gtk_box_pack_start_defaults(GTK_BOX(main_vbox), upper_hbox);
	lower_hbox = gtk_hbox_new(FALSE, 3);
	gtk_box_pack_start(GTK_BOX(main_vbox), lower_hbox, FALSE, FALSE, 0);

	/* Device vbox */
	dev_vbox = gtk_vbox_new(FALSE, 1);
	gtk_box_pack_start_defaults(GTK_BOX(lower_hbox), dev_vbox);

	/* Device model and combo box */
	mwin_devmodel = gtk_list_store_new(2, G_TYPE_STRING, G_TYPE_POINTER);
	mwin_devcombo =
		gtk_combo_box_new_with_model(GTK_TREE_MODEL(mwin_devmodel));
    g_signal_connect(G_OBJECT(mwin_devcombo), "changed",
		G_CALLBACK(mwin_cb_dev_changed), NULL);

	renderer = gtk_cell_renderer_text_new();
	gtk_cell_layout_pack_start(GTK_CELL_LAYOUT(mwin_devcombo), renderer, TRUE);
	gtk_cell_layout_set_attributes(GTK_CELL_LAYOUT(mwin_devcombo), renderer,
		"text", 0, NULL);

	gtk_box_pack_start(GTK_BOX(dev_vbox), mwin_devcombo, FALSE, FALSE, 0);

	/* Labels */
	mwin_devstatus_label = gtk_label_new(NULL);
	gtk_box_pack_start_defaults(GTK_BOX(dev_vbox), mwin_devstatus_label);

	mwin_drvname_label = gtk_label_new(NULL);
	gtk_box_pack_start_defaults(GTK_BOX(dev_vbox), mwin_drvname_label);

	mwin_imgcapa_label = gtk_label_new(NULL);
	gtk_box_pack_start_defaults(GTK_BOX(dev_vbox), mwin_imgcapa_label);

	/* Buttons */
	button = gtk_button_new_from_stock(GTK_STOCK_QUIT);
    g_signal_connect(G_OBJECT(button), "clicked", G_CALLBACK(mwin_cb_destroy),
		NULL);
	gtk_box_pack_start(GTK_BOX(lower_hbox), button, FALSE, FALSE, 0);

	/* Image frame */
	scan_frame = gtk_frame_new("Scanned Image");
	gtk_box_pack_start(GTK_BOX(upper_hbox), scan_frame, FALSE, FALSE, 0);

	/* Image */
	mwin_verify_img = gtk_image_new();
	gtk_container_add(GTK_CONTAINER(scan_frame), mwin_verify_img);

	/* Verification status */
	vfy_frame = gtk_frame_new("Verification");
	gtk_box_pack_start_defaults(GTK_BOX(upper_hbox), vfy_frame);

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

	gtk_widget_show_all(mwin_window);
}

static gboolean mwin_populate_devs(void)
{
	struct fp_dscv_dev **discovered_devs;
	struct fp_dscv_dev *ddev;
	int i;

	discovered_devs = fp_discover_devs();
	if (!discovered_devs)
		return FALSE;

	for (i = 0; ddev = discovered_devs[i]; i++) {
		struct fp_driver *drv = fp_dscv_dev_get_driver(ddev);
		GtkTreeIter iter;

		gtk_list_store_append(mwin_devmodel, &iter);
		gtk_list_store_set(mwin_devmodel, &iter, 0,
			fp_driver_get_full_name(drv), 1, ddev, -1);
	}

	return TRUE;
}

static gboolean mwin_select_first_dev(void)
{
	gtk_combo_box_set_active(GTK_COMBO_BOX(mwin_devcombo), 0);
	return TRUE;
}

int main(int argc, char **argv)
{
	int r;

	r = fp_init();
	if (r < 0)
		return r;

	gtk_init(&argc, &argv);

	mwin_create();
	mwin_populate_devs();
	mwin_select_first_dev();

	gtk_main();

	if (fpdev)
		fp_dev_close(fpdev);
	fp_dscv_prints_free(discovered_prints);
	fp_print_data_free(enroll_data);
	fp_exit();
	return 0;
}

