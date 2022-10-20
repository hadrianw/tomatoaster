#define _GNU_SOURCE
#include <endian.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
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

// 3210-54-76-89-ABCDEF
// 3210547689ABCDEF
// 0123456789ABCDE
// 0123-45-67-89-ABCDEF

/*
struct guid
guid_from_str(char str[36])
{
	char *chr = str;
	unsigned char index = {3, 2, 1, 0, 0xFF, 5, 4, 0xFF, 7, 6, 0xFF, 8, 9, 0xFF, 10, 11, 12, 13, 14, 15};
	for(int i = 0; i < sizeof(index); i++) {
		if(index[i] == 0xFF) {
			chr++;
			continue;
		}
		
		chr += 2
	}
}
*/

// Linux filesystem data  0FC63DAF-8483-4772-8E79-3D69D8477DE4
struct guid linuxfs = {{ 0xAF, 0x3D, 0xC6, 0x0F, 0x83, 0x84, 0x72, 0x47, 0x8E, 0x79, 0x3D, 0x69, 0xD8, 0x47, 0x7D, 0xE4 }};
// Root partition x86-64  4F68BCE3-E8CD-4DB1-96E7-FBCAF984B709
// Root partition aarch64 B921B045-1DF0-41C3-AF44-4C6F280D3FAE
// Swap                   0657FD6D-A4AB-43C4-84E5-0933C84B4F4F
// Home                   933AC7E1-2EB4-4F13-B844-0E14E2AEF915

struct guid
guid_gen(void)
{
	struct guid guid;
	getentropy(&guid, sizeof(guid));
	// version 4
	guid.data[7] = 0x40 | (guid.data[6] & 0xF);
	// RFC4122
	guid.data[8] = 0x80 | (guid.data[8] & 0x3F);
	return guid;
}

ssize_t
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

int
main(int argc, char *argv[])
{
	if(argc < 3) {
		fprintf(stderr, "usage: part0 [part1 part2 ...] disk.img\n");
		return 1;
	}
	uint32_t npart = argc - 2;
	if(npart > 128) {
		fprintf(stderr, "too many parts\n");
		return 1;
	}
	char **part = &argv[1];

	int img = open(argv[argc-1], O_WRONLY | O_TRUNC | O_CREAT, 0666);
	if(img == -1) {
		perror("open img");
		return 1;
	}

	struct gpt_entry entries[0x4000 / sizeof(struct gpt_entry)] = {0};
	size_t part_len[npart];

	uint64_t entries_nlba = (sizeof(entries) + BLOCK_SIZE - 1) / BLOCK_SIZE;
	uint64_t first_lba = 2 + entries_nlba;
	uint64_t last_lba = (first_lba + LBA_ALIGN - 1) / LBA_ALIGN * LBA_ALIGN;
	uint64_t align_gap = last_lba - first_lba;
	for(uint32_t i = 0; i < npart; i++) {
		struct stat st;
		stat(part[i], &st);
		if(st.st_size == 0) {
			fprintf(stderr, "empty part\n");
			return 1;
		}
		part_len[i] = st.st_size;
		uint64_t nlba = ((st.st_size - 1) / BLOCK_SIZE + LBA_ALIGN - 1) / LBA_ALIGN * LBA_ALIGN;
		entries[i] = (struct gpt_entry){
			.type_guid = linuxfs,
			.guid = guid_gen(),
			.first_lba = htole64(last_lba),
			.last_lba = htole64(last_lba += nlba - 1),
		};
		last_lba += 1 + LBA_GAP;
	}

	struct gpt_header header = {
		.signature = {'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T'},
		.revision = {0, 0, 1, 0},
		.size = htole32(sizeof(struct gpt_header)),
		.current_lba = htole64(1),
		.backup_lba = htole64(last_lba + entries_nlba),
		.first_lba = htole64(first_lba),
		.last_lba = htole64(last_lba),
		.guid = guid_gen(),
		.table_lba = htole64(2),
		.table_len = htole32(LEN(entries)),
		.entry_len = htole32(sizeof(struct gpt_entry)),
		.table_crc32 = htole32(crc32(0, (Bytef*)&entries, sizeof(entries))),
	};
	size_t reserved_size = BLOCK_SIZE - sizeof(struct gpt_header);
	header.crc32 = crc32(0, (Bytef*)&header, sizeof(header));

	size_t size = last_lba * BLOCK_SIZE + sizeof(header) + reserved_size + sizeof(entries);
	uint64_t lba_size = size / BLOCK_SIZE;
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
		perror("write mirror entries");
		return 1;
	}

	offset += align_gap * BLOCK_SIZE;

	for(uint32_t i = 0; i < npart; i++) {
		lseek(img, entries[i].first_lba * BLOCK_SIZE, SEEK_SET);
		int fd = open(part[i], O_RDONLY);
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
	// swap
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
