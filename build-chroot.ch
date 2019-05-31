#!/bin/sh
set -e

src_dir_name="$1"
src_dir=$(readlink -f "$src_dir_name")
shift 1

cd void-packages/

for i in XBPS_MASTERDIR XBPS_DISTDIR XBPS_HOSTDIR XBPS_CHROOT_CMD_ARGS; do
	export "$i"=$(./xbps-src show-var "$i")
done

mkdir -p "$XBPS_MASTERDIR/build-chroot/$src_dir_name"

XBPS_CHROOT_CMD_ARGS="-b $src_dir:/build-chroot/$src_dir_name $XBPS_CHROOT_CMD_ARGS"

common/chroot-style/uunshare.sh \
$XBPS_MASTERDIR $XBPS_DISTDIR "$XBPS_HOSTDIR" "$XBPS_CHROOT_CMD_ARGS" "$@"
