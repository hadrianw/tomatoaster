#if 0
exec musl-gcc -static access-order.c -o access-order -Wall -Wextra -pedantic -std=c99 -O2
exit 1
#endif

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fanotify.h>
#include <sys/mount.h>
#include <unistd.h>

#include "uthash.h"

struct entry {
	const char *path;
	int priority;
	UT_hash_handle hh;
};

static int run = 1;

void
quit(int sig)
{
	run = 0;
}

int
priority_cmp(struct entry *a, struct entry *b)
{
	return (a->priority > b->priority) - (a->priority < b->priority);
}

int
main(int argc, char *argv[])
{
	struct fanotify_event_metadata *meta;
	struct fanotify_event_metadata buf[200];
	pid_t pid;
	ssize_t len;
	char procfd_path[PATH_MAX];
	char path[PATH_MAX];
	ssize_t path_len;
	int priority = -32768;
	struct entry *file_tab = NULL;

	pid = getpid();

	if(pid == 1) {
		mount("proc", "/proc", "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, 0);
	}

	int fd = fanotify_init(FAN_CLOEXEC, O_RDONLY);

	// DONT_FOLLOW?
	fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_MOUNT, FAN_ACCESS, 0, "/");

	if(pid == 1) {
		pid = fork();
		if(pid < 0) {
			return -1;
		}
		if(pid > 0) {
			execl("/sbin/init", "/sbin/init");
			return -1;
		}
	}

	signal(SIGINT, quit);

	while(run) {
		do {
			len = read(fd, buf, sizeof(buf));
		} while(run && len == -1 && errno == EAGAIN);

		if(!run) {
			break;
		}

		if(len == -1) {
			perror("read");
			return -1;
		} else if(len == 0) {
			break;
		}

		meta = buf;
		while(FAN_EVENT_OK(meta, len)) {
			if(meta->vers != FANOTIFY_METADATA_VERSION) {
				fputs("Mismatch of fanotify metadata version.\n", stderr);
				return -1;
			}
			if(meta->fd == FAN_NOFD) {
				goto next;
			}
		

			snprintf(procfd_path, sizeof(procfd_path),
					"/proc/self/fd/%d", meta->fd);
			path_len = readlink(procfd_path, path, sizeof(path) - 1);
			close(meta->fd);

			if(path_len == -1) {
				perror("readlink");
				goto next;
			}

			path[path_len] = '\0';

			if(path[0] != '/') {
				goto next;
			}

			struct entry *found;
			HASH_FIND_STR(file_tab, path, found);
			if(found == NULL) {
				struct entry *entry = malloc(sizeof(*entry));
				entry->path = strdup(path);
				entry->priority = priority;
				HASH_ADD_STR(file_tab, path, entry);

				priority++;
				if(priority > 32767) {
					run = 0;
					break;
				}
			}

next:
			meta = FAN_EVENT_NEXT(meta, len);
		}
	}
	close(fd);

	HASH_SORT(file_tab, priority_cmp);

	int sortfd = open("/var/log/access-order.log", O_WRONLY|O_CREAT|O_TRUNC, 0644);
	FILE *sort = fdopen(sortfd, "w");

	struct entry *entry;
	struct entry *tmp;
	HASH_ITER(hh, file_tab, entry, tmp) {
		fprintf(sort, "%s %d\n", entry->path, entry->priority);
	}

	fclose(sort);
	return 0;
}
