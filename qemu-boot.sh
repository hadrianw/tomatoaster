qemu-system-x86_64 \
	-drive file="$1",index=0,format=raw \
	-drive file=tmtstr-rw.img,index=1,format=raw \
	-enable-kvm -cpu host \
	-vga virtio -display sdl,gl=on \
	-no-reboot -serial stdio -m 2G \
	-kernel build-x86_64/rootfs/boot/vmlinuz-* -initrd build-x86_64/rootfs/boot/initramfs-* \
	-append "root=live:/dev/sda panic=1 PATH=/usr/bin console=ttyS0 init=/sbin/init rd.live.overlay=/dev/sdb rd.live.overlay.overlayfs=1"
