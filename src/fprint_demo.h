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

#ifndef __FPRINT_DEMO_H__
#define __FPRINT_DEMO_H__

#include <gtk/gtk.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <libfprint/fprint.h>

/* main.c */
extern struct fp_dev *fpdev;
extern struct fp_dscv_print **fp_dscv_prints;
extern GtkWidget *mwin_window;
const char *fingerstr(enum fp_finger finger);
void pixbuf_destroy(guchar *pixels, gpointer data);
unsigned char *img_to_rgbdata(struct fp_img *img);
GdkPixbuf *img_to_pixbuf(struct fp_img *img);
void mwin_refresh_prints(void);

/* tabs */
struct fpd_tab {
	const char *name;
	GtkWidget *(*create)(void);
	void (*activate_dev)(void);
	void (*clear)(void);
	void (*refresh)(void);
};

extern struct fpd_tab enroll_tab;
extern struct fpd_tab verify_tab;
extern struct fpd_tab identify_tab;
extern struct fpd_tab img_tab;

#endif
