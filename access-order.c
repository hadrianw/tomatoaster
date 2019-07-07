#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <sys/fanotify.h>
#include <sys/mount.h>
#include <sys/mman.h>
#include <sys/sendfile.h>
#include <sys/syscall.h>
#include <unistd.h>

# ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 1U
#endif

static int run = 1;

void
term(int sig)
{
	run = 0;
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
	int mfd;
	FILE *mf;

	mount("proc", "/proc", "proc", MS_NOSUID|MS_NOEXEC|MS_NODEV, 0);

	int fd = fanotify_init(FAN_CLOEXEC, O_RDONLY);

	// DONT_FOLLOW?
	fanotify_mark(fd, FAN_MARK_ADD | FAN_MARK_MOUNT, FAN_ACCESS, 0, "/");

	pid = fork();
	if(pid < 0) {
		return -1;
	}
	if(pid > 0) {
		execl("/sbin/init", "/sbin/init");
		return -1;
	}

	mfd = syscall(SYS_memfd_create, "access-order", MFD_CLOEXEC);
	mf = fdopen(mfd, "w");

	signal(SIGTERM, term);

	for(;;) {
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
			if(path_len == -1) {
				perror("readlink");
				return -1;
			}

			path[path_len] = '\0';
			fprintf(mf, "%s %d\n", path, priority);
			close(meta->fd);

			priority++;
			if(priority > 32767) {
				return 0;
			}

next:
			meta = FAN_EVENT_NEXT(meta, len);
		}
	}
	close(fd);

	fflush(mf);

	int sortfd = open("/var/log/access-order.log", O_WRONLY|O_CREAT|O_TRUNC, 0644);
	long size = ftell(mf);
	off_t off = 0;

	do {
		len = sendfile(sortfd, mfd, &off, size);
		if(len < 0) {
			return -1;
		}
		size -= len;
	} while(size > 0);

	fclose(mf);
	close(sortfd);
	return 0;
}
