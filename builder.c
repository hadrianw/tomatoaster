#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>
#include <sys/types.h>

int
main(int argc, char *argv[])
{
	uid_t uid = getuid();
	uid_t gid = getgid();

	if(unshare(CLONE_NEWUSER|CLONE_NEWNS) == -1) {
		perror("unshare");
		return -1;
	}

	if(mount("chown-log.sh", "/bin/chown", NULL, MS_BIND|MS_REC|MS_PRIVATE, NULL) == -1) {
		perror("mount chown");
		return -1;
	}

	if(mount("chgrp-log.sh", "/bin/chgrp", NULL, MS_BIND|MS_REC|MS_PRIVATE, NULL) == -1) {
		perror("mount chown");
		return -1;
	}

	if(mount("libcap/progs/setcap", "/sbin/setcap", NULL, MS_BIND|MS_REC|MS_PRIVATE, NULL) == -1) {
		perror("mount chown");
		return -1;
	}

	FILE *file;
	int len;

	file = fopen("/proc/self/uid_map", "wb");
	if(!file) {
		perror("fopen uid_map");
		return -1;
	}
	len = fprintf(file, "%d %d 1\n", uid, uid);
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
	len = fprintf(file, "%d %d 1\n", gid, gid);
	if(len < 0) {
		perror("write gid_map");
		return -1;
	}
	fclose(file);

	if(unshare(CLONE_NEWNS) == -1) {
		perror("unshare ns");
		return -1;
	}

	argv++;
	execvp(argv[0], argv);
	perror("execvp");
	return -1;
}
