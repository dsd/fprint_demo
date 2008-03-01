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

#include <poll.h>
#include <stdlib.h>
#include <sys/time.h>

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

static void mwin_devstatus_update(char *status)
{
	gchar *msg = g_strdup_printf("<b>Status:</b> %s", status);
	gtk_label_set_markup(GTK_LABEL(mwin_devstatus_label), msg);
	g_free(msg);
}

static void dev_open_cb(struct fp_dev *dev, int status, void *user_data)
{
	struct fp_driver *drv;
	gchar *tmp;

	gtk_widget_destroy(GTK_WIDGET(user_data));
	fpdev = dev;

	fp_dscv_prints = fp_discover_prints();
	if (!fp_dscv_prints) {
		mwin_devstatus_update("Error loading enrolled prints.");
		/* FIXME error handling */
		return;
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
}

static void mwin_cb_dev_changed(GtkWidget *widget, gpointer user_data)
{
	GtkTreeIter iter;
	GtkWidget *dialog;
	struct fp_dscv_dev *ddev;
	int r;

	for_each_tab_call_op(clear);

	if (!gtk_combo_box_get_active_iter(GTK_COMBO_BOX(mwin_devcombo), &iter)) {
		mwin_devstatus_update("No devices found.");
		return;
	}

	gtk_tree_model_get(GTK_TREE_MODEL(mwin_devmodel), &iter, 1, &ddev, -1);

	fp_dscv_prints_free(fp_dscv_prints);
	fp_dscv_prints = NULL;
	fp_dev_close(fpdev);

	dialog = run_please_wait_dialog("Opening device...");
	r = fp_async_dev_open(ddev, dev_open_cb, dialog);
	if (r)
		mwin_devstatus_update("Could not open device.");
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
	gtk_window_set_title(GTK_WINDOW(mwin_window),
		"fprint project demo v" VERSION);
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

	for (i = 0; (ddev = discovered_devs[i]); i++) {
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

struct fdsource {
	GSource source;
	GSList *pollfds;
};

static gboolean source_prepare(GSource *source, gint *timeout)
{
	int r;
	struct timeval tv;

	r = fp_get_next_timeout(&tv);
	if (r == 0) {
		*timeout = -1;
		return FALSE;
	}

	if (!timerisset(&tv))
		return TRUE;

	*timeout = (tv.tv_sec * 1000) + (tv.tv_usec / 1000);
	return FALSE;
}

static gboolean source_check(GSource *source)
{
	struct fdsource *_fdsource = (struct fdsource *) source;
	GSList *elem = _fdsource->pollfds;
	struct timeval tv;
	int r;

	if (!elem)
		return FALSE;

	do {
		GPollFD *pollfd = elem->data;
		if (pollfd->revents)
			return TRUE;
	} while ((elem = g_slist_next(elem)));

	r = fp_get_next_timeout(&tv);
	if (r == 1 && !timerisset(&tv))
		return TRUE;

	return FALSE;
}

static gboolean source_dispatch(GSource *source, GSourceFunc callback,
	gpointer data)
{
	struct timeval zerotimeout = {
		.tv_sec = 0,
		.tv_usec = 0,
	};

	/* FIXME error handling */
	fp_handle_events_timeout(&zerotimeout);

	/* FIXME whats the return value used for? */
	return TRUE;
}

static void source_finalize(GSource *source)
{
	struct fdsource *_fdsource = (struct fdsource *) source;
	GSList *elem = _fdsource->pollfds;

	if (elem)
		do {
			GPollFD *pollfd = elem->data;
			g_source_remove_poll((GSource *) _fdsource, pollfd);
			g_slice_free(GPollFD, pollfd);
			_fdsource->pollfds = g_slist_delete_link(_fdsource->pollfds, elem);
		} while ((elem = g_slist_next(elem)));

	g_slist_free(_fdsource->pollfds);
}

static GSourceFuncs sourcefuncs = {
	.prepare = source_prepare,
	.check = source_check,
	.dispatch = source_dispatch,
	.finalize = source_finalize,
};

struct fdsource *fdsource = NULL;

static void pollfd_add(int fd, short events)
{
	GPollFD *pollfd = g_slice_new(GPollFD);
	pollfd->fd = fd;
	pollfd->events = 0;
	pollfd->revents = 0;
	if (events & POLLIN)
		pollfd->events |= G_IO_IN;
	if (events & POLLOUT)
		pollfd->events |= G_IO_OUT;

	fdsource->pollfds = g_slist_prepend(fdsource->pollfds, pollfd);
	g_source_add_poll((GSource *) fdsource, pollfd);
}

static void pollfd_added_cb(int fd, short events)
{
	g_message("now monitoring fd %d", fd);
	pollfd_add(fd, events);
}

static void pollfd_removed_cb(int fd)
{
	GSList *elem = fdsource->pollfds;
	g_message("no longer monitoring fd %d", fd);

	if (!elem) {
		g_warning("cannot remove from list as list is empty?");
		return;
	}

	do {
		GPollFD *pollfd = elem->data;
		if (pollfd->fd != fd)
			continue;

		g_source_remove_poll((GSource *) fdsource, pollfd);
		g_slice_free(GPollFD, pollfd);
		fdsource->pollfds = g_slist_delete_link(fdsource->pollfds, elem);
		return;
	} while ((elem = g_slist_next(elem)));
	
	g_error("couldn't find fd in list\n");
}

static int setup_pollfds(void)
{
	size_t numfds;
	size_t i;
	struct fp_pollfd *fpfds;
	GSource *gsource = g_source_new(&sourcefuncs, sizeof(struct fdsource));

	fdsource = (struct fdsource *) gsource;
	fdsource->pollfds = NULL;

	numfds = fp_get_pollfds(&fpfds);
	if (numfds < 0) {
		if (fpfds)
			free(fpfds);
		return (int) numfds;
	} else if (numfds > 0) {
		for (i = 0; i < numfds; i++) {
			struct fp_pollfd *fpfd = &fpfds[i];
			pollfd_add(fpfd->fd, fpfd->events);
		}
	}

	free(fpfds);
	fp_set_pollfd_notifiers(pollfd_added_cb, pollfd_removed_cb);
	g_source_attach(gsource, NULL);
	return 0;
}

int main(int argc, char **argv)
{
	int r;

	r = fp_init();
	if (r < 0)
		return r;

	gtk_init(&argc, &argv);
	gtk_window_set_default_icon_name("fprint_demo");

	r = setup_pollfds();
	if (r < 0)
		return r;

	mwin_create();
	mwin_populate_devs();
	mwin_select_first_dev();

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

/* simple dialog to display a "Please wait" message */
GtkWidget *run_please_wait_dialog(char *msg)
{
	GtkWidget *dlg = gtk_dialog_new();
	GtkWidget *label = gtk_label_new(msg);

	gtk_container_add(GTK_CONTAINER(GTK_DIALOG(dlg)->vbox), label);

	gtk_window_set_modal(GTK_WINDOW(dlg), TRUE);
	gtk_widget_show_all(dlg);
	gtk_widget_hide(GTK_DIALOG(dlg)->action_area);
	return dlg;
}

/* create and populate a dialog with a request to scan a finger plus a cancel
 * button. hook onto the response signal in order to listen for cancellation. */
GtkWidget *create_scan_finger_dialog(void)
{
	GtkWidget *dialog, *label, *progressbar;
	GtkWidget *vbox;

	dialog = gtk_dialog_new_with_buttons("Scan finger", GTK_WINDOW(mwin_window),
		GTK_DIALOG_MODAL | GTK_DIALOG_NO_SEPARATOR,
		GTK_STOCK_CANCEL, GTK_RESPONSE_CANCEL, NULL);
	gtk_window_set_deletable(GTK_WINDOW(dialog), FALSE);
	vbox = GTK_DIALOG(dialog)->vbox;

	label = gtk_label_new("Scan your finger now");
	gtk_box_pack_start_defaults(GTK_BOX(vbox), label);
	
	progressbar = gtk_progress_bar_new();
	gtk_box_pack_end_defaults(GTK_BOX(vbox), progressbar);

	g_object_set_data(G_OBJECT(dialog), "progressbar", progressbar);
	return dialog;
}

/* timer callback to pulse the progress bar */
static gboolean scan_finger_pulse_progress(gpointer data)
{
	GtkWidget *dialog = data;
	GtkWidget *progressbar = g_object_get_data(G_OBJECT(dialog), "progressbar");
	if (!progressbar)
		return FALSE;

	gtk_progress_bar_pulse(GTK_PROGRESS_BAR(progressbar));
	return TRUE;
}

/* show a scan finger dialog */
void run_scan_finger_dialog(GtkWidget *dialog)
{
	guint id;
	gtk_widget_show_all(dialog);
	id = gdk_threads_add_timeout(100, scan_finger_pulse_progress, dialog);
	g_object_set_data(G_OBJECT(dialog), "pulsesource", GUINT_TO_POINTER(id));
}

/* destroy a scan finger dialog */
void destroy_scan_finger_dialog(GtkWidget *dialog)
{
	gpointer source = g_object_get_data(G_OBJECT(dialog), "pulsesource");
	if (source)
		g_source_remove(GPOINTER_TO_UINT(source));
	gtk_widget_destroy(dialog);
}

