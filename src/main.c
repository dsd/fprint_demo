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

#define for_each_tab_call_op(op) do { \
		int __fe_tabno; \
		for (__fe_tabno = 0; __fe_tabno < G_N_ELEMENTS(tabs); __fe_tabno++) { \
			const struct fpd_tab *__fe_tab = tabs[__fe_tabno]; \
			if (__fe_tab->op) \
				__fe_tab->op(); \
		} \
	} while(0);

static GtkWidget *mwin_devcombo;
static GtkListStore *mwin_devmodel;
static GtkWidget *mwin_drvname_label;
static GtkWidget *mwin_imgcapa_label;
static GtkWidget *mwin_devstatus_label;
static GtkWidget *mwin_notebook;

struct fp_dev *fpdev = NULL;
struct fp_dscv_print **fp_dscv_prints = NULL;
GtkWidget *mwin_window;

static const struct fpd_tab *tabs[] = {
	&enroll_tab,
	&verify_tab,
	&identify_tab,
	&img_tab,
};

static gboolean mwin_cb_tab_changed(GtkNotebook *notebook,
	GtkNotebookPage *page, guint tabnum, gpointer data)
{
	const struct fpd_tab *tab = tabs[tabnum];
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
	gchar *tmp;
	int i;

	for_each_tab_call_op(clear);

	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(mwin_devcombo), &iter)) {
		mwin_devstatus_update("No devices found.");
		goto err;
	}

	gtk_tree_model_get(GTK_TREE_MODEL(mwin_devmodel), &iter, 1, &ddev, -1);

	fp_dscv_prints_free(fp_dscv_prints);
	fp_dscv_prints = NULL;
	fp_dev_close(fpdev);

	fpdev = fp_dev_open(ddev);
	if (!fpdev) {
		mwin_devstatus_update("Could not open device.");
		goto err;
	}

	fp_dscv_prints = fp_discover_prints();
	if (!fp_dscv_prints) {
		mwin_devstatus_update("Error loading enrolled prints.");
		goto err;
	}

	mwin_devstatus_update("Device ready for use.");

	drv = fp_dev_get_driver(fpdev);
	tmp = g_strdup_printf("<b>Driver:</b> %s", fp_driver_get_name(drv));
	gtk_label_set_markup(GTK_LABEL(mwin_drvname_label), tmp);
	g_free(tmp);

	if (fp_dev_supports_imaging(fpdev))
		gtk_label_set_markup(GTK_LABEL(mwin_imgcapa_label), "Imaging device");
	else
		gtk_label_set_markup(GTK_LABEL(mwin_imgcapa_label),
			"Non-imaging device");

	for_each_tab_call_op(activate_dev);
	return;

err:
	if (fpdev)
		fp_dev_close(fpdev);
	fpdev = NULL;

	gtk_label_set_text(GTK_LABEL(mwin_drvname_label), NULL);
	gtk_label_set_text(GTK_LABEL(mwin_imgcapa_label), NULL);
}

static void mwin_cb_destroy(GtkWidget *widget, gpointer data)
{
	gtk_main_quit();
}

static GtkWidget *mwin_create_devbar(void)
{
	GtkCellRenderer *renderer;
	GtkWidget *devbar_hbox, *dev_vbox, *button;

	/* hbox for lower and upper halves */
	devbar_hbox = gtk_hbox_new(FALSE, 3);

	/* Device vbox */
	dev_vbox = gtk_vbox_new(FALSE, 1);
	gtk_box_pack_start_defaults(GTK_BOX(devbar_hbox), dev_vbox);

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
	gtk_box_pack_start(GTK_BOX(devbar_hbox), button, FALSE, FALSE, 0);

	return devbar_hbox;
}

static void mwin_create(void)
{
	GtkWidget *main_vbox;
	int i;

	/* Window */
	mwin_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(mwin_window), "fprint project demo");
	g_signal_connect(G_OBJECT(mwin_window), "destroy",
		G_CALLBACK(mwin_cb_destroy), NULL);

	/* Top level vbox */
	main_vbox = gtk_vbox_new(FALSE, 5);
	gtk_container_add(GTK_CONTAINER(mwin_window), main_vbox);

	/* Notebook */
	mwin_notebook = gtk_notebook_new();
	gtk_box_pack_start_defaults(GTK_BOX(main_vbox), mwin_notebook);

	for (i = 0; i < G_N_ELEMENTS(tabs); i++) {
		const struct fpd_tab *tab = tabs[i];
		gtk_notebook_append_page(GTK_NOTEBOOK(mwin_notebook),
			tab->create(), gtk_label_new(tab->name));
	}
	for_each_tab_call_op(clear);

	/* Device bar */
	gtk_box_pack_end(GTK_BOX(main_vbox), mwin_create_devbar(), FALSE, FALSE, 0);

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
	int i, r;

	r = fp_init();
	if (r < 0)
		return r;

	gtk_init(&argc, &argv);

	mwin_create();
	mwin_populate_devs();
	mwin_select_first_dev();

	/* don't connect this handler until late in order to avoid callbacks
	 * while we are still adding tabs and stuff. */
	g_signal_connect(G_OBJECT(mwin_notebook), "switch-page",
		G_CALLBACK(mwin_cb_tab_changed), NULL);

	gtk_main();

	if (fpdev)
		fp_dev_close(fpdev);
	fp_exit();
	return 0;
}

const char *fingerstr(enum fp_finger finger)
{
	const char *names[] = {
		[LEFT_THUMB] = "Left thumb",
		[LEFT_INDEX] = "Left index finger",
		[LEFT_MIDDLE] = "Left middle finger",
		[LEFT_RING] = "Left ring finger",
		[LEFT_LITTLE] = "Left little finger",
		[RIGHT_THUMB] = "Right thumb",
		[RIGHT_INDEX] = "Right index finger",
		[RIGHT_MIDDLE] = "Right middle finger",
		[RIGHT_RING] = "Right ring finger",
		[RIGHT_LITTLE] = "Right little finger",
	};
	if (finger < LEFT_THUMB || finger > RIGHT_LITTLE)
		return "UNKNOWN";
	return names[finger];
}

void pixbuf_destroy(guchar *pixels, gpointer data)
{
	g_free(pixels);
}

unsigned char *img_to_rgbdata(struct fp_img *img)
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

GdkPixbuf *img_to_pixbuf(struct fp_img *img)
{
	int width = fp_img_get_width(img);
	int height = fp_img_get_height(img);
	unsigned char *rgbdata = img_to_rgbdata(img);

	return gdk_pixbuf_new_from_data(rgbdata, GDK_COLORSPACE_RGB,
			FALSE, 8, width, height, width * 3, pixbuf_destroy, NULL);
}

void mwin_refresh_prints(void)
{
	fp_dscv_prints_free(fp_dscv_prints);
	fp_dscv_prints = NULL;
	fp_dscv_prints = fp_discover_prints();
	if (!fp_dscv_prints) {
		mwin_devstatus_update("Error loading enrolled prints.");
		if (fpdev)
			fp_dev_close(fpdev);
		fpdev = NULL;

		gtk_label_set_text(GTK_LABEL(mwin_drvname_label), NULL);
		gtk_label_set_text(GTK_LABEL(mwin_imgcapa_label), NULL);
		for_each_tab_call_op(clear);
	} else {
		for_each_tab_call_op(refresh);
	}
}

