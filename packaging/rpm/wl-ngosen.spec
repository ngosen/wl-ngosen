%global pkgrel 1

Name:           wl-ngosen
Version:        0.1.0
Release:        %{pkgrel}%{?dist}
Summary:        Ngó Sen for Wayland, a Vietnamese input method without fcitx5 or IBus
License:        GPL-3.0-or-later AND LGPL-2.1-or-later AND MIT AND GPL-3.0-only AND HPND-sell-variant
URL:            https://github.com/ngosen/wl-ngosen
# Release tarballs carry the ngosen submodule, which GitHub's own archives leave out.
Source0:        %{url}/releases/download/%{name}-%{version}-%{pkgrel}/%{name}-%{version}.tar.gz

BuildRequires:  cmake
BuildRequires:  gcc-c++
BuildRequires:  cargo
BuildRequires:  rust >= 1.88
BuildRequires:  pkgconfig(gio-2.0) >= 2.72
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-server)
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  python3
BuildRequires:  gettext

# The settings window loads these through GObject introspection, which the automatic requires miss.
Requires:       python3-gobject
Requires:       gtk4
# Fedora's gtk4 does not pull in libGLESv2, and the window aborts without it in a bare install.
Requires:       libglvnd-gles
Requires:       libadwaita >= 1.5
Requires:       hicolor-icon-theme

%description
Ngó Sen for Wayland types Vietnamese on compositors that offer input-method-v2,
such as niri, Sway and Hyprland, without fcitx5 or IBus. A small daemon types
through the Ngó Sen core and shows a tray icon with the typing modes. It shares
its settings with the other Ngó Sen editions.

%prep
%setup -q

%build
# CI runs the session tests on every pull request.
%cmake -DNGOSEN_WL_SESSION_TESTS:BOOL=OFF
%cmake_build

%install
%cmake_install
%find_lang %{name}

%check
%ctest

%files -f %{name}.lang
%license LICENSE
%{_bindir}/wl-ngosen
%{_bindir}/wl-ngosen-settings
%{_datadir}/wl-ngosen/
%{_datadir}/applications/io.github.ngosen.WlNgoSen.desktop
%{_datadir}/metainfo/io.github.ngosen.WlNgoSen.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.ngosen.WlNgoSen.svg
%{_datadir}/icons/hicolor/scalable/status/wl-ngosen.svg
%{_datadir}/icons/hicolor/scalable/status/wl-ngosen-off.svg

%changelog
* Wed Nov 11 2026 Nguyen Phi <nguyenphidt@gmail.com> - 0.1.0-1
- First release.
