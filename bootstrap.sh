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

patch -p1 -d void-packages < void-packages-xbps-triggers-pycompile-redate.patch
install -D ../shared-mime-info-*.patch -t srcpkgs/shared-mime-info/patches
cp ../squashfs-*.patch srcpkgs/squashfs-tools/patches
cp ../cpio-*.patch srcpkgs/cpio/patches/

for p in xbps-triggers shared-mime-info gtk+3 libzbar gst-plugins-good1 squashfs-tools cpio; do
	./xbps-src pkg "$p"
done
)

mkdir -p rootfs
(cd rootfs;
mkdir -p dev etc proc sys mnt tmp usr/bin usr/lib var/db/xbps/keys/ rw
ln -s /run/resolv.conf etc/resolv.conf
ln -s usr/bin bin
ln -s usr/lib lib
)
cp xbps/var/db/xbps/keys/* rootfs/var/db/xbps/keys/

gcc -O2 unshare-chroot.c -o unshare-chroot
./rootfs-xbps-install --yes --unpack-only --sync \
	base-system attr-progs squashfs-tools cpio \
	xorg-minimal xorg-input-drivers xorg-video-drivers xorg-fonts \
	slim xfce4 \
	firefox libreoffice \
	xfburn evince mate-calc \
	catfish thunar-archive-plugin engrampa handbrake \
	network-manager-applet gnome-disk-utility \
	cups cups-filters system-config-printer system-config-printer-udev

install -D dracut-reproducible.conf -t rootfs/etc/dracut.conf.d/
./rootfs-xbps-reconfigure --all

cp fstab rootfs/etc
patch -p1 -d rootfs < root-read-only.patch

# enable services
for i in acpid cgmanager consolekit cups-browsed cupsd dbus dhclient dhcpcd dhcpcd-eth0 NetworkManager polkitd slim wpa_supplicant; do
	test -e "rootfs/etc/sv/$i"
	ln -s "/etc/sv/$i" rootfs/etc/runit/runsvdir/current
done

for i in rootfs/boot/vmlinuz-*; do
	touch -c -r "$i" "${i/vmlinuz/initramfs}.img"
done

# add a user
root="$PWD/rootfs"
PATH="/mnt/xbps/usr/bin:/usr/bin" \
./unshare-chroot -d /proc "$root/proc" \
        -d /sys "$root/sys" \
        -d /dev "$root/dev" \
        -d "$PWD" "$root/mnt" \
        -m "$root" \
        -- /usr/bin/sh <<EOF
/usr/bin/useradd -m tmtstr -G wheel
{ echo tmtstr; echo tmtstr; } | /usr/bin/passwd tmtstr
EOF

# prepare rwfs
mkdir -p rwfs/etc
for i in home root var; do
	mv "rootfs/$i/" rwfs/
	mkdir -p "rootfs/$i"
done

for i in rootfs rootfs/*; do
	touch -c -d "2001-01-01 00:00:01" "$i"
done


# prepare squashfs image
root="$PWD/void-packages/masterdir"
PATH="/mnt/xbps/usr/bin:/usr/bin" \
./unshare-chroot -d /proc "$root/proc" \
        -d /sys "$root/sys" \
        -d /dev "$root/dev" \
        -d "$PWD/rootfs" "$root/mnt" \
        -m "$root" \
        -- /usr/bin/sh <<EOF
set -e
xbps-install --dry-run squashfs-tools &&
xbps-install --yes squashfs-tools
mksquashfs /mnt /builddir/tmtstr.sqfs -all-root
EOF
mv "$root/builddir/tmtstr.sqfs" .
