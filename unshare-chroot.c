#define _GNU_SOURCE
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>
#include <sys/types.h>

static void
usage()
{
	fputs("usage: ./unshare-chroot [-r] [-d SRC DST] [-b SRC DST] [-m NEW_ROOT] -- CMD [ARGS]\n", stderr);
}

int
main(int argc, char *argv[])
{
	int i;
	bool map_root = false;
	uid_t uid;
	uid_t gid;
	char *src;
	char *dst;

	if(argc < 3) {
		usage();
		fputs("unshare-chroot: not enough arguments\n", stderr);
		return -1;
	}

	uid = getuid();
	gid = getgid();

	if(unshare(CLONE_NEWUSER|CLONE_NEWNS) == -1) {
		perror("unshare");
		return -1;
	}

	for(i=1; i < argc && strcmp(argv[i], "--"); ) {
		if(!strcmp(argv[i], "-d") && i + 2 < argc) {
			src = argv[i+1];
			dst = argv[i+2];
			if(chdir(src) == -1) {
				fprintf(stderr,
					"uunshare-mask: chdir %s failed: %s\n",
					src, strerror(errno)
				);
			}
			src = ".";
			if(mount(src, dst, NULL, MS_BIND|MS_REC|MS_PRIVATE, NULL) == -1) {
				fprintf(stderr,
					"uunshare-mask: bind mount %s on %s failed: %s\n",
					src, dst, strerror(errno)
				);
				return -1;
			}
			i += 3;
		} else if(!strcmp(argv[i], "-b") && i + 2 < argc) {
			src = argv[i+1];
			dst = argv[i+2];
			if(mount(src, dst, NULL, MS_BIND|MS_REC|MS_PRIVATE, NULL) == -1) {
				fprintf(stderr,
					"uunshare-mask: bind mount %s on %s failed: %s\n",
					src, dst, strerror(errno)
				);
				return -1;
			}
			i += 3;
		} else if(!strcmp(argv[i], "-m") && i + 1 < argc) {
			src = argv[i+1];
			dst = "/";
			if(chdir(src) == -1) {
				fprintf(stderr,
					"uunshare-mask: chdir %s failed: %s\n",
					src, strerror(errno)
				);
			}
			if(mount(".", ".", NULL, MS_BIND|MS_PRIVATE, NULL) == -1) {
				fprintf(stderr,
					"uunshare-mask: bind mount . on . failed: %s\n",
					strerror(errno)
				);
				return -1;
			}
			if(mount(src, dst, NULL, MS_MOVE, NULL) == -1) {
				fprintf(stderr,
					"uunshare-mask: bind mount %s on %s failed: %s\n",
					src, dst, strerror(errno)
				);
				return -1;
			}
			if(chroot(".") == -1) {
				fprintf(stderr,
					"uunshare-mask: chroot . failed: %s\n",
					strerror(errno)
				);
			}
			i += 2;
		} else if(!strcmp(argv[i], "-r")) {
			map_root = true;
			i++;
		} else {
			usage();
			return -1;
		}
	}

	if(i+1 >= argc) {
		usage();
		fputs("expected -- argument and the command the least", stderr);
		return -1;
	}

	if(strcmp(argv[i], "--")) {
		usage();
		fputs("expected -- argument", stderr);
		return -1;
	}

	FILE *file;
	int len;

	file = fopen("/proc/self/uid_map", "wb");
	if(!file) {
		perror("fopen uid_map");
		return -1;
	}
	len = fprintf(file, "%d %d 1\n", map_root ? 0 : uid, uid);
	if(len < 0) {
		perror("write uid_map");
		return -1;
	}
	fclose(file);

	file = fopen("/proc/self/setgroups", "wb");
	if(!file) {
		perror("fopen setgroups");
		return -1;
	}
	len = fputs("deny", file);
	if(len < 0) {
		perror("write setgroups");
		return -1;
	}
	fclose(file);

	file = fopen("/proc/self/gid_map", "wb");
	if(!file) {
		perror("fopen gid_map");
		return -1;
	}
	len = fprintf(file, "%d %d 1\n", map_root ? 0 : gid, gid);
	if(len < 0) {
		perror("write gid_map");
		return -1;
	}
	fclose(file);

	/*
	if(unshare(CLONE_NEWNS) == -1) {
		perror("unshare ns");
		return -1;
	}
	*/

	argv += i + 1;
	execvp(argv[0], argv);
	perror("execvp");
	return -1;
}
