#define _GNU_SOURCE
#include <ctype.h>
#include <endian.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

struct __attribute__((__packed__)) chs {
	uint8_t head;
	uint8_t sector;
	uint8_t cylinder;
};

#define CHS_MAX 1024*256*63

struct chs
chs_from_lba(uint64_t lba)
{
	if(lba > CHS_MAX) {
		return (struct chs){0xFF, 0xFF, 0xFF};
	}
	uint16_t cylinder = lba / (256 * 63);
	uint8_t head = lba / 63 % 256;
	uint8_t sector = lba % 63 + 1;

	return (struct chs) {
		.head = head,
		.sector = sector | ((0x300 & cylinder) >> 8),
		.cylinder = cylinder & 0xFF
	};
}

struct __attribute__((__packed__)) mbr {
	uint8_t boot_indicator;
	struct chs first_chs;
	uint8_t os_type;
	struct chs last_chs;
	uint32_t first_lba;
	uint32_t last_lba;
};

struct __attribute__((__packed__)) guid {
	uint8_t data[16];
};

struct __attribute__((__packed__)) gpt_header {
	char signature[8];
	uint8_t revision[4];
	uint32_t size;
	uint32_t crc32;
	uint32_t reserved;
	uint64_t current_lba;
	uint64_t backup_lba;
	uint64_t first_lba;
	uint64_t last_lba;
	struct guid guid;
	uint64_t table_lba;
	uint32_t table_len;
	uint32_t entry_len;
	uint32_t table_crc32;
};

struct __attribute__((__packed__)) gpt_entry {
	struct guid type_guid;
	struct guid guid;
	uint64_t first_lba;
	uint64_t last_lba;
	uint64_t attributes;
	char name[72];
};

#define LEN(x) (sizeof(x)/sizeof((x)[0]))
#define MIN(x, y) ((x) < (y) ? (x) : (y))

#define BLOCK_SIZE 512
#define LBA_ALIGN 8
//#define LBA_GAP (128 * 1024 * 1024 / BLOCK_SIZE)
#define LBA_GAP 8

#define PART_TYPE (1 << 0)
#define PART_GUID (1 << 1)
#define PART_SIZE (1 << 2)
#define PART_FILE (1 << 3)

static uint8_t
parse_hex_digit(char chr)
{
	if(chr <= '9') {
		return chr - '0';
	} else if(chr <= 'F') {
		return 0xA + chr - 'A';
	} else {
		return 0xa + chr - 'a';
	}
}

static int
guid_from_str(struct guid *out_guid, const char *str)
{
	const char *chr = str;
	const uint8_t index[] = {3, 2, 1, 0, 0xFF, 5, 4, 0xFF, 7, 6, 0xFF, 8, 9, 0xFF, 10, 11, 12, 13, 14, 15};
	for(unsigned i = 0; i < LEN(index); i++) {
		if(index[i] == 0xFF) {
			if(*chr != '-' ) {
				return -1;
			}
			chr++;
			continue;
		}

		if(!isxdigit(*chr)) {
			return -1;
		}
		const uint8_t digit_hi = parse_hex_digit(*chr);
		chr++;

		if(!isxdigit(*chr)) {
			return -1;
		}
		const uint8_t digit_lo = parse_hex_digit(*chr);
		chr++;

		out_guid->data[index[i]] = (digit_hi << 4) | digit_lo;
	}
	return 0;
}

// EFI system partition C12A7328-F81F-11D2-BA4B-00A0C93EC93B
struct guid efisys = {{ 0x28, 0x73, 0x2A, 0xC1, 0x1F, 0xF8, 0xD2, 0x11, 0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B }};
// Linux filesystem data 0FC63DAF-8483-4772-8E79-3D69D8477DE4
struct guid linuxfs = {{ 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 }};
// Root partition x86-64 4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709
struct guid root_x86_64 = {{ 0xE3, 0xBC, 0x68, 0x4F, 0xCD, 0xE8, 0xB1, 0x4D, 0x96, 0xE7, 0xFB, 0xCA, 0xF9, 0x84, 0xB7, 0x09 }};
// Root partition aarch64 B921B045-1DF0-41C3-AF44-4C6F280D3FAE
struct guid root_aarch64 = {{ 0x45, 0xB0, 0x21, 0xB9, 0xF0, 0x1D, 0xC3, 0x41, 0xAF, 0x44, 0x4C, 0x6F, 0x28, 0x0D, 0x3F, 0xAE }};
// Swap 0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
struct guid swap = {{ 0x6D, 0xFD, 0x57, 0x06, 0xAB, 0xA4, 0xC4, 0x43, 0x84, 0xE5, 0x09, 0x33, 0xC8, 0x4B, 0x4F, 0x4F }};
// Home 933AC7E1-2EB4-4F13-B844-0E14E2AEF915
struct guid home = {{ 0xE1, 0xC7, 0x3A, 0x93, 0xB4, 0x2E, 0x13, 0x4F, 0xB8, 0x44, 0x0E, 0x14, 0xE2, 0xAE, 0xF9, 0x15 }};

static void
guid_gen(struct guid *guid)
{
	getentropy(guid, sizeof(*guid));
	// version 4
	guid->data[7] = 0x40 | (guid->data[6] & 0xF);
	// RFC4122
	guid->data[8] = 0x80 | (guid->data[8] & 0x3F);
}

static ssize_t
writeall(int fd, const void *buf, size_t count, off_t *offset)
{
	ssize_t len;
	do {
		len = pwrite(fd, buf, count, *offset);
		if(len < 0) {
			return len;
		}
		*offset += len;
		count -= len;
	} while(count > 0);
	return 0;
}

static void
usage(void)
{
	fprintf(stderr,
		"gptimg gen disk.img --part type=linux size=16g file=builddir/part.img\n"
		"gptimg mod disk.img --part index=0 --with file=builddir/part.img\n"
		"gptimg mod disk.img --part index=0 --with type=efisys\n"
		"gptimg extract disk.img --part index=0 --out  builddir/part.img\n"
	);
	exit(1);
}

static size_t
parse_size(const char *str)
{
	size_t size;
	char unit = 0;
	int ret = sscanf(str, "%zu%c", &size, &unit);
	if(ret < 0) {
		perror("sscanf");
		usage();
		return 0;
	}
	if(ret == 0) {
		fprintf(stderr, "couldn't parse size: '%s'\n", str);
		usage();
		return 0;
	}
	switch(unit) {
	case 'T':
	case 't':
		size *= 1ull << 40;
		break;
	case 'G':
	case 'g':
		size *= 1ull << 30;
		break;
	case 'M':
	case 'm':
		size *= 1ull << 20;
		break;
	case 'K':
	case 'k':
		size *= 1ull << 10;
		break;
	case '\0':
		break;
	default:
		fprintf(stderr, "unknown size unit: %c in '%s'\n", unit, str);
		usage();
		return 0;
	}
	return size;
}

static uint64_t
align_blocks_from_size(size_t nbytes)
{
	return ((nbytes + BLOCK_SIZE - 1) / BLOCK_SIZE + LBA_ALIGN - 1) / LBA_ALIGN * LBA_ALIGN;
}

static uint64_t
parse_part(
	int argc, char **argv, int *out_i,
	struct gpt_entry *entry,
	char **file,
	size_t *len
) {
	int i = *out_i;
	unsigned flags = 0;
	uint64_t current_blocks = 0;
	for(; i < argc && argv[i][0] != '-'; i++) {
		char *val = strchr(argv[i], '=');
		if(val == NULL) {
			fprintf(stderr, "bad partition option format: '%s'\n", argv[i]);
			goto err;
		}
		val[0] = '\0';
		val++;

		if(!strcmp(argv[i], "type")) {
			if(flags & PART_TYPE) {
				fprintf(stderr, "reduntant partition type: 'type=%s'\n", val);
				goto err;
			}
			flags |= PART_TYPE;

			if(!strcmp(val, "efisys")) {
				entry->type_guid = efisys;
			} else if(!strcmp(val, "linux")) {
				entry->type_guid = linuxfs;
			} else if(!strcmp(val, "root-x86-64")) {
				entry->type_guid = root_x86_64;
			} else if(!strcmp(val, "root-aarch64")) {
				entry->type_guid = root_aarch64;
			} else if(!strcmp(val, "swap")) {
				entry->type_guid = swap;
			} else if(!strcmp(val, "home")) {
				entry->type_guid = home;
			} else if(!strcmp(val, "random")) {
				guid_gen(&entry->type_guid);
			} else if(guid_from_str(&entry->type_guid, val)) {
				fprintf(stderr, "unknown partition type '%s'\n", val);
				goto err;
			}
		} else if(!strcmp(argv[i], "guid")) {
			if(flags & PART_GUID) {
				fprintf(stderr, "reduntant partition guid: 'type=%s'\n", val);
				goto err;
			}
			flags |= PART_GUID;

			if(guid_from_str(&entry->guid, val)) {
				fprintf(stderr, "couldn't parse partition guid '%s'\n", val);
				goto err;
			}
		} else if(!strcmp(argv[i], "size")) {
			if(flags & PART_SIZE) {
				fprintf(stderr, "reduntant partition size: 'size=%s'\n", val);
				goto err;
			}
			flags |= PART_SIZE;
			const size_t size = parse_size(val);
			const uint64_t blocks = align_blocks_from_size(size);
			if(blocks < current_blocks) {
				fprintf(stderr, "specified size (%s) too small for selected file (%s %zu)\n", val, *file, *len);
				goto err;
			}
			current_blocks = blocks;
		} else if(!strcmp(argv[i], "file")) {
			if(flags & PART_FILE) {
				fprintf(stderr, "reduntant partition file: 'file=%s'\n", val);
				goto err;
			}
			flags |= PART_FILE;

			struct stat st;
			int res = stat(val, &st);
			if(res != 0) {
				perror("stat");
				goto err;
			}
			if(st.st_size == 0) {
				fprintf(stderr, "empty partition file: '%s'\n", val);
				goto err;
			}
			const uint64_t blocks = align_blocks_from_size(st.st_size);
			if(blocks < current_blocks) {
				fprintf(stderr, "specified size (%zu) too small for selected file (%s %zu)\n", *len, val, st.st_size);
				goto err;
			}
			current_blocks = blocks;
			*file = val;
			*len = st.st_size;
		} else {
			fprintf(stderr, "unknown partition option: '%s=%s'\n", argv[i], val);
			goto err;
		}
	}
	if(current_blocks == 0) {
		fputs("file or size partition option required\n", stderr);
		goto err;
	}
	if(!(flags & PART_GUID)) {
		guid_gen(&entry->guid);
	}
	*out_i = i - 1;
	return current_blocks;
err:
	usage();
	return 0;
}

static int
gen(int argc, char *argv[])
{
	struct gpt_entry entries[16 * 1024 / sizeof(struct gpt_entry)] = {0};
	const uint64_t entries_nlba = (sizeof(entries) + BLOCK_SIZE - 1) / BLOCK_SIZE;
	const uint64_t first_lba = 2 + entries_nlba;
	uint64_t last_lba = (first_lba + LBA_ALIGN - 1) / LBA_ALIGN * LBA_ALIGN;
	const uint64_t align_gap = last_lba - first_lba;

	const char *disk_guid = NULL;
	char *part_path[LEN(entries)] = {0};
	size_t part_len[LEN(entries)] = {0};
	uint8_t npart = 0;

	if(argc < 1) {
		usage();
		return 1;
	}
	const char *disk_path = argv[0];
	argc--;
	argv++;

	for(int i = 0; i < argc; i++) {
		if(i + 1 == argc) {
			usage();
			return 1;
		} else if(!strcmp(argv[i], "--disk-guid")) {
			if(disk_guid != NULL) {
				fprintf(stderr, "reduntant --disk-guid flags\n");
				usage();
				return 1;
			}
			disk_guid = argv[++i];
		} else if(!strcmp(argv[i], "--part")) {
			npart++;
			if(npart > LEN(entries)) {
				fprintf(stderr, "too many partitions: %" PRIu8 " > %zu\n", npart, LEN(entries));
				usage();
				return 1;
			}
			struct gpt_entry *entry = &entries[npart - 1];
			i++;
			const uint64_t blocks = parse_part(
				argc, argv, &i,
				entry,
				&part_path[npart - 1],
				&part_len[npart - 1]
			);
			entry->first_lba = htole64(last_lba);
			entry->last_lba = htole64(last_lba += blocks - 1);
			last_lba += 1 + LBA_GAP;
		} else {
			usage();
			return 1;
		}
	}

	struct gpt_header header = {
		.signature = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
		.revision = {0, 0, 1, 0},
		.size = htole32(sizeof(struct gpt_header)),
		.current_lba = htole64(1),
		.backup_lba = htole64(last_lba + entries_nlba),
		.first_lba = htole64(first_lba),
		.last_lba = htole64(last_lba),
		.table_lba = htole64(2),
		.table_len = htole32(LEN(entries)),
		.entry_len = htole32(sizeof(struct gpt_entry)),
		.table_crc32 = htole32(crc32(0, (Bytef*)&entries, sizeof(entries))),
	};
	if(disk_guid == NULL) {
		guid_gen(&header.guid);
	} else if(guid_from_str(&header.guid, disk_guid)) {
		fprintf(stderr, "couldn't parse disk guid '%s'\n", disk_guid);
		usage();
		return 1;
	}
	const size_t reserved_size = BLOCK_SIZE - sizeof(struct gpt_header);
	header.crc32 = crc32(0, (Bytef*)&header, sizeof(header));

	const size_t size = last_lba * BLOCK_SIZE + sizeof(header) + reserved_size + sizeof(entries);
	const uint64_t lba_size = size / BLOCK_SIZE;
	int img = open(disk_path, O_WRONLY | O_TRUNC | O_CREAT, 0666);
	if(img == -1) {
		perror("open disk");
		return 1;
	}
	posix_fallocate(img, 0, size);

	struct mbr pmbr = {
		.boot_indicator = 0,
		.first_chs = {0, 2, 0},
		.os_type = 0xEE,
		.last_chs = chs_from_lba(lba_size),
		.first_lba = 1,
		.last_lba = htole32(lba_size > UINT32_MAX ? UINT32_MAX : (uint32_t)lba_size),
	};

	off_t offset = 0x1BE;
	if(writeall(img, &pmbr, sizeof(pmbr), &offset) != 0) {
		perror("write pmbr");
		return 1;
	}
	offset = BLOCK_SIZE - 2;
	if(writeall(img, &(uint8_t[2]){0x55 ,0xAA}, 2, &offset) != 0) {
		perror("write pmbr signature");
		return 1;
	}

	if(writeall(img, &header, sizeof(header), &offset) != 0) {
		perror("write header");
		return 1;
	}

	offset += reserved_size;

	if(writeall(img, &entries, sizeof(entries), &offset) != 0) {
		perror("write entries");
		return 1;
	}

	offset += align_gap * BLOCK_SIZE;

	for(uint32_t i = 0; i < npart; i++) {
		if(part_path[i] == NULL) {
			continue;
		}
		lseek(img, entries[i].first_lba * BLOCK_SIZE, SEEK_SET);
		int fd = open(part_path[i], O_RDONLY);
		size_t rest = part_len[i];
		ssize_t len;

		do {
			len = sendfile(img, fd, NULL, rest);
			if(len < 0) {
				perror("sendfile");
				return 1;
			}
			rest -= len;
		} while(rest > 0);
		close(fd);
	}

	offset = header.backup_lba * BLOCK_SIZE - sizeof(entries);
	// mirror
	if(writeall(img, &entries, sizeof(entries), &offset) != 0) {
		perror("write mirror entries");
		return 1;
	}

	// swap header location for the mirror
	uint64_t tmp = header.current_lba;
	header.current_lba = header.backup_lba;
	header.backup_lba = tmp;

	header.crc32 = 0;
	header.crc32 = htole32(crc32(0, (Bytef*)&header, sizeof(header)));
	if(writeall(img, &header, sizeof(header), &offset) != 0) {
		perror("write mirror header");
		return 1;
	}

	return 0;
}


int
main(int argc, char *argv[])
{
	if(argc < 2) {
		usage();
	}

	if(!strcmp(argv[1], "gen")) {
		return gen(argc - 2, argv + 2);
/*	} else if(!strcmp(argv[1], "mod")) {
		mod(argc - 2, argv + 2);
	} else if(!strcmp(argv[1], "extract")) {
		mod(argc - 2, argv + 2); */
	} else {
		usage();
	}

/*
	for(int i = 2; i < argc; i++) {
		if(i + 1 == argc) {
			usage();
		} else if(!strcmp(argv[i], "--disk")) {
			disk_path = argv[++i];
		} else if(!strcmp(argv[i], "--part")) {
		} else if(!strcmp(argv[i], "--with")) {
		} else {
			usage();
		}
	}

	for(int i = 2; i < argc; i++) {
		if(i + 1 == argc) {
			usage();
		} else if(!strcmp(argv[i], "--disk")) {
		} else if(!strcmp(argv[i], "--part")) {
		} else if(!strcmp(argv[i], "--out")) {
		} else {
			usage();
		}
	}
*/
	return 1;
}
