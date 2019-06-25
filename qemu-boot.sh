qemu-system-x86_64 \
	-drive file="$1",index=0,format=raw \
	-drive file=tmtstr-rw.img,index=1,format=raw \
	-no-reboot -serial stdio -m 2G \
	-kernel rootfs/boot/vmlinuz-* -initrd rootfs/boot/initramfs-* \
	-append "root=/dev/sda panic=1 PATH=/usr/bin console=ttyS0"
