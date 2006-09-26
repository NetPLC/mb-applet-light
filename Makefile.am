pngs = mblight-high.png mblight-mid.png mblight-low.png mblight-zero.png 

EXTRA_DIST = $(pngs) mb-applet-light.desktop TODO

INCLUDES = @APPLET_CFLAGS@

bin_PROGRAMS = mb-applet-light

mb_applet_light_SOURCES = light-applet.c			        

mb_applet_light_LDADD = @APPLET_LIBS@

desktopdir = $(datadir)/applications/
pixmapsdir = $(datadir)/pixmaps/

pixmaps_DATA = $(pngs)

desktop_DATA = mb-applet-light.desktop

