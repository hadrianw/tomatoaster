#!/bin/sh
set -e

wget  -N "https://repo.voidlinux.eu/static/xbps-static-latest.x86_64-musl.tar.xz"
mkdir -p xbps/usr/share/xbps.d
tar xf xbps-static-latest.x86_64-musl.tar.xz -C xbps
cp 00-repository-main.conf xbps/usr/share/xbps.d

(cd void-packages && git pull origin master) ||
	git clone --depth 1 "https://github.com/voidlinux/void-packages.git"
(cd void-packages
export PATH="$PWD/../xbps/usr/bin:$PATH"
which xbps-install
./xbps-src binary-bootstrap x86_64-musl
git apply ../void-packages.patch
cp ../void-packages.conf etc/conf
for p in dbus dracut eudev gtk+3 libGL mdocml xbps-triggers; do
	./xbps-src pkg "$p"
done
)

mkdir -p rootfs
(cd rootfs;
mkdir -p config/useradd config/chgrp config/chown \
	dev proc sys tmp usr/bin usr/lib var/db/xbps/keys/
ln -s usr/bin bin
ln -s usr/lib lib
)
cp xbps/var/db/xbps/keys/* rootfs/var/db/xbps/keys/

# When v3 of security.capability xattr will be live in kernel near me
# patching of setcap and mksquashfs will no longer be necessary
# details: https://lwn.net/Articles/689169/
git clone --depth 1 git://git.kernel.org/pub/scm/linux/kernel/git/morgan/libcap.git
(cd libcap
git apply ../libcap.patch
make
)
git clone --depth 1 https://git.code.sf.net/p/squashfs/code squashfs-tools
(cd squashfs-tools
git apply ../squashfs-tools.patch
cd squashfs-tools
make
)

gcc -O2 builder.c -o builder

./rootfs-xbps-install -y -S xbps-triggers base-system dracut dash \
	xorg-minimal xorg-input-drivers xorg-video-drivers xorg-fonts \
	firefox
# busybox?
# libreoffice?
