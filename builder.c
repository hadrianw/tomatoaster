#define _GNU_SOURCE
#include <stdio.h>
#include <sched.h>
#include <sys/mount.h>
#include <unistd.h>

int
main(int argc, char *argv[])
{
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

	argv++;
	execvp(argv[0], argv);
	perror("execvp");
	return -1;
}
