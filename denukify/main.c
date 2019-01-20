/*
 main.c (02.09.09)
 exFAT nuclear fallout cleaner-upper

 Free exFAT implementation.
 Copyright (C) 2011-2018  Andrew Nayenko
 Copyright (C) 2018-2019  Paul Ciarlo

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

#include "exfat.h"
#include "exfatfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

#define SECTOR_SIZE_BYTES ((size_t)512)
#define SECTORS_PER_CLUSTER ((size_t)512)
#define CLUSTER_COUNT ((cluster_t)0xE8DB79)
#define CLUSTER_COUNT_SECTORS (CLUSTER_COUNT * SECTORS_PER_CLUSTER)
#define FAT_CLUSTER_COUNT (CLUSTER_COUNT - 2)
#define CLUSTER_SIZE_BYTES (CLUSTER_COUNT_SECTORS * SECTOR_SIZE_BYTES)
#define DISK_SIZE_BYTES ((size_t)0x000003a352944000)
#define CLUSTER_HEAP_DISK_START_SECTOR ((size_t)0x8c400)
#define CLUSTER_HEAP_PARTITION_START_SECTOR ((size_t)0x283D8)
#define PARTITION_START_SECTOR ((size_t)0x64028)

static const size_t sector_size_bytes = SECTOR_SIZE_BYTES; // bytes 0x0200
static const size_t sectors_per_cluster = SECTORS_PER_CLUSTER; // 0x0200
static const size_t cluster_size_bytes = SECTOR_SIZE_BYTES * SECTORS_PER_CLUSTER;
static const size_t disk_size_bytes = DISK_SIZE_BYTES; // 0x0000040000000000; // 4TB
static const size_t cluster_heap_disk_start_sector = CLUSTER_HEAP_DISK_START_SECTOR;
static const size_t cluster_heap_partition_start_sector = CLUSTER_HEAP_PARTITION_START_SECTOR;
static const size_t partition_start_sector = PARTITION_START_SECTOR;
static const size_t cluster_count = CLUSTER_COUNT;

// rounded to sector boundary
struct exfat_file_allocation_table
{
    cluster_t entries[CLUSTER_COUNT];
    uint8_t padding[SECTOR_SIZE_BYTES - ((CLUSTER_COUNT<<2) % SECTOR_SIZE_BYTES)];
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_file_allocation_table) == (CLUSTER_COUNT << 2) + SECTOR_SIZE_BYTES - ((CLUSTER_COUNT << 2) % SECTOR_SIZE_BYTES) && (sizeof(struct exfat_file_allocation_table) % SECTOR_SIZE_BYTES) == 0 );

struct exfat_sector_t
{
    uint8_t data[SECTOR_SIZE_BYTES];
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_sector_t) == SECTOR_SIZE_BYTES);

struct exfat_cluster_t
{
    struct exfat_sector_t sectors[SECTORS_PER_CLUSTER];
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_cluster_t) == SECTOR_SIZE_BYTES*SECTORS_PER_CLUSTER);

//STATIC_ASSERT(sizeof(struct exfat_filesystem) == 1 + 24 + (0xE8DB79+2) );

struct exfat_entry_label volume_label =		/* volume label */
{
	.type = EXFAT_ENTRY_LABEL,                                                 //uint8_t type; /* EXFAT_ENTRY_LABEL */
	.length = 8,                                                               //uint8_t length; /* number of characters */
	.name = { 'E', 'l', 'e', 'm', 'e', 'n', 't', 's', 0, 0, 0, 0, 0, 0, 0 },   //le16_t name[EXFAT_ENAME_MAX]; /* in UTF-16LE */
};

void init_fat(struct exfat_file_allocation_table *fat) {
    const size_t sectors = sizeof(struct exfat_file_allocation_table) / SECTOR_SIZE_BYTES;
    fat->entries[0] = 0x0FFFFFF8; // Media descriptor hard drive
    fat->entries[1] = EXFAT_CLUSTER_END; // Label?
    fat->entries[2] = EXFAT_CLUSTER_END; // To be set to next cluster of the Allocation bitmap
    //fat->entries[3] = EXFAT_CLUSTER_END; // Up-Case table
    //fat->entries[4] = EXFAT_CLUSTER_END; // Root directory
    for (cluster_t c = 3; c < CLUSTER_COUNT; ++c) {
        fat->entries[c] = EXFAT_CLUSTER_FREE;
    }
}

struct exfat_volume_boot_record_t vbr = {
	.sb = {
    //    uint8_t jump[3];                /* 0x00 jmp and nop instructions */
        .jump = { 0xEB, 0x76, 0x90 },
    //    uint8_t oem_name[8];            /* 0x03 "EXFAT   " */
        .oem_name = { 'E', 'X', 'F', 'A', 'T', ' ', ' ', ' ' },
    //    uint8_t    __unused1[53];            /* 0x0B always 0 */
        .__unused1 = { 0 },
    //    le64_t sector_start;            /* 0x40 partition first sector */
        .sector_start = PARTITION_START_SECTOR,		// 409640
    //    le64_t sector_count;            /* 0x48 partition sectors count */
        .sector_count = 0x1D1B977B7, 	// 7813560247
    //    le32_t fat_sector_start;        /* 0x50 FAT first sector */
        .fat_sector_start = 0,
    //    le32_t fat_sector_count;        /* 0x54 FAT sectors count */
        .fat_sector_count = 0,
    //    le32_t cluster_sector_start;    /* 0x58 first cluster sector */
        .cluster_sector_start = CLUSTER_HEAP_PARTITION_START_SECTOR,
    //    le32_t cluster_count;            /* 0x5C total clusters count */
        .cluster_count = FAT_CLUSTER_COUNT,
    //    le32_t rootdir_cluster;            /* 0x60 first cluster of the root dir */
        .rootdir_cluster = 0,
    //    le32_t volume_serial;            /* 0x64 volume serial number */
        .volume_serial = 0xdeadbeef,
    //    struct                            /* 0x68 FS version */
    //    {
    //        uint8_t minor;
    //        uint8_t major;
    //    }
    //    version;
        .version = { 0, 1 },
    //    le16_t volume_state;            /* 0x6A volume state flags */
        .volume_state = 0,
    //    uint8_t sector_bits;            /* 0x6C sector size as (1 << n) */
        .sector_bits = 9,
    //    uint8_t spc_bits;                /* 0x6D sectors per cluster as (1 << n) */
        .spc_bits = 9,
    //    uint8_t fat_count;                /* 0x6E always 1 */
        .fat_count = 1,
    //    uint8_t drive_no;                /* 0x6F always 0x80 */
        .drive_no = 0x80,
    //    uint8_t allocated_percent;        /* 0x70 percentage of allocated space */
        .allocated_percent = 100,
    //    uint8_t __unused2[397];            /* 0x71 always 0 */
        .__unused2 = 0,
    //    le16_t boot_signature;            /* the value of 0xAA55 */
        .boot_signature = 0xAA55,
    },
    .bpb = { {
        .mebs = { { { 0 }, 0xAA55 } },
        .oem_params = { {0}, 0, 0, 0, 0, 0, 0, 0, 0, {0} },
        .zs = { { 0 } },
        .chksum = { 0 },
    } },
};

// Sector 10 is reserved, and is not currently defined.
// Sector 11 is a checksum sector, where every 4 byte integer is a 32 bit repeating checksum value
// of the previous 11 sectors. If anyone wanted to tamper with the VBR by changing values in the BPB
// or the boot code, like a boot sector virus infecting the VBR, then the checksum would have to be
// recalculated and sector 11 would need to be updated.
// The last 3 sectors of this 12 sector VBR (sectors 9, 10 and 11) do not contain signatures,
// the signatures are only used for sectors containing boot code and are in the first 9 sectors.

void update_chksum_sector(le32_t *chksum, const uint8_t *const buf, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        if (i != 106 && i != 107 && i != 112){
            chksum->__u32 = ((chksum->__u32 << 31) | (chksum->__u32 >> 1)) + (uint32_t)buf[i];
        }
    }
}

void restore_fat(struct exfat_dev *dev) {
    //exfat_seek(dev, 0, SEEK_SET);
    //exfat_write(dev, &sb, sizeof(struct exfat_super_block)); // 0
    update_chksum_sector(vbr.bpb[0].chksum.chksum, (const uint8_t *const)&vbr.sb, sizeof(struct exfat_super_block));
    for (int i = 0; i < 8; ++i) {
        //exfat_write(dev, &zero_mebs, sizeof(struct mebr_sector_t)); // 1-8
        //memcpy(vbr.mebs[i], )
        update_chksum_sector(vbr.bpb[0].chksum.chksum, (const uint8_t *const)&vbr.bpb[0].mebs[i], sizeof(struct mebr_sector_t));
    }
    //exfat_write(dev, zero_sector, sizeof(zero_sector)); // 9
	//exfat_write(dev, zero_sector, sizeof(zero_sector)); // 10
    //for (size_t i = 1; i < sizeof(vbr.bpb[0].chksum.chksum); ++i) {
    //    vbr.bpb[0].chksum.chksum[i] = vbr.bpb[0].chksum.chksum[0];
    //}
    //memcpy(vbr.bpb[1].chksum.chksum, vbr.bpb[0].chksum.chksum, sizeof(vbr.bpb[0].chksum.chksum));
    //exfat_write(dev, &chksum_sector, sizeof(struct chksum_sector_t)); // 11
}

cluster_t find_next_free_cluster(const struct exfat_file_allocation_table *const fat) {
    for (cluster_t c = 0; c < sizeof(fat->entries); ++c) {
        if (fat->entries[c] == EXFAT_CLUSTER_FREE) {
            return c;
        }
    }
    return -1;
}

static struct exfat_node node_prototype = {
    .parent 		= NULL, //struct exfat_node* parent;
    .child 			= NULL, //struct exfat_node* child;
    .next 			= NULL,	//struct exfat_node* next;
    .prev 			= NULL,	//struct exfat_node* prev;

    .references 	= 0,	//int references;
    .fptr_index 	= 0,	//uint32_t fptr_index;
    .fptr_cluster 	= 0,	//cluster_t fptr_cluster;
    .entry_offset	= 0,	//off_t entry_offset;
    .start_cluster	= 0,	//cluster_t start_cluster;
    .attrib 		= 0,	//uint16_t attrib;
    .continuations	= 0,	//uint8_t continuations;
    .is_contiguous	= 0,	//bool is_contiguous : 1;
    .is_cached		= 0,	//bool is_cached : 1;
    .is_dirty		= 0,	//bool is_dirty : 1;
    .is_unlinked	= 0,	//bool is_unlinked : 1;
    .size			= 0,	//uint64_t size;
    .mtime			= 0,	//time_t mtime
    .atime			= 0,	//time_t atime;
    .name			= {0},	//le16_t name[EXFAT_NAME_MAX + 1];
};

struct exfat_node* make_node() {
    struct exfat_node *node = malloc(sizeof(struct exfat_node));
    memcpy(node, &node_prototype, sizeof(struct exfat_node));
    return node;
}

void free_node(struct exfat_node *node) {
    free(node);
}

void dump_exfat_entry(union exfat_entries_t *ent, size_t cluster_ofs);

struct exfat_node* try_load_node_from_fde(struct exfat *fs, size_t fde_offset) {
    struct exfat_node *node = make_node();
    do {
        struct exfat_node_entry node_entry;
        ssize_t res = exfat_seek(fs->dev, fde_offset, SEEK_SET);
        size_t to_read = 3*sizeof(node_entry.efi);
        res = exfat_read(fs->dev, &node_entry, to_read);
        if (res != to_read) { break; }
        const int continuations_left = node_entry.fde.continuations - 3;
        to_read = continuations_left*sizeof(union exfat_entries_t);
        res = exfat_read(fs->dev, node_entry.u_continuations, to_read);
        if (res != to_read) { break; }

        // verify checksum
        //dump_exfat_entry((union exfat_entries_t *)&node_entries, fde_offset);
        const uint8_t continuations = node_entry.fde.continuations;
        if (continuations < 2 || continuations > 18) {
            fprintf(stderr, "bad number of continuations %d\n", continuations);
            break;
        }

        le16_t chksum = exfat_calc_checksum((const struct exfat_entry*)&node_entry, continuations + 1);

        if (chksum.__u16 != node_entry.fde.checksum.__u16) {
            fprintf(stderr, "bad checksum %04x vs. %04x\n", chksum.__u16, node_entry.fde.checksum.__u16);
            break;
        }

		// todo

        return node;
    } while(0);
//fail:
    free_node(node);
    return NULL;
}

void init_filesystem(struct exfat_dev *dev, struct exfat *fs) {
    fs->dev = dev;
    //fs->upcase =
    fs->sb	= &vbr.sb;

    fs->repair = EXFAT_REPAIR_NO;
}

struct exfat_entry_bitmap bmp_entry =            /* allocated clusters bitmap */
{
    .type = EXFAT_ENTRY_BITMAP,                    /* EXFAT_ENTRY_BITMAP */
    .bitmap_flags = 0,            /* bit 0: 0 = 1st cluster heap. 1 = 2nd cluster heap. */
    .__unknown1 = { 0 },
    .start_cluster = 2,
    .size = (FAT_CLUSTER_COUNT) / 8 + (FAT_CLUSTER_COUNT) % 8 /* in bytes = Ceil (Cluster count / 8 ) */
};

#define CLUSTER_HEAP_SIZE (BMAP_SIZE(FAT_CLUSTER_COUNT))
#define CLUSTER_HEAP_SIZE_BYTES (CLUSTER_HEAP_SIZE * sizeof(bitmap_t))

struct exfat_cluster_heap {
    bitmap_t allocation_flags[CLUSTER_HEAP_SIZE];
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_cluster_heap) == CLUSTER_HEAP_SIZE_BYTES);

int init_cluster_heap(struct exfat_file_allocation_table *fat, struct exfat_cluster_heap *heap) {
    // mark everything allocated so we don't accidentally overwrite any data
    memset(heap, 0xFF, sizeof(struct exfat_cluster_heap));

    const size_t bmp_size_clusters =
        bmp_entry.size.__u64 / cluster_size_bytes +
        bmp_entry.size.__u64 % cluster_size_bytes;
    cluster_t c = 2;
    for (size_t i = 1; i < bmp_size_clusters; ++i) {
        cluster_t next = find_next_free_cluster(fat);
        if (next == -1) {
            return -1;
        } else {
            fat->entries[c] = next;
            c = next;
        }
    }
    fat->entries[c] = EXFAT_CLUSTER_END;

    return 0;
}

struct exfat_upcase_table
{
    uint16_t upcase_entries[0xFFFF];
}
PACKED;
STATIC_ASSERT(sizeof(struct exfat_upcase_table) == 0xFFFF * sizeof(uint16_t)); //0x1FFFE

struct exfat_entry_upcase upcase_entry =    /* upper case translation table */
{
	.type          = EXFAT_ENTRY_UPCASE,   //uint8_t type; /* EXFAT_ENTRY_UPCASE */
	.__unknown1    = {0},                  //uint8_t __unknown1[3];
	.checksum      = {0},                  //le32_t checksum;
	.__unknown2    = {0},                  //.uint8_t __unknown2[12];
	.start_cluster = 0,                    //le32_t start_cluster;
	.size          = sizeof(struct exfat_upcase_table), //le64_t size; /* in bytes */
};

uint32_t upcase_checksum(const uint8_t *const data, size_t data_bytes)
{
    uint32_t chksum = 0;
    for (size_t i = 0; i < data_bytes; ++i) {
        chksum = ((chksum << 31) | (chksum >> 1)) + (uint32_t)data[i];
    }
    return chksum;
}

int init_upcase_table(struct exfat_file_allocation_table *fat, struct exfat_upcase_table *tbl) {
    for (int i = 0; i < sizeof(tbl->upcase_entries); ++i) {
        tbl->upcase_entries[i] = i;
    }
    // ASCII
    for (char ch = 'a'; ch <= 'z'; ++ch) {
        tbl->upcase_entries[ch] = ch ^ 0x20;
    }
    upcase_entry.checksum.__u32 = upcase_checksum((const uint8_t *const)tbl->upcase_entries, sizeof(tbl->upcase_entries));
    cluster_t c = find_next_free_cluster(fat);
    if (c == -1) {
        return -1;
    }
    upcase_entry.start_cluster.__u32 = c;
    fat->entries[c] = EXFAT_CLUSTER_END;
}

struct exfat_node_entry dir_prototype = {
    .fde = {
        .type           = EXFAT_ENTRY_FILE,
        .continuations  = 2,
        .checksum       = {0},
        .attrib         = {0},  /* combination of EXFAT_ATTRIB_xxx */
        .__unknown1     = {0},
        .crtime         = {0},
        .crdate         = {0},  /* creation date and time */
        .mtime          = {0},
        .mdate          = {0},  /* latest modification date and time */
        .atime          = {0},
        .adate          = {0},  /* latest access date and time */
        .crtime_cs      = 0,    /* creation time in cs (centiseconds) */
        .mtime_cs       = 0,    /* latest modification time in cs */
        .atime_cs       = 0,    /* latest access time in cs */
        .__unknown2     = {0},
    },
    .efi = {
        .type           = EXFAT_ENTRY_FILE_INFO,
        .flags          = 0,	/* combination of EXFAT_FLAG_xxx */
        .__unknown1     = 0,
        .name_length    = 0,
        .name_hash      = {0},
        .__unknown2     = {0},
        .valid_size     = {0},  /* in bytes, less or equal to size */
        .__unknown3     = {0},
        .start_cluster  = {0},
        .size           = {0},  /* in bytes */
    },
    .efn = {
        .type = EXFAT_ENTRY_FILE_NAME,
        .__unknown = 0,
        .name = {{0}},	/* in UTF-16LE */
    },
    .u_continuations = {
        {
            .ent = {
                .type = 0,  /* any of EXFAT_ENTRY_xxx */
                .data = {0},
            }
        },
    },
};

struct exfat_node_entry * init_directory(struct exfat_file_allocation_table *fat) {
    struct exfat_node_entry *dir = malloc(sizeof(struct exfat_node_entry));
    memcpy(dir, &dir_prototype, sizeof(struct exfat_node_entry));
    return dir;
}

void free_directory(struct exfat_node_entry *dir) {
    free(dir);
}

struct bptree_node
{
    struct exfat_entry_meta2 entry; // 32 bytes. file or directory represented by this node
    uint64_t offset;                // offset of this entry on disk
    // following are all offsets into the bptree_heap
    uint32_t parent_directory;      // directory containing this file or directory
    uint32_t next_fde;              // next file or directory in the same directory
    uint32_t prev_fde;              // previous file or directory in the same directory
    uint32_t first_directory_entry; // if this is a directory, offset of the bptree_node containing its first entry
    uint32_t child_nodes[8];        // offset of child nodes in the bptree structure
}
PACKED;
STATIC_ASSERT(sizeof(struct bptree_node) == 88);

struct bptree_node * alloc_bptree_heap() {
    // 4194304 entries, 369MiB starting size
    struct bptree_node *bptree_heap = malloc(sizeof(struct bptree_node) * (1 << 22));
    memset(bptree_heap, '\0', sizeof(struct bptree_node));
    return bptree_heap;
}

void free_bptree_heap(struct bptree_node *bptree_heap) {
    free(bptree_heap);
}

int reconstruct(struct exfat_dev *dev, FILE *logfile) {
    struct exfat fs;
    struct exfat_file_allocation_table fat;
    struct exfat_cluster_heap heap;
    struct exfat_upcase_table upcase;
    struct bptree_node *bptree_heap = alloc_bptree_heap();

    init_filesystem(dev, &fs);
    //fs->root	= make_node();
    //bptree_heap[0]

    init_fat(&fat);
    int ret = init_cluster_heap(&fat, &heap);
    if (ret != 0) {
        return ret;

    }
    init_upcase_table(&fat, &upcase);

    char * line = NULL;
    size_t len = 0;
    ssize_t read;

    while ((read = getline(&line, &len, logfile)) != -1) {
        printf("Retrieved line of length %zu:\n", read);
        printf("%s", line);

        // parse each line
    }

    // TODO write FAT to disk or log
    //free_node(fs->root);
    free_bptree_heap(bptree_heap);
    return 0;
}

static void usage(const char* prog)
{
    fprintf(stderr, "Usage: %s <device> <logfile>\n", prog);
    fprintf(stderr, "       %s -V\n", prog);
    exit(1);
}

int main(int argc, char* argv[])
{
    int opt, ret = 0;
    const char* options;
    const char* spec = NULL;
    struct exfat_dev *dev = NULL;
    FILE *logfile = NULL;

    fprintf(stderr, "%s %s\n", argv[0], VERSION);

    while ((opt = getopt(argc, argv, "V")) != -1)
    {
        switch (opt)
        {
            case 'V':
                fprintf(stderr, "Copyright (C) 2011-2018  Andrew Nayenko\n");
                fprintf(stderr, "Copyright (C) 2018-2019  Paul Ciarlo\n");
                return 0;
            default:
                usage(argv[0]);
                break;
        }
    }
    if (argc - optind != 2)
        usage(argv[0]);
    spec = argv[optind];
    fprintf(stderr, "Reconstructing nuked file system on %s.\n", spec);
    dev = exfat_open(spec, EXFAT_MODE_RW);

    do {
        if (dev == NULL) {
            ret = errno;
            fprintf(stderr, "exfat_open(%s) failed: %s\n", spec, strerror(ret));
            break;
        }

        spec = argv[optind+1];
        logfile = fopen(spec, "r");
        if (logfile == NULL) {
            ret = errno;
            fprintf(stderr, "fopen(%s) failed: %s\n", spec, strerror(ret));
            break;
        }

        ret = reconstruct(dev, logfile);
        if (ret != 0) {
            fprintf(stderr, "reconstruct() returned error: %s\n", strerror(ret));
        }
    } while (0);

    exfat_close(dev);
    fclose(logfile);

    return ret;
}
