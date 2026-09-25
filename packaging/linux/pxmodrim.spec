Name:           pxmodrim
Version:        %{?version}%{!?version:0.1.0}
Release:        1%{?dist}
Summary:        Mod manager for RimWorld
License:        LGPL-3.0
URL:            https://github.com/PyXiion/PxModRim
BuildArch:      x86_64
AutoReqProv:    no

%description
PxModRim is a fast and modern mod manager for RimWorld with Steam Workshop
integration, dependency sorting, and local mod management support.

%install
mkdir -p %{buildroot}/opt/pxmodrim
mkdir -p %{buildroot}/usr/bin
mkdir -p %{buildroot}/usr/share/applications
mkdir -p %{buildroot}/usr/share/icons/hicolor/scalable/apps

cp -a %{project_root}/dist/entrypoint.dist/* %{buildroot}/opt/pxmodrim/
ln -s /opt/pxmodrim/PxModRim %{buildroot}/usr/bin/pxmodrim
install -m 644 %{project_root}/packaging/linux/pxmodrim.desktop %{buildroot}/usr/share/applications/pxmodrim.desktop
install -m 644 %{project_root}/src/pxmodrim/ui/assets/logo.svg %{buildroot}/usr/share/icons/hicolor/scalable/apps/pxmodrim.svg

%files
/opt/pxmodrim
/usr/bin/pxmodrim
/usr/share/applications/pxmodrim.desktop
/usr/share/icons/hicolor/scalable/apps/pxmodrim.svg
