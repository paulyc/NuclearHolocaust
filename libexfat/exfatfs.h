/*
	exfatfs.h (29.08.09)
	Definitions of structures and constants used in exFAT file system.

	Free exFAT implementation.
	Copyright (C) 2010-2018  Andrew Nayenko

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#ifndef EXFATFS_H_INCLUDED
#define EXFATFS_H_INCLUDED

#include "byteorder.h"
#include "compiler.h"

#define EXFAT_NAME_MAX 255
/* UTF-16 encodes code points up to U+FFFF as single 16-bit code units.
 UTF-8 uses up to 3 bytes (i.e. 8-bit code units) to encode code points
 up to U+FFFF. One additional character is for null terminator. */
#define EXFAT_UTF8_NAME_BUFFER_MAX (EXFAT_NAME_MAX * 3 + 1)
#define EXFAT_UTF8_ENAME_BUFFER_MAX (EXFAT_ENAME_MAX * 3 + 1)

#define SECTOR_SIZE(sb) (1 << (sb).sector_bits)
#define CLUSTER_SIZE(sb) (SECTOR_SIZE(sb) << (sb).spc_bits)
#define CLUSTER_INVALID(sb, c) ((c) < EXFAT_FIRST_DATA_CLUSTER || \
(c) - EXFAT_FIRST_DATA_CLUSTER >= le32_to_cpu((sb).cluster_count))

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define DIV_ROUND_UP(x, d) (((x) + (d) - 1) / (d))
#define ROUND_UP(x, d) (DIV_ROUND_UP(x, d) * (d))

#define BMAP_SIZE(count) (ROUND_UP(count, sizeof(bitmap_t) * 8) / 8)
#define BMAP_BLOCK(index) ((index) / sizeof(bitmap_t) / 8)
#define BMAP_MASK(index) ((bitmap_t) 1 << ((index) % (sizeof(bitmap_t) * 8)))
#define BMAP_GET(bitmap, index) \
((bitmap)[BMAP_BLOCK(index)] & BMAP_MASK(index))
#define BMAP_SET(bitmap, index) \
((bitmap)[BMAP_BLOCK(index)] |= BMAP_MASK(index))
#define BMAP_CLR(bitmap, index) \
((bitmap)[BMAP_BLOCK(index)] &= ~BMAP_MASK(index))

#define EXFAT_REPAIR(hook, ef, ...) \
(exfat_ask_to_fix(ef) && exfat_fix_ ## hook(ef, __VA_ARGS__))

typedef uint32_t cluster_t;		/* cluster number */

#define EXFAT_FIRST_DATA_CLUSTER 2
#define EXFAT_LAST_DATA_CLUSTER 0xfffffff6

#define EXFAT_CLUSTER_FREE         0 /* free cluster */
#define EXFAT_CLUSTER_BAD 0xfffffff7 /* cluster contains bad sector */
#define EXFAT_CLUSTER_END 0xffffffff /* final cluster of file or directory */

#define EXFAT_STATE_MOUNTED 2

struct exfat_super_block
{
	uint8_t jump[3];				/* 0x00 jmp and nop instructions */
	uint8_t oem_name[8];			/* 0x03 "EXFAT   " */
	uint8_t	__unused1[53];			/* 0x0B always 0 */
	le64_t sector_start;			/* 0x40 partition first sector */
	le64_t sector_count;			/* 0x48 partition sectors count */
	le32_t fat_sector_start;		/* 0x50 FAT first sector */
	le32_t fat_sector_count;		/* 0x54 FAT sectors count */
	le32_t cluster_sector_start;	/* 0x58 first cluster sector */
	le32_t cluster_count;			/* 0x5C total clusters count */
	le32_t rootdir_cluster;			/* 0x60 first cluster of the root dir */
	le32_t volume_serial;			/* 0x64 volume serial number */
	struct							/* 0x68 FS version */
	{
		uint8_t minor;
		uint8_t major;
	}
	version;
	le16_t volume_state;			/* 0x6A volume state flags */
	uint8_t sector_bits;			/* 0x6C sector size as (1 << n) */
	uint8_t spc_bits;				/* 0x6D sectors per cluster as (1 << n) */
	uint8_t fat_count;				/* 0x6E always 1 */
	uint8_t drive_no;				/* 0x6F always 0x80 */
	uint8_t allocated_percent;		/* 0x70 percentage of allocated space */
	uint8_t __unused2[397];			/* 0x71 always 0 */
	le16_t boot_signature;			/* the value of 0xAA55 */
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_super_block) == 512);

// Main Extended Boot Region - 8 sectors, generally not used
struct mebr_sector {
    uint8_t zero[510];
    le16_t boot_signature;
}
PACKED;
STATIC_ASSERT(sizeof(struct mebr_sector) == 512);

// The Main Extended Boot Region takes up the next 8 sectors, even when not used.
//static struct mebr_sector_t zero_mebs = { { 0 }, 0xAA55 };

//The next sector in the VBR (sector 9) is the OEM parameters record.
// Since this record really doesn’t exist yet (it is all zeros in the file systems that were
// generated), there is little analysis that can be done at this time. The patent specifies this
// table as 10 fields of 48 bytes, the first 16 bytes of each field is the GUID and the remaining
// 32 bytes are the parameters, but no additional definition is provided.
// The entries are not sorted, and it is possible that the first 9 are empty and the last has data,
// so the specification states that all 10 entries should be searched. This sector is filled out by
// the media manufacturer at the factory and a format operation is not supposed to erase this sector
// with the exception of a secure wipe of the media.
struct oem_parameters
{
    le32_t OemParameterType[4]; //GUID. Value is OEM_FLASH_PARAMETER_GUID
    le32_t EraseBlockSize; //Erase block size in bytes
    le32_t PageSize;
    le32_t NumberOfSpareBlocks;
    le32_t tRandomAccess; //Random Access Time in nanoseconds
    le32_t tProgram; //Program time in nanoseconds
    le32_t tReadCycle; // Serial read cycle time in nanoseconds
    le32_t tWriteCycle; // Write Cycle time in nanoseconds
    le32_t Reserved;
    uint8_t Padding[464];
}
PACKED;
STATIC_ASSERT(sizeof(struct oem_parameters) == 512);

//static struct oem_parameters_t zero_params = { {0}, 0, 0, 0, 0, 0, 0, 0, 0, {0} };

struct zero_sector {
    uint8_t zero[512];
}
PACKED;
STATIC_ASSERT(sizeof(struct zero_sector) == 512);

//static struct zero_sector_t zero_sector = {0};

struct chksum_sector {
    le32_t chksum[128];
}
PACKED;
STATIC_ASSERT(sizeof(struct chksum_sector) == 512);

struct exfat_bios_parameter_block {
    struct mebr_sector mebs[8];
    struct oem_parameters oem_params;
    struct zero_sector zs[2];
    struct chksum_sector chksum;
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_bios_parameter_block) == 12*512);

struct exfat_volume_boot_record {
	struct exfat_super_block sb;
    struct exfat_bios_parameter_block bpb[2]; // one copy and a backup
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_volume_boot_record) == 25*512);

#define EXFAT_ENTRY_VALID     0x80
#define EXFAT_ENTRY_CONTINUED 0x40
#define EXFAT_ENTRY_OPTIONAL  0x20

#define EXFAT_ENTRY_BITMAP    (0x01 | EXFAT_ENTRY_VALID)
#define EXFAT_ENTRY_UPCASE    (0x02 | EXFAT_ENTRY_VALID)
#define EXFAT_ENTRY_LABEL     (0x03 | EXFAT_ENTRY_VALID)
#define EXFAT_ENTRY_FILE      (0x05 | EXFAT_ENTRY_VALID)
#define EXFAT_ENTRY_FILE_INFO (0x00 | EXFAT_ENTRY_VALID | EXFAT_ENTRY_CONTINUED)
#define EXFAT_ENTRY_FILE_NAME (0x01 | EXFAT_ENTRY_VALID | EXFAT_ENTRY_CONTINUED)
#define EXFAT_ENTRY_FILE_TAIL (0x00 | EXFAT_ENTRY_VALID \
                                    | EXFAT_ENTRY_CONTINUED \
                                    | EXFAT_ENTRY_OPTIONAL)

struct exfat_entry					/* common container for all entries */
{
	uint8_t type;					/* any of EXFAT_ENTRY_xxx */
	uint8_t data[31];
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_entry) == 32);

#define EXFAT_ENAME_MAX 15

struct exfat_entry_bitmap			/* allocated clusters bitmap */
{
	uint8_t type;					/* EXFAT_ENTRY_BITMAP */
	uint8_t bitmap_flags;			/* bit 0: 0 = 1st cluster heap. 1 = 2nd cluster heap. */
	uint8_t __unknown1[18];
	le32_t start_cluster;
    le64_t size;					/* in bytes = Ceil (Cluster count / 8 ) */
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_entry_bitmap) == 32);

#define EXFAT_UPCASE_CHARS 0x10000

struct exfat_entry_upcase			/* upper case translation table */
{
	uint8_t type;					/* EXFAT_ENTRY_UPCASE */
	uint8_t __unknown1[3];
	le32_t checksum;
	uint8_t __unknown2[12];
	le32_t start_cluster;
	le64_t size;					/* in bytes */
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_entry_upcase) == 32);

struct exfat_entry_label			/* volume label */
{
	uint8_t type;					/* EXFAT_ENTRY_LABEL */
	uint8_t length;					/* number of characters */
	le16_t name[EXFAT_ENAME_MAX];	/* in UTF-16LE */
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_entry_label) == 32);

#define EXFAT_ATTRIB_RO     0x01
#define EXFAT_ATTRIB_HIDDEN 0x02
#define EXFAT_ATTRIB_SYSTEM 0x04
#define EXFAT_ATTRIB_VOLUME 0x08
#define EXFAT_ATTRIB_DIR    0x10
#define EXFAT_ATTRIB_ARCH   0x20

struct exfat_entry_meta1			/* file or directory info (part 1) */
{
	uint8_t type;					/* EXFAT_ENTRY_FILE */
	uint8_t continuations;
	le16_t checksum;
	le16_t attrib;					/* combination of EXFAT_ATTRIB_xxx */
	uint8_t __unknown1[2];
	le16_t crtime, crdate;			/* creation date and time */
	le16_t mtime, mdate;			/* latest modification date and time */
	le16_t atime, adate;			/* latest access date and time */
	uint8_t crtime_cs;				/* creation time in cs (centiseconds) */
	uint8_t mtime_cs;				/* latest modification time in cs */
	uint8_t atime_cs;               /* latest access time in cs */
	uint8_t __unknown2[9];
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_entry_meta1) == 32);

#define EXFAT_FLAG_ALWAYS1		(1u << 0)
#define EXFAT_FLAG_CONTIGUOUS	(1u << 1)

struct exfat_entry_meta2			/* file or directory info (part 2) */
{
	uint8_t type;					/* EXFAT_ENTRY_FILE_INFO */
	uint8_t flags;					/* combination of EXFAT_FLAG_xxx */
	uint8_t __unknown1;
	uint8_t name_length;
	le16_t name_hash;
	uint8_t __unknown2[2];
	le64_t valid_size;				/* in bytes, less or equal to size */
	uint8_t __unknown3[4];
	le32_t start_cluster;
	le64_t size;					/* in bytes */
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_entry_meta2) == 32);

struct exfat_entry_name				/* file or directory name */
{
	uint8_t type;					/* EXFAT_ENTRY_FILE_NAME */
	uint8_t __unknown;
	le16_t name[EXFAT_ENAME_MAX];	/* in UTF-16LE */
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_entry_name) == 32);

union exfat_entries_t
{
    struct exfat_entry ent;
    struct exfat_entry_bitmap bitmap;
    struct exfat_entry_upcase upcase;
    struct exfat_entry_label label;
    struct exfat_entry_meta1 meta1;
    struct exfat_entry_meta2 meta2;
    struct exfat_entry_name name;
}
PACKED;
STATIC_ASSERT(sizeof(union exfat_entries_t) == 32);

struct exfat_node_entry
{
    struct exfat_entry_meta1 fde;
    struct exfat_entry_meta2 efi;
    struct exfat_entry_name efn;
    union exfat_entries_t u_continuations[16]; // up to 18 minus the efi and efn
}
PACKED;

#endif /* ifndef EXFATFS_H_INCLUDED */
