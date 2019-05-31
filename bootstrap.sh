#!/bin/sh
set -e

wget  -N "https://alpha.de.repo.voidlinux.org/static/xbps-static-latest.x86_64-musl.tar.xz"
mkdir -p xbps/usr/share/xbps.d
tar xf xbps-static-latest.x86_64-musl.tar.xz -C xbps
cp 00-repository-main.conf xbps/usr/share/xbps.d

(cd void-packages && git pull origin master) ||
	git clone --depth 1 "https://github.com/void-linux/void-packages.git"
(cd void-packages
export PATH="$PWD/../xbps/usr/bin:$PATH"
which xbps-install
./xbps-src binary-bootstrap x86_64-musl
git apply ../void-packages.patch
cp ../void-packages.conf etc/conf
for p in base-files dbus dracut eudev gtk+3 libGL mdocml xbps-triggers; do
	./xbps-src pkg "$p"
done
)

mkdir -p rootfs
(cd rootfs;
mkdir -p dev proc rw sys tmp usr/bin usr/lib var/db/xbps/keys/
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
)
./build-chroot.ch libcap sh <<EOF
xbps-install -y perl
cd /build-chroot/libcap
make
EOF

git clone --depth 1 https://git.code.sf.net/p/squashfs/code squashfs-tools
(cd squashfs-tools
git apply ../squashfs-tools.patch
)
./build-chroot.ch squashfs-tools sh <<EOF
xbps-install -y zlib-devel
cd /build-chroot/squashfs-tools/squashfs-tools
make
EOF


gcc -O2 builder.c -o builder

./rootfs-xbps-install -y -S xbps-triggers base-system dracut dash \
	xorg-minimal xorg-input-drivers xorg-video-drivers xorg-fonts \
	firefox
# busybox?
# libreoffice?

cp fstab rootfs/etc
