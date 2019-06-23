# Tomatoaster

A Linux distribution with ChromeOS-like updates - AB partition scheme.

Builds a somewhat reproducible squashfs rootfs image of something resembling Xubuntu.

There's a script generating a delta image.

It uses Void Linux packages and xbps-src to build the image.

## Requirements:

- Linux 4.14+
- bash
- wget
- git
- gcc

## TODO:

- modularize the image generation script
- actually reliably booting the image
- installation
- updates
	- a delta image between the same version should be empty
- make it depend only on what is provided by xbps and xbps-src

## Updates - WIP

Generation of the delta image is done via rsync and overlayfs.
A crude pseudo-script to do the update as below:

```
wget http://tomatoaster.org/updates/delta1.sqfs
mount /dev/root old
mount delta1.sqfs delta
mount -t overlay new-root-gen-overlay \
        -o lowerdir=old:delta new
mksquashfs new /dev/root2 -all-root
```

There is a possible optimization to use bsdiff for changed files
in the delta image.
