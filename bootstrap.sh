#!/bin/sh
set -e

. $PWD/functions.sh

wget  -N "https://alpha.de.repo.voidlinux.org/static/xbps-static-latest.x86_64-musl.tar.xz"
mkdir -p xbps/usr/share/xbps.d
tar xf xbps-static-latest.x86_64-musl.tar.xz -C xbps
cp configs/00-repository-main.conf xbps/usr/share/xbps.d

(cd void-packages
git checkout master
git pull origin master) ||
	git clone --depth 1 "https://github.com/void-linux/void-packages.git"

cp configs/void-packages.conf void-packages/etc/conf
for d in void-packages; do
	for i in "patches/$d/"*; do
		patch -p1 -d "$d" < "$i"
	done
done
(cd patches/void-packages-srcpkgs/
for i in *; do
	(cd "../../void-packages/srcpkgs/$i/"
		mkdir -p patches)
	cp "$i"/* "../../void-packages/srcpkgs/$i/patches"
done)

(export PATH="$PWD/xbps/usr/bin:$PATH"
cd void-packages
which xbps-install
./xbps-src binary-bootstrap x86_64-musl

for p in xbps-triggers shared-mime-info gtk+3 libzbar gst-plugins-good1 squashfs-tools cpio; do
	./xbps-src pkg "$p"
done
)

mv rwfs/var/db/xbps/http___alpha_de_repo_voidlinux_org_current_musl/x86_64-musl-repodata . || true
rm -rf rootfs rwfs

mkdir -p rootfs
(cd rootfs;
mkdir -p dev etc proc sys mnt tmp usr/bin usr/lib var rw
ln -s /run/resolv.conf etc/resolv.conf
ln -s usr/bin bin
ln -s usr/lib lib
)
install -D configs/dracut-reproducible.conf -t rootfs/etc/dracut.conf.d/

mkdir -p rwfs/etc rwfs/home rwfs/var/db/xbps/keys/ rwfs/root
cp xbps/var/db/xbps/keys/* rwfs/var/db/xbps/keys/

mkdir -p rwfs/var/db/xbps/http___alpha_de_repo_voidlinux_org_current_musl/
mv x86_64-musl-repodata rwfs/var/db/xbps/http___alpha_de_repo_voidlinux_org_current_musl/ || true

gcc -O2 unshare-chroot.c -o unshare-chroot
./rootfs-xbps-install --yes --unpack-only --sync \
	base-system attr-progs squashfs-tools cpio \
	xorg-minimal xorg-input-drivers xorg-video-drivers xorg-fonts \
	nodm xfce4 \
	firefox libreoffice \
	xfburn evince mate-calc \
	catfish thunar-archive-plugin engrampa handbrake \
	network-manager-applet gnome-disk-utility \
	cups cups-filters system-config-printer system-config-printer-udev
./rootfs-xbps-reconfigure --all

cp configs/fstab rootfs/etc
for d in rootfs; do
	for i in "patches/$d/"*; do
		patch -p1 -d "$d" < "$i"
	done
done

# enable services
for i in acpid cgmanager consolekit cups-browsed cupsd dbus dhclient dhcpcd dhcpcd-eth0 NetworkManager nodm polkitd wpa_supplicant; do
	test -e "rootfs/etc/sv/$i"
	ln -s "/etc/sv/$i" rootfs/etc/runit/runsvdir/current
	touch -ch -r "rootfs/etc/sv/$i" "rootfs/etc/runit/runsvdir/current/$i"
done

cp -a configs/nodm.conf rootfs/etc/sv/nodm/conf

for i in rootfs/boot/vmlinuz-*; do
	version=${i##*-}
	touch -ch -r "$i" "rootfs/boot/initramfs-$version.img"
	find "rootfs/usr/lib/modules/$version/" -type d -exec \
		touch -ch -r "$i" '{}' \+
done

touch -ch -r $(newest_in_dir rootfs/usr/lib/udev/hwdb.d/) rootfs/etc/udev/hwdb.bin

# fix fonts.* files mtimes
find rootfs/usr/share/fonts/ -type f -name "fonts.*" -exec sh -c '
set -e
. $PWD/functions.sh
newest=$(newest_in_dir $(dirname "$1") "" "*/fonts.*")
test -n "$newest" &&
touch -ch -r "$newest" "$1"' -- '{}' \;

# fix alternatives' symlinks mtimes
./rootfs-xbps-alternatives -l | awk '
parse && !/^  -/ {
	parse = 0
}

parse {
	if(split($2, lnk, /:/) != 2) {
		exit 1
	}
	system("set -e;. $PWD/functions.sh; link_mtime_from_target \"rootfs$(dirname \"" lnk[2] "\")/" lnk[1]"\"")
}

/^ - / && $NF == "(current)" {
	parse = 1
}'

# fix man page dirs mtimes
for i in rootfs/usr/share/man/*/; do
	touch -ch -r $(newest_in_dir "$i") "$i"
done
# fix mandoc.db mtime
touch -ch -r $(newest_in_dir rootfs/usr/share/man/ "" "mandoc.db") rootfs/usr/share/man/mandoc.db

# add a user
root="$PWD/rootfs"
PATH="/mnt/xbps/usr/bin:/usr/bin" \
./unshare-chroot -d /proc "$root/proc" \
        -d /sys "$root/sys" \
        -d /dev "$root/dev" \
        -d "$PWD" "$root/mnt" \
        -d "$PWD/rwfs/var" "$root/var" \
        -m "$root" \
        -- /usr/bin/sh <<EOF
/usr/bin/useradd -m tmtstr -G audio,input,lpadmin,optical,scanner,storage,users,video,wheel
{ echo tmtstr; echo tmtstr; } | /usr/bin/passwd tmtstr
EOF

# prepare rwfs
for i in home root; do
	mv "rootfs/$i/" rwfs/
	mkdir -p "rootfs/$i"
done

for i in rootfs rootfs/*; do
	touch -ch -d "2001-01-01 00:00:01" "$i"
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
