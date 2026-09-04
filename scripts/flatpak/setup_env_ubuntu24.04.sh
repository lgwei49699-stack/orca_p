#! /bin/bash

sudo apt update
sudo apt install build-essential flatpak flatpak-builder gnome-software-plugin-flatpak -y
flatpak remote-add --if-not-exists flathub https://flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.gnome.Platform//47 org.gnome.Sdk//47


##
# In the source folder, run the following command to build 智小白切片软件.
# # First time build
# flatpak-builder --state-dir=.flatpak-builder --keep-build-dirs --user --force-clean build-dir scripts/flatpak/com.wisebeginner3d.ZhiXiaoBaiSlicer.yml

# # Subsequent builds (only rebuilding 智小白切片软件)
# flatpak-builder --state-dir=.flatpak-builder --keep-build-dirs --user build-dir scripts/flatpak/com.wisebeginner3d.ZhiXiaoBaiSlicer.yml --build-only=ZhiXiaoBaiSlicer
