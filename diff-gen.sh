#!/bin/sh
set -ex

old="$1"
new="$2"
diff="$3"

rm -rf update/diff update/work
mkdir -p update/old update/new update/diff update/work update/diff-gen

mount "$old" update/old
mount "$new" update/new

mount -t overlay diff-gen-overlay \
	-o lowerdir=update/old,upperdir=update/diff,workdir=update/work \
	update/diff-gen
rsync -aHAX --delete --info=progress2 update/new/ update/diff-gen/
umount update/diff-gen
rm -rf update/work

umount update/new update/old

mksquashfs update/diff "$diff" -all-root
rm -rf update/diff
