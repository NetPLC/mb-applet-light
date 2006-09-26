/* mb-light-applet - A tiny back-light control tray app

   GTk bits based on GPE's minilite.c bu p.blundell

   Copyright 2004 Matthew Allum

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
*/


#include <libmb/mb.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/ioctl.h>
#include <sys/ioctl.h>

#include <gtk/gtk.h>
#include <gdk/gdkx.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define IMG_EXT "png"

/*
#else
#define IMG_EXT "xpm"
#endif
*/

#define POPUP_WIDTH  33
#define POPUP_HEIGHT 130
#define POPUP_PTR_SZ 10

#define BRIGHTNESS_ZERO 0
#define BRIGHTNESS_LOW  1
#define BRIGHTNESS_MID  2
#define BRIGHTNESS_HIGH 3

#define BRIGHTNESS_ZERO_IMG "mblight-zero."  IMG_EXT
#define BRIGHTNESS_LOW_IMG  "mblight-low."   IMG_EXT
#define BRIGHTNESS_MID_IMG  "mblight-mid."   IMG_EXT
#define BRIGHTNESS_HIGH_IMG "mblight-high."  IMG_EXT

#define SLIDER_HEIGHT 100

#define SYSCLASS 	"/sys/class/backlight/"
static char *SYS_BRIGHTNESS = NULL;
static char *SYS_MAXBRIGHTNESS = NULL;
static char *SYS_POWER = NULL;

static char *ImgLookup[64] = {
  BRIGHTNESS_ZERO_IMG,
  BRIGHTNESS_LOW_IMG,
  BRIGHTNESS_MID_IMG,
  BRIGHTNESS_HIGH_IMG
};

static char *ThemeName = NULL;
static MBPixbuf *pb;
static MBPixbufImage *Imgs[4] = { 0,0,0,0 }, *ImgsScaled[4] = { 0,0,0,0 };

static Window WinPopup;
static int    mixerfd;
static Bool   PopupIsMapped = False;

static int    PopupYOffset  = 5;
static Bool   SliderActive  = False;

static gint Brightness;
static gint MaxBrightness;

GtkWidget *slider_window;
GtkWidget *slider;


static gboolean
setup_sysclass(void)
{
	GDir *dir;
	gchar *dirname = NULL;
	gboolean retval = FALSE;
	
	if (!(dir = g_dir_open (SYSCLASS, 0, NULL))) {
		g_warning ("Unable to open %s", SYSCLASS);
		return FALSE;
	}
	
	if ((dirname = g_dir_read_name (dir))) {
		SYS_BRIGHTNESS = g_strconcat (SYSCLASS, dirname,
			G_DIR_SEPARATOR_S, "brightness", NULL);
		SYS_MAXBRIGHTNESS = g_strconcat (SYSCLASS, dirname,
			G_DIR_SEPARATOR_S, "max_brightness", NULL);
		SYS_POWER = g_strconcat (SYSCLASS, dirname,
			G_DIR_SEPARATOR_S, "power", NULL);
		retval = TRUE;
	}
	
	g_dir_close (dir);
	
	return retval;
}

static void
sysclass_set_level (gint level)
{
	FILE *f_light;

	f_light = fopen (SYS_BRIGHTNESS, "w");
	if (f_light != NULL) {
		fprintf (f_light,"%d\n", level);
		fclose (f_light);
	} else
		return -1;

	f_light = fopen (SYS_POWER, "w");
	if (f_light != NULL) {
		fprintf (f_light,"%d\n",  level ? SYS_STATE_ON : SYS_STATE_OFF);
		fclose (f_light);
	}
}

static gint
sysclass_get_level ()
{
	FILE *f_light;
	gfloat level;

	f_light = fopen (SYS_BRIGHTNESS, "r");
	if (f_light != NULL) {
		fscanf (f_light,"%f", &level);
		fclose (f_light);
		return (gint)level;
	}
	return -1;
}

static gint
sysclass_get_maxlevel ()
{
	FILE *f_max;
	float maxlevel, factor;

	f_max = fopen (SYS_MAXBRIGHTNESS, "r");
	if (f_max != NULL) {
		fscanf (f_max,"%f", &maxlevel);
		fclose (f_max);
		return (gint)maxlevel;
	}
	return -1;
}

/* -- tray app callbacks -- */

void
paint_callback (MBTrayApp *app, Drawable drw )
{
  int img_index = 0;
  MBPixbufImage *img_backing = NULL;
  
  img_backing = mb_tray_app_get_background (app, pb);

  if (!err_str)
    {
      img_index = (Brightness*4)/MaxBrightness;

      if (img_index > 3) img_index = 3;
      if (img_index < 0) img_index = 0;
    }

  /* CurrentLightLevel */
  mb_pixbuf_img_composite(pb, img_backing, 
			  ImgsScaled[img_index], 
			  0, 0);

  mb_pixbuf_img_render_to_drawable(pb, img_backing, drw, 0, 0);

  mb_pixbuf_img_free( pb, img_backing );
}

void
resize_callback (MBTrayApp *app, int w, int h )
{
  int i;

  for (i=0; i<4; i++)
    {
      if (ImgsScaled[i] != NULL) mb_pixbuf_img_free(pb, ImgsScaled[i]);
      ImgsScaled[i] = mb_pixbuf_img_scale(pb, Imgs[i], w, h);
    }
}

void
load_icons(void)
{
 int   i;
 char *icon_path;

  for (i=0; i<4; i++)
    {
      if (Imgs[i] != NULL) mb_pixbuf_img_free(pb, Imgs[i]);
      icon_path = mb_dot_desktop_icon_get_full_path (ThemeName, 
						     32, 
						     ImgLookup[i]);
      
      if (icon_path == NULL 
	  || !(Imgs[i] = mb_pixbuf_img_new_from_file(pb, icon_path)))
	{
	  g_error ("Failed to load icon (%s)", icon_path ? icon_path : "null" );
	}

      free(icon_path);
    }
}

void 
theme_callback (MBTrayApp *app, char *theme_name)
{
  if (!theme_name) return;
  if (ThemeName) free(ThemeName);
  ThemeName = strdup(theme_name);
  load_icons(); 	
  resize_callback (app, mb_tray_app_width(app), mb_tray_app_width(app) );
}

gboolean
popup_close (GtkWidget *w, GtkWidget *list_view)
{
  gtk_widget_hide (slider_window);
  PopupIsMapped = False;
      
}

static void
slider_clicked (GtkWidget *w, GdkEventButton *ev)
{
  gdk_pointer_ungrab (ev->time);

  gtk_widget_hide (slider_window);
}


static void
button_callback (MBTrayApp *app, int cx, int cy, Bool is_released)
{
  int level;
  int x, y, win_w, win_h;

  if (!is_released) return;

  mb_tray_app_get_absolute_coords (app, &x, &y);

  gdk_window_get_geometry (slider_window->window, NULL, NULL,
			   &win_w, &win_h, NULL);

  if (mb_tray_app_tray_is_vertical (app))
    {
      if (x > (DisplayWidth(mb_tray_app_xdisplay(app), 
			    mb_tray_app_xscreen(app)) /2) )
	x -= ( mb_tray_app_width(app) + win_w );
      else
	x += mb_tray_app_width(app);
      
      /*
	if ( (y - win_h) < 0 ) 
	y = 0;
	else if ( (y + POPUP_HEIGHT) > root_attr.height )
	y = root_attr.height - POPUP_HEIGHT;
      */
    }
  else
    {
      if (y < mb_tray_app_height(app))
	{ y = mb_tray_app_height(app); }
      else
	{ 
	  y = DisplayHeight(mb_tray_app_xdisplay(app), 
			    mb_tray_app_xscreen(app)) 
	    - win_h - mb_tray_app_height(app) - 4;
	}

      x -= (mb_tray_app_width(app)/2);
      
      if ((x + win_w) > DisplayWidth(mb_tray_app_xdisplay(app), 
				     mb_tray_app_xscreen(app)))
	x = DisplayWidth(mb_tray_app_xdisplay(app), 
			 mb_tray_app_xscreen(app)) - win_w - 2;
    }

  
  
  gtk_widget_set_uposition (GTK_WIDGET (slider_window), x, y);
  
  Brightness = sysclass_get_level ();
  MaxBrightness = sysclass_get_maxlevel ();

  gtk_adjustment_set_value(gtk_range_get_adjustment(GTK_RANGE(slider)), 
			   Brightness);

  gtk_widget_show_all (slider_window);

  gdk_pointer_grab (slider_window->window, TRUE, GDK_BUTTON_PRESS_MASK, NULL, NULL, CurrentTime);

  PopupIsMapped = True;
}


void
popup_light_changed_cb (GtkAdjustment *adj, gpointer data)
{
  int value;
  MBTrayApp *app = (MBTrayApp *)data;
  
  Brightness = gtk_adjustment_get_value (adj);

  sysclass_set_level (app, value);

  mb_tray_app_repaint(app);
}

void
popup_init(MBTrayApp *app)
{
  GtkWidget     *vbox;
  GtkWidget     *hbox;
  GtkWidget     *label;
  GtkWidget     *button_ok;
  GtkAdjustment *adj;

  vbox = gtk_vbox_new (FALSE, 2);

  /* hbox for text */
  hbox = gtk_hbox_new (FALSE, 2);

  label = gtk_label_new(NULL);
  gtk_label_set_markup (GTK_LABEL(label), "<b>Low</b>");

  gtk_box_pack_start (GTK_BOX (hbox), label, TRUE, TRUE, 0);

  label = gtk_label_new(NULL);
  gtk_label_set_markup (GTK_LABEL(label), "<b>High</b>");

  gtk_box_pack_end (GTK_BOX (hbox), label, TRUE, TRUE, 0);

  gtk_box_pack_start (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  slider_window = gtk_window_new (GTK_WINDOW_POPUP);

  slider = gtk_vscale_new_with_range (0, 100, 1);

  gtk_scale_set_draw_value (GTK_SCALE (slider), FALSE);
  gtk_widget_set_usize (slider_window, -1, SLIDER_HEIGHT);
  gtk_range_set_inverted (GTK_RANGE (slider), TRUE);

  adj = gtk_range_get_adjustment (GTK_RANGE (slider)); 

  g_signal_connect (G_OBJECT (adj), "value-changed", 
		    G_CALLBACK (popup_light_changed_cb), (gpointer)app);
  
  gtk_container_add (GTK_CONTAINER (slider_window), slider);

  /*
  g_signal_connect (G_OBJECT (window), "button-press-event", G_CALLBACK (clicked), NULL);
  */

  g_signal_connect (G_OBJECT (slider_window), "button-press-event", G_CALLBACK (slider_clicked), NULL);

  gtk_widget_add_events (slider_window, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);


  /*
  gtk_window_set_decorated (GTK_WINDOW(slider_window), FALSE);

  gtk_window_set_type_hint (GTK_WINDOW(slider_window), 
			    GDK_WINDOW_TYPE_HINT_DIALOG);

  gtk_widget_set_usize (slider_window, SLIDER_WIN_WIDTH, -1 ); 

  slider = gtk_hscale_new_with_range (0, 100, 1);

  gtk_scale_set_draw_value (GTK_SCALE (slider), FALSE);

  adj = gtk_range_get_adjustment (GTK_RANGE (slider)); 

  g_signal_connect (G_OBJECT (adj), "value-changed", 
		    G_CALLBACK (popup_light_changed_cb), (gpointer)app);
  
  gtk_box_pack_start (GTK_BOX (vbox), slider, TRUE, TRUE, 0);

  hbox = gtk_hbox_new (FALSE, 2);

  button_mute = gtk_check_button_new_with_label ( "Mute" );  
  */

  /*
  gtk_box_pack_start (GTK_BOX (hbox), button_mute, TRUE, TRUE, 0);
  */

  button_ok = gtk_button_new_from_stock(GTK_STOCK_OK);

  gtk_box_pack_end (GTK_BOX (hbox), button_ok, FALSE, FALSE, 0);

  /* XXX mute callback */

  gtk_box_pack_end (GTK_BOX (vbox), hbox, TRUE, TRUE, 0);

  gtk_container_set_border_width (GTK_CONTAINER (slider_window), 6);

  gtk_container_add (GTK_CONTAINER (slider_window), vbox);

  g_signal_connect (G_OBJECT (button_ok), "clicked", 
		    G_CALLBACK (popup_close), NULL);

  gtk_widget_realize (slider_window);
}



GdkFilterReturn
event_filter (GdkXEvent *xev, GdkEvent *gev, gpointer data)
{
  XEvent    *ev  = (XEvent *)xev;
  MBTrayApp *app = (MBTrayApp*)data;

  Display *dpy = ev->xany.display;

  mb_tray_handle_xevent (app, ev); 

  return GDK_FILTER_CONTINUE;
}

static gboolean
brightness_timeout_cb (MBTrayApp *app)
{
  /* check light data */
  int this_bri = sysclass_get_level ();

  if (this_bri != -1 && this_bri != Brightness)
    {
      Brightness = this_bri;

      gtk_adjustment_set_value(gtk_range_get_adjustment(GTK_RANGE(slider)), 
			       Brightness);

      mb_tray_app_repaint(app);
    }

  return TRUE;
}

int
main( int argc, char *argv[])
{
  char *mixer_dev = "/dev/mixer";
  int devmask = 0;

  MBTrayApp *app = NULL;

  gtk_init (&argc, &argv);

#if ENABLE_NLS
  setlocale (LC_ALL, "");
  bindtextdomain (PACKAGE, DATADIR "/locale");
  bind_textdomain_codeset (PACKAGE, "UTF-8"); 
  textdomain (PACKAGE);
#endif

  if (!setup_sysclass ())
    g_error ("Failed to open a backlight");

  app = mb_tray_app_new_with_display ( "Brightness Control",
				       resize_callback,
				       paint_callback,
				       &argc,
				       &argv,
				       GDK_DISPLAY ());  
  
  if (!app) exit(0); 
  
  pb = mb_pixbuf_new(mb_tray_app_xdisplay(app), 
		     mb_tray_app_xscreen(app));
  
  mb_tray_app_set_theme_change_callback (app, theme_callback );

  mb_tray_app_set_button_callback (app, button_callback );
  
  gtk_timeout_add (500,
		   (GSourceFunc) brightness_timeout_cb,
		   app);
  
  load_icons();
  
  mb_tray_app_set_icon(app, pb, Imgs[3]);

  popup_init(app);

  mb_tray_app_main_init (app);
  
  gdk_window_add_filter (NULL, event_filter, (gpointer)app );
  
  gtk_main ();
  
  return 1;
}