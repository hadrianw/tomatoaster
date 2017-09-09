#!/bin/sh
set -e

wget  -N "https://repo.voidlinux.eu/static/xbps-static-latest.x86_64-musl.tar.xz"
mkdir -p xbps/usr/share/xbps.d
tar xf xbps-static-latest.x86_64-musl.tar.xz -C xbps
cp 00-repository-main.conf xbps/usr/share/xbps.d

(cd void-packages && git pull origin master) ||
	git clone "https://github.com/voidlinux/void-packages.git"
(cd void-packages
export PATH="$PWD/../xbps/usr/bin:$PATH"
which xbps-install
./xbps-src binary-bootstrap x86_64-musl
git apply ../void-packages.patch
cp ../void-packages.conf etc/conf
./xbps-src pkg libGL
./xbps-src pkg gtk+3
./xbps-src pkg xbps-triggers
)

for i in config dev proc sys tmp usr/bin usr/lib var; do
	mkdir -p "rootfs/$i"
done
(cd rootfs;
ln -s usr/bin bin
ln -s usr/lib lib
)

gcc -O2 builder.c -o builder

./rootfs-xbps-install -A -S xbps-triggers base-system dash \
	xorg-minimal xorg-input-drivers xorg-video-drivers xorg-fonts \
	firefox
# busybox?
# libreoffice?
