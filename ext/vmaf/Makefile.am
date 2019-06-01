plugin_LTLIBRARIES = libgstvmaf.la

libgstvmaf_la_SOURCES = \
	vmaf.c

libgstvmaf_la_CFLAGS =  \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)

libgstvmaf_la_CFLAGS += $(LIBVMAF_CFLAGS)

libgstvmaf_la_LIBADD =  \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS)

libgstvmaf_la_LIBADD += $(LIBVMAF_LIBS)

libgstvmaf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	vmaf.h

