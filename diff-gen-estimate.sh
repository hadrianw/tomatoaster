#!/bin/sh
set -e

old="$1"
new="$2"

mkdir -p update/old update/new
mount "$old" update/old
mount "$new" update/new

#rsync -n -i -aHAX --delete update/new update/old > diffs

diff -rq --no-dereference update/old update/new |
awk '
BEGIN{ORS="\0"}
$1 == "Files" && $NF == "differ" {print $4}
/^Only in new/ { print substr($3, 1, length($3)-1) "/" $4 }' |
du -hc --apparent-size --files0-from - | tail -n1

umount update/new update/old
