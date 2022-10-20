#define _XOPEN_SOURCE 500
#include <ftw.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>
#include <unistd.h>

#define ModeMetaMask 1
#define UidMetaMask 2
#define GidMetaMask 4

typedef struct meta_path_t meta_path_t;
struct meta_path_t {
	meta_path_t *next;
	union {
		mode_t mode;
		uid_t uid;
		gid_t gid;
	};
	short mask;
	char path[];
};
static meta_path_t *meta_path_first = NULL;
static meta_path_t *meta_path_last = NULL;

typedef struct inode_path_t inode_path_t;
struct inode_path_t {
	inode_path_t *next;
	ino_t inode;
	char path[];
};
static inode_path_t *inode_path_first = NULL;
static inode_path_t *inode_path_last = NULL;

static bool all_root = true;

static char target[BUFSIZ];

void
print_path(const char *path)
{
	while(*path) {
		if(*path == '"' || *path == '\\') {
			putchar('\\');
		}
		putchar(*path++);
	}
}

static int
num_from_xattr(const char *filename, const char *name, unsigned short *out_value)
{
	ssize_t vsize;
	char str_value[16];
	long value;
	char *b;

	vsize = lgetxattr(filename, name, str_value, sizeof(str_value)-1);
	if(vsize < 0) {
		/*
		ERROR("lgetxattr failed for %s in "
			"num_from_xattr, because %s", filename,
			strerror(errno));
		ERROR_EXIT(".  Ignoring");
		*/
		return 1;
	}
	str_value[vsize] = '\0';

	value = strtoll(str_value, &b, 10);
	if(*b !='\0') {
		fprintf(stderr, "strtoll failed for xattr %s of file %s\n", name, filename);
		return -1;
	}
	if(value < 0 || value >  USHRT_MAX) {
		// FIXME: proper error handling
		fprintf(stderr, "strtoll out of range for xattr %s of file %s\n", name, filename);
		return -1;
	}
	*out_value = (unsigned short)value;
	return 0;
}

static void
meta_append(const char *path, int mask, short value)
{
	size_t len = strlen(path);
	meta_path_t *mp = malloc(sizeof(*mp) + len + 1);
	mp->next = NULL;
	mp->mask = mask;
	switch(mask) {
	case ModeMetaMask:
		mp->mode = value;
		break;
	case UidMetaMask:
		mp->uid = value;
		break;
	case GidMetaMask:
		mp->gid = value;
		break;
	}
	memcpy(mp->path, path, len);
	mp->path[len] = '\0';
	if(meta_path_first == NULL) {
		meta_path_first = mp;
	} else {
		meta_path_last->next = mp;
	}
	meta_path_last = mp;
}

void
print_entry(const char *type, const char *path, const struct stat *sb)
{
	unsigned short id;
	uid_t uid = 0;
	gid_t gid = 0;
	if(!all_root) {
		uid = sb->st_uid;
		gid = sb->st_gid;
	}
	if(!strncmp(path, "home/tmtstr", strlen("home/tmtstr"))) {
		uid = 1000;
		gid = 1000;
	}

	if(num_from_xattr(path, "user.escape.uid", &id) == 0) {
		lremovexattr(path, "user.escape.uid");
		uid = id;
		meta_append(path, UidMetaMask, id);
	}
	if(num_from_xattr(path, "user.escape.gid", &id) == 0) {
		lremovexattr(path, "user.escape.gid");
		gid = id;
		meta_append(path, GidMetaMask, id);
	}

	printf("%s \"/", type);
	print_path(path);
	printf("\" %04o %u %u", sb->st_mode & 07777, uid, gid);
}

static int
step(const char *path, const struct stat *sb, int typeflag, struct FTW *ftwbuf)
{
	(void)ftwbuf;

	if(!strcmp(path, ".")) {
		path++;
	} else if(!strncmp(path, "./", strlen("./"))) {
		path += strlen("./");
	}
	// FIXME: root directory has empty path ""

	if(sb->st_nlink > 1) {
		// search for inode in inode_path list
		for(inode_path_t *ip = inode_path_first; ip; ip = ip->next) {
			if(ip->inode != sb->st_ino) {
				continue;
			}
			// if found emit: link path ... target
			printf("link \"");
			print_path(path);
			// target path can't be quote wrapped, strange
			printf("\" 0 0 0 %s\n", ip->path);
			return 0;
		}
		// else append path to the list
		size_t len = strlen(path);
		inode_path_t *ip = malloc(sizeof(*ip) + len + 1);
		ip->next = NULL;
		ip->inode = sb->st_ino;
		memcpy(ip->path, path, len);
		ip->path[len] = '\0';
		if(inode_path_first == NULL) {
			inode_path_first = ip;
		} else {
			inode_path_last->next = ip;
		}
		inode_path_last = ip;
	}
	switch(typeflag) {
	case FTW_F:
		switch(sb->st_mode & S_IFMT) {
		case S_IFREG:
			print_entry("file", path, sb);
			putchar('\n');
			if(!(sb->st_mode & S_IRUSR) && sb->st_uid == getuid()) {
				mode_t tmp_st_mode = sb->st_mode | S_IRUSR;
				if(chmod(path, tmp_st_mode) != 0) {
					perror("chmod");
					// FIXME: try to restore previous modes on errors
					return -1;
				}

				meta_append(path, ModeMetaMask, sb->st_mode);
			}
			break;
		case S_IFIFO:
			print_entry("pipe", path, sb);
			putchar('\n');
			break;
		case S_IFSOCK:
			print_entry("sock", path, sb);
			putchar('\n');
			break;
		case S_IFBLK:
		case S_IFCHR:
			print_entry("nod", path, sb);
			printf("%c %u %u\n", (sb->st_mode & S_IFMT) == S_IFBLK ? 'b' : 'c', major(sb->st_rdev), minor(sb->st_rdev));
			break;
		case S_IFLNK:
		case S_IFDIR:
		default:
			return -1;
		}
		break;
	case FTW_D:
		print_entry("dir", path, sb);
		putchar('\n');
		break;
	case FTW_SL: {
		// Remember: readlink does not null terminate
		ssize_t len = readlink(path, target, sizeof(target));
		if(len < 0) {
			perror("readlink");
		} else if(len == sizeof(target)) {
			// FIXME: extend buffer
			fprintf(stderr, "Possible truncation of symlink's (%s) target\n", path);
			return -1;
		}
		// We abort on len == sizeof(target) so it's always safe to put a null terminator
		target[len] = '\0';

		print_entry("slink", path, sb);
		printf(" %s\n", target);
		break;
	}
	case FTW_DNR:
		perror("FTW_DNR");
		return -1;
	case FTW_NS:
		perror("FTW_NS");
		return -1;
	case FTW_DP:
	case FTW_SLN:
	default:
		return -1;

	}

	return 0;
}

// TODO: what about sorting?
int
main(int argc, char *argv[])
{
	pid_t pid;
	int wstatus;
	char buf[16];
	int siz;
	int fd[2];

	if(argc > 2) {
		if(pipe(fd) < 0) {
			perror("pipe");
			return -1;
		}

		pid = fork();
		if(pid < 0) {
			perror("fork");
			return -1;
		} else if(pid == 0) {
			dup2(fd[0], 0);
			close(fd[1]);
			close(fd[0]);
			execvp(argv[2], argv + 2);
			perror("execv");
			return -1;
		}

		dup2(fd[1], 1);
		close(fd[0]);
		close(fd[1]);
	}

	if(chdir(argv[1]) != 0) {
		perror("chdir");
		return -1;
	}
	if(nftw(".", step, 16, FTW_PHYS) != 0) {
		perror("nftw");
		goto out_restore;
		return -1;
	}
	fclose(stdout);

	if(argc > 2) {
		waitpid(pid, &wstatus, 0);
		// FIXME: log errors
	}

out_restore:
	for(meta_path_t *mp = meta_path_first; mp; mp = mp->next) {
		// FIXME: error handling
		switch(mp->mask) {
		case ModeMetaMask:
			chmod(mp->path, mp->mode);
			break;
		case UidMetaMask:
			siz = snprintf(buf, sizeof buf, "%hu", mp->uid);
			if(siz <= 0) {
				// FIXME: ERROR!
				break;
			}
			lsetxattr(mp->path, "user.escape.uid", buf, siz, XATTR_CREATE);
			break;
		case GidMetaMask:
			siz = snprintf(buf, sizeof buf, "%hu", mp->gid);
			if(siz <= 0) {
				// FIXME: ERROR!
				break;
			}
			lsetxattr(mp->path, "user.escape.gid", buf, siz, XATTR_CREATE);
			break;
		}
	}

	return 0;
}
