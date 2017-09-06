#!/bin/sh
set -e

wget  "https://repo.voidlinux.eu/static/xbps-static-latest.x86_64-musl.tar.xz"
mkdir xbps
tar xf -C xbps xbps-static-latest.x86_64-musl.tar.xz
cp 00-repository-main.conf xbps/usr/share/xbps.d

git clone "https://github.com/voidlinux/void-packages.git"
(cd void-packages
export PATH="xbps/usr/bin:$PATH"
./xbps-src binary-bootstrap x86_64-musl
patch PATCH PATCH
cp ../void-packages.conf etc/conf
./xbps-src pkg libGL
./xbps-src pkg gtk+3
./xbps-src pkg xbps-triggers
)

cp -r rootfs{-next,}
mkdir -p rootfs/{config,dev,proc,sys,tmp,usr/{bin,lib},var}
(cd rootfs;
ln -s bin usr/bin
ln -s lib usr/lib
)

gcc -O2 builder.c -o builder

./builder ./rootfs-xbps-install base-system xbps-triggers dash \
	xorg-minimal xorg-input-drivers xorg-video-drivers xorg-fonts \
	firefox
# busybox?
# libreoffice?
