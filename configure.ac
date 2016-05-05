dnl Process this file with autoconf to produce a configure script.

# get rid of that stupid cache mechanism
rm -f config.cache

AC_INIT(e-mod-tizen-eom, 0.1, sc1.lim@samsung.com)
AC_PREREQ(2.52)
AC_CONFIG_SRCDIR(configure.ac)
AC_CANONICAL_BUILD
AC_CANONICAL_HOST
AC_ISC_POSIX

AM_INIT_AUTOMAKE(1.8)
AM_CONFIG_HEADER(config.h)
m4_ifdef([AM_SILENT_RULES], [AM_SILENT_RULES([yes])])

AC_PROG_CC
AM_PROG_CC_STDC
AM_PROG_CC_C_O
AC_HEADER_STDC
AC_C_CONST
AC_C___ATTRIBUTE__

define([AC_LIBTOOL_LANG_CXX_CONFIG], [:])dnl
define([AC_LIBTOOL_LANG_F77_CONFIG], [:])dnl
AC_PROG_LIBTOOL

#AM_GNU_GETTEXT([external])
#AM_GNU_GETTEXT_VERSION(0.14)

AC_PATH_PROG([wayland_scanner], [wayland-scanner])
if test x$wayland_scanner = x; then
   AC_MSG_ERROR([wayland-scanner is needed to compile])
fi

PKG_CHECK_MODULES(WAYLAND_SCANNER, wayland-scanner)
PKG_CHECK_MODULES(WAYLAND, [wayland-server])
PKG_CHECK_MODULES(ENLIGHTENMENT, [enlightenment, elementary, eom])

dnl ========================================================================
# checks for wayland only argument
dnl ========================================================================
have_wayland_only=no
AC_ARG_ENABLE([wayland-only],
  AS_HELP_STRING([--enable-wayland-only],[enable wayland-only version of enlightenment @<:@default=disabled@:>@]),
  [have_wayland_only=$enableval],
  [have_wayland_only=no])
AC_MSG_CHECKING([whether wayland-only version is enabled])
AM_CONDITIONAL(WAYLAND_ONLY, test x$have_wayland_only = xyes)
if test "x${have_wayland_only}" != "xno"; then
  AC_DEFINE_UNQUOTED([HAVE_WAYLAND_ONLY],[1],[enable wayland-only version of enlightenment])
fi

dnl ========================================================================
# checks for wayland only argument
dnl ========================================================================
if test "x${have_wayland_only}" = "xyes"; then
  PKG_CHECK_MODULES(WAYLAND, [wayland-server])
fi

AC_SUBST(ENLIGHTENMENT_CFLAGS)
AC_SUBST(ENLIGHTENMENT_LIBS)

AC_CHECK_LIB(dl, dlopen, dlopen_libs=-ldl)
AC_SUBST(dlopen_libs)

release=$(pkg-config --variable=release enlightenment)
MODULE_ARCH="$host_os-$host_cpu-$release"
AC_SUBST(MODULE_ARCH)
AC_DEFINE_UNQUOTED(MODULE_ARCH, "$MODULE_ARCH", "Module architecture")

datadir=$(pkg-config --variable=modules enlightenment)/${PACKAGE}

AC_CONFIG_FILES([Makefile
		 src/Makefile])
AC_OUTPUT
