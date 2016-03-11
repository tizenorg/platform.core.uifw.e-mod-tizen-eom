%bcond_with x
%bcond_with wayland

Name: e-mod-tizen-eom
Version: 0.1.1
Release: 1
Summary: The Enlightenment eom Module for Tizen
URL: http://www.enlightenment.org
Group: Graphics & UI Framework/Other
Source0: %{name}-%{version}.tar.gz
License: BSD-2-Clause
BuildRequires: pkgconfig(enlightenment)
BuildRequires:  gettext
%if %{with x}
BuildRequires:  pkgconfig(x11)
%endif
%if %{with wayland}
BuildRequires:  pkgconfig(wayland-server)
%endif
BuildRequires:  pkgconfig(dlog)
%if "%{?profile}" == "common"
%else
BuildRequires:  e-tizen-data
%endif
%description
This package is a the Enlightenment eom Module for Tizen.

%global TZ_SYS_RO_SHARE  %{?TZ_SYS_RO_SHARE:%TZ_SYS_RO_SHARE}%{!?TZ_SYS_RO_SHARE:/usr/share}

%prep
%setup -q

%build

export GC_SECTIONS_FLAGS="-fdata-sections -ffunction-sections -Wl,--gc-sections"
export CFLAGS+=" -Wall -g -fPIC -rdynamic ${GC_SECTIONS_FLAGS}"
export LDFLAGS+=" -Wl,--hash-style=both -Wl,--as-needed -Wl,--rpath=/usr/lib"

%autogen
%if %{with wayland}
%configure --prefix=/usr --enable-wayland-only
%else
%configure --prefix=/usr
%endif

make

%install
rm -rf %{buildroot}

# for license notification
mkdir -p %{buildroot}%{TZ_SYS_RO_SHARE}/license
cp -a %{_builddir}/%{buildsubdir}/COPYING %{buildroot}%{TZ_SYS_RO_SHARE}/license/%{name}

# install
make install DESTDIR=%{buildroot}

# clear useless textual files
find  %{buildroot}%{_libdir}/enlightenment/modules/%{name} -name *.la | xargs rm

%files
%defattr(-,root,root,-)
%{_libdir}/enlightenment/modules/e-mod-tizen-eom
%{TZ_SYS_RO_SHARE}/license/%{name}
