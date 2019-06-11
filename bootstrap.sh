#!/bin/sh
set -e

wget  -N "https://alpha.de.repo.voidlinux.org/static/xbps-static-latest.x86_64-musl.tar.xz"
mkdir -p xbps/usr/share/xbps.d
tar xf xbps-static-latest.x86_64-musl.tar.xz -C xbps
cp 00-repository-main.conf xbps/usr/share/xbps.d

export PATH="$PWD/xbps/usr/bin:$PATH"

(cd void-packages && git pull origin master) ||
	git clone --depth 1 "https://github.com/void-linux/void-packages.git"
(cd void-packages
which xbps-install
./xbps-src binary-bootstrap x86_64-musl
cp ../void-packages.conf etc/conf

# From kernel 4.14 the v3 of security.capability xattr is available
# details: https://lwn.net/Articles/689169/
cp ../squashfs-tools.patch srcpkgs/squashfs-tools/patches

cp ../cpio-special-files-from-xattr.patch srcpkgs/cpio/patches/

for p in gtk+3 squashfs-tools cpio; do
	./xbps-src pkg "$p"
done
)

mkdir -p rootfs
(cd rootfs;
mkdir -p dev proc rw sys mnt tmp usr/bin usr/lib var/db/xbps/keys/
ln -s usr/bin bin
ln -s usr/lib lib
)
cp xbps/var/db/xbps/keys/* rootfs/var/db/xbps/keys/

gcc -O2 unshare-chroot.c -o unshare-chroot
./rootfs-xbps-install --yes --unpack-only --sync \
	base-system attr-progs squashfs-tools cpio \
	xorg-minimal xorg-input-drivers xorg-video-drivers xorg-fonts \
	firefox libreoffice
# FIXME: dracut: mknod failed
./rootfs-xbps-reconfigure --all

cp fstab rootfs/etc

# TODO: mksquashfs rootfs
