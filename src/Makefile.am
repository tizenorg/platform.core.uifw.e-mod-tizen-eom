MAINTAINERCLEANFILES = Makefile.in
MODULE = e-mod-tizen-eom

# data files for the module
filesdir = $(libdir)/enlightenment/modules/$(MODULE)

pkgdir                 = $(libdir)/enlightenment/modules/$(MODULE)/$(MODULE_ARCH)
pkg_LTLIBRARIES        = module.la

if WAYLAND_ONLY
module_la_SOURCES      = e_mod_main.c eom-protocol.c
module_la_LIBADD       =
module_la_CFLAGS       = @WAYLAND_CFLAGS@ @ENLIGHTENMENT_CFLAGS@ -DHAVE_WAYLAND_ONLY
module_la_LDFLAGS      = -module -avoid-version @WAYLAND_LIBS@ @ENLIGHTENMENT_LIBS@
module_la_DEPENDENCIES = $(top_builddir)/config.h

BUILT_SOURCES = eom-protocol.c \
                eom-client-protocol.h \
                eom-server-protocol.h

CLEANFILES = $(BUILT_SOURCES)

%-protocol.c : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) code < $< > $@

%-server-protocol.h : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) server-header < $< > $@

%-client-protocol.h : $(top_srcdir)/protocol/%.xml
	$(AM_V_GEN)$(MKDIR_P) $(dir $@) && $(wayland_scanner) client-header < $< > $@
else
module_la_SOURCES      = e_mod_main_x11.c 
module_la_LIBADD       =
module_la_CFLAGS       = @ENLIGHTENMENT_CFLAGS@
module_la_LDFLAGS      = -module -avoid-version @ENLIGHTENMENT_LIBS@
endif
