#!/bin/bash
set -e

# TODO: rootfs tmp?
# TODO: relative paths -> prefix $ROOT/

# Prints every argument on a new line, useful for simple indentation
print() {
	for i in "$@"; do
		echo $i
	done
}

nproc() {
	getconf _NPROCESSORS_ONLN
}

root-dir() {
	local wdir="$PWD"
	if [[ "$PWD" = "/" ]]; then
		wdir=""
	fi

	case "$0" in
	  /*) local scriptdir="$0";;
	  *) local scriptdir="$wdir/${0#./}";;
	esac

	readlink -f "${scriptdir%/*}"
}

xbps-alternatives() {
	./unshare-chroot \
		-d "$RWFS/var" "$ROOTFS/var" \
		-- $ROOT/xbps/usr/bin/xbps-alternatives \
		-C "$ROOT/xbps/usr/share/xbps.d" \
		-r "$ROOTFS" \
		"$@"
}

xbps-create() {
	xbps/usr/bin/xbps-create "@"
}

xbps-fbulk() {
	xbps/usr/bin/xbps-fbulk "@"
}

xbps-rindex() {
	xbps/usr/bin/xbps-rindex "@"
}

xbps-uchroot() {
	xbps/usr/bin/xbps-uchroot "@"
}

xbps-uunshare() {
	xbps/usr/bin/xbps-uunshare "@"
}


xbps-checkvers() {
	xbps/usr/bin/xbps-checkvers \
		-C "$ROOT/xbps/usr/share/xbps.d" \
		-D "$ROOT/void-packages" \
		-r "$ROOTFS" \
		"$@"
}

xbps-dgraph() {
	xbps/usr/bin/xbps-dgraph \
		-C "$ROOT/xbps/usr/share/xbps.d" \
		-r "$ROOTFS" \
		-R \
		"$@"
}

xbps-install() {
	./unshare-chroot \
		-d "$RWFS/home" "$ROOTFS/home" \
		-d "$RWFS/root" "$ROOTFS/root" \
		-d "$RWFS/var" "$ROOTFS/var" \
		-b "$ROOT/fake-bin/chown" "/bin/chown" \
		-b "$ROOT/fake-bin/chgrp" "/bin/chgrp" \
		-- $ROOT/xbps/usr/bin/xbps-install \
		-C "$ROOT/xbps/usr/share/xbps.d" \
		-c "$ROOT/xbps/var/db/xbps" \
		-r "$ROOTFS" \
		-R "$HOSTDIR/binpkgs" \
		"$@"
}

xbps-pkgdb() {
	xbps/usr/bin/xbps-pkgdb \
		-C "$ROOT/xbps/usr/share/xbps.d"
		-r "$ROOTFS" \
		"$@"
}

xbps-query() {
	./unshare-chroot \
		-d "$RWFS/var" "$ROOTFS/var" \
		-- $ROOT/xbps/usr/bin/xbps-query \
		-C "$ROOT/xbps/usr/share/xbps.d" \
		-c "$ROOT/xbps/var/db/xbps" \
		-r "$ROOTFS" \
		--repository="$HOSTDIR/binpkgs" \
		"$@"
}

xbps-reconfigure() {
	PATH="/mnt/xbps/usr/bin:/usr/bin" \
	LOGNAME=root \
	USER=root \
	HOME=/root \
	./unshare-chroot \
		-r \
		-d /proc "$ROOTFS/proc" \
		-d /sys "$ROOTFS/sys" \
		-d /dev "$ROOTFS/dev" \
		-d "$ROOT" "$ROOTFS/mnt" \
		-d "$RWFS/home" "$ROOTFS/home" \
		-d "$RWFS/root" "$ROOTFS/root" \
		-d "$RWFS/var" "$ROOTFS/var" \
		-b "$ROOT/fake-bin/chown" "$ROOTFS/bin/chown" \
		-b "$ROOT/fake-bin/chgrp" "$ROOTFS/bin/chgrp" \
		-b "$ROOT/fake-bin/mknod" "$ROOTFS/bin/mknod" \
		-m "$ROOTFS/" \
		-- /mnt/xbps/usr/bin/xbps-reconfigure \
			-C "/build-tmp/xbps/usr/share/xbps.d" \
			"$@"
}

xbps-remove() {
	xbps/usr/bin/xbps-remove \
		-C "$ROOT/xbps/usr/share/xbps.d" \
		-c "$ROOT/xbps/var/db/xbps" \
		-r "$ROOTFS" \
		"$@"
}

xbps-uhelper() {
	xbps/usr/bin/xbps-uhelper \
		-C "$ROOT/xbps/usr/share/xbps.d"
		-r "$ROOTFS" \
		"$@"
}

update-xbps() {
	xbps/usr/bin/xbps-install -r "$ROOT/xbps" --sync
	xbps/usr/bin/xbps-install -r "$ROOT/xbps" --dry-run xbps &&
	xbps/usr/bin/xbps-install -r "$ROOT/xbps" --yes xbps
}

install-xbps() {
	[ -e xbps/usr/bin/xbps-install ] || {
		curl -L -O "https://alpha.de.repo.voidlinux.org/static/xbps-static-latest.$HOST_ARCH_MUSL.tar.xz"
		mkdir -p "$ROOT/xbps"
		tar xf "xbps-static-latest.$HOST_ARCH_MUSL.tar.xz" -C "$ROOT/xbps"
		mkdir -p "$ROOT/xbps/usr/share/xbps.d"
		print \
			"architecture=$TARGET_ARCH" \
			"repository=http://alpha.de.repo.voidlinux.org/current/$(
				case "$TARGET_ARCH" in
					*-musl)
						echo musl
					;;
				esac
			)/" > "$ROOT/xbps/usr/share/xbps.d/00-repository-main.conf"
	}

	update-xbps
}

xbps-src() {
	(cd "$ROOT/void-packages"
	PATH="$ROOT/xbps/usr/bin:$PATH" \
	exec ./xbps-src \
		-m "$MASTERDIR" \
		-H "$HOSTDIR" \
		-j "$(nproc)" \
		"$@"
	)
}

bootstrap() {
	mkdir -p xbps "$MASTERDIR" "$HOSTDIR"
	[ -e xbps/usr/bin/xbps-install ] || install-xbps

	git clone --depth 1 "https://github.com/void-linux/void-packages.git"
	xbps-src binary-bootstrap "$TARGET_ARCH"
	(cd "$ROOT"
	ln -s configs/void-packages.conf void-packages/etc/conf)
}

bootstrap-update() {
	local _dirty="no"
	if [[ ! -d "$ROOT/void-packages" ]]; then
		bootstrap
	fi

	update-xbps
	(cd "$ROOT/void-packages"
	git diff --quiet || _dirty=yes
	git stash # TODO: isn't stashing ugly in this case?
	git pull origin master
	if [[ "$_dirty" = "yes" ]]; then
		git stash pop
	fi)
	xbps-src bootstrap-update
}

patch-src-pkgs() {
	(cd $ROOT/void-packages
	git checkout .)
	for p in "$ROOT"/patches/void-packages/*; do
		patch -p1 -d "$ROOT/void-packages" < "$p"
	done

	(cd "$ROOT/patches/void-packages-srcpkgs"
	for p in *; do
		local patch_dir="$ROOT/void-packages/srcpkgs/$p/patches"
		mkdir -p "$patch_dir"
		cp "$p"/* "$patch_dir"
	done)
}

build-src-pkgs() {
	for p in \
		xbps-triggers \
		shared-mime-info \
		gtk+3 \
		libzbar \
		gst-plugins-good1 \
		cpio
	do
		xbps-src pkg "$p"
	done
}

basic-rootfs() {
	mkdir -p "$ROOTFS"/{dev,etc,home,proc,root,sys,mnt,tmp,usr/bin,usr/lib,var,rw}
	cp -fa "$ROOT"/rootfs-stub/* "$ROOTFS"

	mkdir -p "$ROOTFS/usr/share/xbps.d"
	cp -fa "$ROOT/xbps/usr/share/xbps.d/00-repository-main.conf" \
		"$ROOTFS/usr/share/xbps.d/"
}

basic-rwfs() {
	mkdir -p "$RWFS"/{etc,home,var/db/xbps/keys,root}
	cp "$ROOT"/xbps/var/db/xbps/keys/* "$RWFS/var/db/xbps/keys/"
}

compile() {
	if [[ "$1" -nt "$2" ]]; then
		gcc -std=c99 -Wall -Wextra -O2 "$1" -o "$2"
	fi
}

compile-tools() {
	compile unshare-chroot.c unshare-chroot
	compile genimg.c genimg
}

install-pkgs() {
	# FIXME: why xfce and xfce4-plugins need exo and libxfce4panel? instead of pulling them as dependencies?
	xbps-install --yes --unpack-only \
		base-system attr-progs squashfs-tools-ng cpio \
		elogind busybox \
		xorg-minimal xorg-input-drivers xorg-video-drivers xorg-fonts \
		noto-fonts-cjk noto-fonts-emoji noto-fonts-ttf noto-fonts-ttf-extra \
		nss-mdns \
		nodm exo libxfce4panel xfce4 xfce4-plugins xfce4-screenshooter \
		ksuperkey \
		firefox libreoffice \
		gnome-mpv \
		xfburn evince mate-calc \
		catfish thunar-archive-plugin engrampa handbrake \
		network-manager-applet gnome-disk-utility \
		cups cups-filters system-config-printer system-config-printer-udev \
		flatpak
	# NOTES:
	# Xubuntu uses atril instead of evince, pdf.js from firefox could be used instead of both
	# gimp?, inkscape?, audacity?
	# bluetooth?
	# video calls
	# file-system/disk package
	# remove all below?
	# flatpak
	xbps-reconfigure --all
}

patch-rootfs() {
	# FIXME: patching two times does not work
	for p in patches/rootfs/*; do
		patch -p1 -d "$ROOTFS" < "$p"
	done
}

newest-in-dir() {
	local _dir="$1"
	local _newest="$2"
	local _except="$3"

	for i in "$_dir"/*; do
		case "$i" in
			$_except) ;;
			*) test "$i" -nt "$_newest" && _newest="$i" ;;
		esac
	done
	echo "$_newest"
}

fix-mtime() {
	local _src="$1"
	shift
	touch -ch -r "$_src" "$@"
}

fix-symlink-mtime() {
	local _link="$1"
	local _target=$(readlink -f "$_link")
	fix-mtime "$_target" "$_link"
}

enable-services() {
	for i in \
		acpid \
		avahi-daemon \
		elogind \
		cups-browsed \
		cupsd \
		dbus \
		NetworkManager \
		nodm \
		ntpd \
		polkitd \
		udevd
	do
		if [[ ! -e "$ROOTFS/etc/sv/$i" ]]; then
			echo Service $i does not exist
			false
		fi
		ln -f -s "/etc/sv/$i" "$ROOTFS/etc/runit/runsvdir/default"
		fix-mtime "$ROOTFS/etc/sv/$i" "$ROOTFS/etc/runit/runsvdir/default/$i"
	done
}

fix-mtime-file-from-parent() {
	local _file="$1"
	local _except="$2"

	local _newest=$(newest-in-dir $(dirname "$_file") "" "$_except")
	if [[ -n "$_newest" ]]; then
		fix-mtime "$_newest" "$_file"
	fi
}

fix-mtime-from-dir() {
	local _dst="$1"
	local _dir="$2"
	local _except="$3"

	fix-mtime $(newest-in-dir "$_dir" "" "$_except") "$_dst"
}

users() {
	PATH="/mnt/xbps/usr/bin:/usr/bin" \
	./unshare-chroot \
		-b "$ROOT/fake-bin/chown" "$ROOTFS/usr/bin/chown" \
		-b "$ROOT/fake-bin/chgrp" "$ROOTFS/usr/bin/chgrp" \
		-d /proc "$ROOTFS/proc" \
		-d /sys "$ROOTFS/sys" \
		-d /dev "$ROOTFS/dev" \
		-d "$ROOT" "$ROOTFS/mnt" \
		-d "$RWFS/home" "$ROOTFS/home" \
		-d "$RWFS/root" "$ROOTFS/root" \
		-d "$RWFS/var" "$ROOTFS/var" \
		-m "$ROOTFS" \
		-- /usr/bin/sh <<-EOF
	if ! id tmtstr; then
		/usr/bin/useradd -m tmtstr -G audio,input,lpadmin,optical,scanner,storage,users,video,wheel
		{ echo tmtstr; echo tmtstr; } | /usr/bin/passwd tmtstr
	fi
	EOF
}

fix-mtimes() {
	for i in "$ROOTFS"/boot/vmlinuz-*; do
		local version=${i##*-}
		fix-mtime "$i" "$ROOTFS/boot/initramfs-$version.img"
		find "$ROOTFS/usr/lib/modules/$version/" -type d -exec \
			"$0" fix-mtime "$i" '{}' \+
	done

	fix-mtime-from-dir "$ROOTFS/etc/udev/hwdb.bin" "$ROOTFS/usr/lib/udev/hwdb.d"

	find "$ROOTFS/usr/share/fonts" -type f -name "fonts.*" -exec \
		"$0" fix-mtime-file-from-parent '{}' "*/fonts.*" \;

	# fix alternatives' symlinks mtimes
	xbps-alternatives -l | awk -v tmtstr="$0" -v rootfs="$ROOTFS" '
		parse && !/^  -/ {
			parse = 0
		}
		parse {
			if(split($2, lnk, /:/) != 2) {
				exit 1
			}
			system("\"" tmtstr "\" fix-symlink-mtime \"" rootfs "$(dirname \"" lnk[2] "\")/" lnk[1]"\"")
		}
		/^ - / && $NF == "(current)" {
			parse = 1
		}'

	# fix man page dirs mtimes
	for i in "$ROOTFS"/usr/share/man/*/; do
		fix-mtime-from-dir "$i" "$i"
	done
	# fix mandoc.db mtime
	fix-mtime-from-dir "$ROOTFS/usr/share/man/mandoc.db" "$ROOTFS/usr/share/man" mandoc.db

	# fix mtimes for base directories
	for i in "$ROOTFS" "$ROOTFS"/*; do
		touch -ch -d "2001-01-01 00:00:01" "$i"
	done
}

gen-image() {
	local _out="$1"
	if [[ -z "$_out" ]]; then
		_out="$BUILDDIR/img-rootfs.sqfs"
	fi

	cp genimg "$MASTERDIR/usr/bin"

	PATH="/mnt/xbps/usr/bin:/usr/bin" \
	./unshare-chroot -d /proc "$MASTERDIR/proc" \
		-d /sys "$MASTERDIR/sys" \
		-d /dev "$MASTERDIR/dev" \
		-d "$ROOTFS" "$MASTERDIR/mnt" \
		-m "$MASTERDIR" \
		-- /usr/bin/sh <<-EOF
	set -e
	xbps-install --dry-run squashfs-tools-ng &&
	xbps-install --yes squashfs-tools-ng
	genimg /mnt gensquashfs \
		--compressor zstd \
		--exportable \
		--num-jobs $(nproc) \
		--force \
		--pack-dir /mnt \
		--pack-file /dev/stdin \
		--keep-time \
		/builddir/tmtstr.sqfs
	EOF
	mv "$MASTERDIR/builddir/tmtstr.sqfs" "$_out"
}

build-wip() {
	echo @@@ update
	bootstrap-update
	echo @@@ compile-tools
	compile-tools

	echo @@@ patch-src-pkgs
	patch-src-pkgs
	echo @@@ build-src-pkgs
	build-src-pkgs
	echo @@@ basic-rootfs
	basic-rootfs
	echo @@@ basic-rwfs
	basic-rwfs
	echo @@@ install-pkgs
	xbps-install -S
	install-pkgs
	echo @@@ patch-rootfs
	patch-rootfs
	echo @@@ enable-services
	enable-services
	echo @@@ users
	users
	echo @@@ fix-mtimes
	fix-mtimes
	echo @@@ gen-image
	gen-image
}

help() {
	awk '
		/^[a-z-]+\(\) {$/ { print substr($1, 1, length($1)-2) help }
		{ if($0 ~ /^# /) help = "\t\t" substr($0, 3); else help = "" }
	' "$0" | sort
}

_cmd="$1"
shift
awk -v cmd="$_cmd" '
BEGIN { ret = 1; if(cmd !~ /^[a-z-]+$/) exit }
$0 == cmd"() {" { ret=0; exit }
END { exit ret }' "$0" || {
	echo "$0: unknown command $_cmd"
	help
	exit 1
}

ROOT=$(root-dir)
# musl targets do not have nss-mdns equivalent
TARGET_ARCH=x86_64
HOST_ARCH=x86_64
HOST_ARCH_MUSL=x86_64-musl
BUILDDIR="$ROOT/build-$TARGET_ARCH"
ROOTFS="$BUILDDIR/rootfs"
RWFS="$BUILDDIR/rwfs"
HOSTDIR="$BUILDDIR/hostdir"
MASTERDIR="$BUILDDIR/masterdir"

"$_cmd" "$@"
