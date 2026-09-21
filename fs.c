#include <stdint.h>

#include "block.h"

extern void kernel_debug(const char *s);

#define FS_MAGIC           0x4E49434F53465331ULL
#define FS_VERSION         1

#define FS_SECTOR_SIZE     512
#define FS_INODE_SIZE      64
#define FS_INODE_COUNT     128

#define FS_SUPERBLOCK      0
#define FS_INODE_START     1
#define FS_INODE_SECTORS   16
#define FS_ROOT_DIR        17
#define FS_DATA_START      18

#define FS_TYPE_FREE       0
#define FS_TYPE_FILE       1
#define FS_TYPE_DIR        2

#define FS_NAME_SIZE       56
#define FS_DIR_ENTRY_SIZE  64
#define FS_ROOT_ENTRIES    8

typedef struct {
    uint64_t magic;
    uint32_t version;
    uint32_t sector_size;
    uint32_t inode_start;
    uint32_t inode_count;
    uint32_t data_start;
    uint32_t root_inode;
    uint8_t reserved[480];
} FS_Superblock;

typedef struct {
    uint8_t type;
    uint8_t reserved0[3];

    uint32_t size;
    uint32_t start_sector;
    uint32_t sector_count;

    uint8_t reserved[48];
} FS_Inode;

typedef struct {
    uint32_t inode;
    uint8_t type;
    uint8_t reserved[3];
    char name[56];
} FS_DirEntry;

static FS_Superblock superblock;
static uint8_t sector_buffer[FS_SECTOR_SIZE];

static void fs_zero(void *buffer, uint32_t size)
{
    uint8_t *p = (uint8_t *)buffer;

    for (uint32_t i = 0; i < size; i++)
        p[i] = 0;
}

static void fs_copy(void *dst, const void *src, uint32_t size)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;

    for (uint32_t i = 0; i < size; i++)
        d[i] = s[i];
}

static int fs_string_equals(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a != *b)
            return 0;

        a++;
        b++;
    }

    return *a == 0 && *b == 0;
}

static int fs_string_copy(char *dst, const char *src)
{
    uint32_t i = 0;

    while (src[i] && i < FS_NAME_SIZE - 1) {
        dst[i] = src[i];
        i++;
    }

    dst[i] = 0;

    return src[i] == 0;
}

static int fs_read_inode(uint32_t inode_number, FS_Inode *inode)
{
    if (inode_number >= FS_INODE_COUNT)
        return 0;

    uint32_t byte_offset =
        inode_number * FS_INODE_SIZE;

    uint32_t sector =
        FS_INODE_START + byte_offset / FS_SECTOR_SIZE;

    uint32_t offset =
        byte_offset % FS_SECTOR_SIZE;

    if (!block_read_sector(sector, sector_buffer))
        return 0;

    fs_copy(inode, sector_buffer + offset, FS_INODE_SIZE);

    return 1;
}

static int fs_write_inode(uint32_t inode_number, const FS_Inode *inode)
{
    if (inode_number >= FS_INODE_COUNT)
        return 0;

    uint32_t byte_offset =
        inode_number * FS_INODE_SIZE;

    uint32_t sector =
        FS_INODE_START + byte_offset / FS_SECTOR_SIZE;

    uint32_t offset =
        byte_offset % FS_SECTOR_SIZE;

    if (!block_read_sector(sector, sector_buffer))
        return 0;

    fs_copy(sector_buffer + offset, inode, FS_INODE_SIZE);

    return block_write_sector(sector, sector_buffer);
}

static int fs_find_free_inode(uint32_t *result)
{
    FS_Inode inode;

    for (uint32_t i = 1; i < FS_INODE_COUNT; i++) {

        if (!fs_read_inode(i, &inode))
            return 0;

        if (inode.type == FS_TYPE_FREE) {
            *result = i;
            return 1;
        }
    }

    return 0;
}

static int fs_find_entry(const char *name, uint32_t *inode_number)
{
    if (!block_read_sector(FS_ROOT_DIR, sector_buffer))
        return 0;

    FS_DirEntry *entries =
        (FS_DirEntry *)sector_buffer;

    for (uint32_t i = 0; i < FS_ROOT_ENTRIES; i++) {

        if (entries[i].inode == 0)
            continue;

        if (fs_string_equals(entries[i].name, name)) {
            *inode_number = entries[i].inode;
            return 1;
        }
    }

    return 0;
}

static int fs_add_entry(
    uint32_t inode_number,
    uint8_t type,
    const char *name
)
{
    if (!block_read_sector(FS_ROOT_DIR, sector_buffer))
        return 0;

    FS_DirEntry *entries =
        (FS_DirEntry *)sector_buffer;

    for (uint32_t i = 0; i < FS_ROOT_ENTRIES; i++) {

        if (entries[i].inode != 0)
            continue;

        entries[i].inode = inode_number;
        entries[i].type = type;

        fs_zero(entries[i].name, FS_NAME_SIZE);

        if (!fs_string_copy(entries[i].name, name))
            return 0;

        return block_write_sector(
            FS_ROOT_DIR,
            sector_buffer
        );
    }

    return 0;
}

static int fs_format(void)
{
    FS_Inode inode;

    fs_zero(&superblock, sizeof(superblock));

    superblock.magic = FS_MAGIC;
    superblock.version = FS_VERSION;
    superblock.sector_size = FS_SECTOR_SIZE;
    superblock.inode_start = FS_INODE_START;
    superblock.inode_count = FS_INODE_COUNT;
    superblock.data_start = FS_DATA_START;
    superblock.root_inode = 0;

    if (!block_write_sector(
            FS_SUPERBLOCK,
            &superblock))
        return 0;

    fs_zero(sector_buffer, FS_SECTOR_SIZE);

    for (uint32_t i = 0;
         i < FS_INODE_SECTORS;
         i++) {

        if (!block_write_sector(
                FS_INODE_START + i,
                sector_buffer))
            return 0;
    }

    fs_zero(&inode, sizeof(inode));

    inode.type = FS_TYPE_DIR;
    inode.size = 0;
    inode.start_sector = FS_ROOT_DIR;
    inode.sector_count = 1;

    if (!fs_write_inode(0, &inode))
        return 0;

    fs_zero(sector_buffer, FS_SECTOR_SIZE);

    return block_write_sector(
        FS_ROOT_DIR,
        sector_buffer
    );
}

int fs_init(void)
{
    FS_Inode root;

    kernel_debug("[FS] READ SUPERBLOCK...\r\n");

    kernel_debug("[FS] sector_buffer=");
    kernel_debug("\r\n");

    if (!block_read_sector(FS_SUPERBLOCK, sector_buffer)) {
        kernel_debug("[FS] SUPERBLOCK READ FAILED\r\n");
        return 0;
    }

    kernel_debug("[FS] MAGIC AFTER READ: ");

    {
        uint8_t *raw = sector_buffer;
        char hex[] = "0123456789ABCDEF";
        char out[3];

        for (int i = 0; i < 8; i++) {
            out[0] = hex[(raw[i] >> 4) & 0xF];
            out[1] = hex[raw[i] & 0xF];
            out[2] = 0;
            kernel_debug(out);
            kernel_debug(" ");
        }

        kernel_debug("\r\n");
    }

    fs_copy(&superblock, sector_buffer, sizeof(superblock));

    kernel_debug("[FS] SUPERBLOCK COPIED\r\n");

    if (superblock.magic != FS_MAGIC) {
        kernel_debug("[FS] BAD MAGIC\r\n");
        return fs_format();
    }

    if (superblock.version != FS_VERSION) {
        kernel_debug("[FS] BAD VERSION\r\n");
        return fs_format();
    }

    if (superblock.sector_size != FS_SECTOR_SIZE) {
        kernel_debug("[FS] BAD SECTOR SIZE\r\n");
        return fs_format();
    }

    if (superblock.inode_start != FS_INODE_START) {
        kernel_debug("[FS] BAD INODE START\r\n");
        return fs_format();
    }

    if (superblock.inode_count != FS_INODE_COUNT) {
        kernel_debug("[FS] BAD INODE COUNT\r\n");
        return fs_format();
    }

    if (superblock.data_start != FS_DATA_START) {
        kernel_debug("[FS] BAD DATA START\r\n");
        return fs_format();
    }

    if (superblock.root_inode != 0) {
        kernel_debug("[FS] BAD ROOT INODE\r\n");
        return fs_format();
    }

    kernel_debug("[FS] SUPERBLOCK VALID\r\n");

    if (!fs_read_inode(0, &root)) {
        kernel_debug("[FS] ROOT INODE READ FAILED -> FORMAT\r\n");
        return fs_format();
    }

    kernel_debug("[FS] ROOT INODE type=");
    kernel_debug(root.type == FS_TYPE_DIR ? "DIR" :
                 root.type == FS_TYPE_FILE ? "FILE" : "OTHER");
    kernel_debug("\r\n");

    if (root.type != FS_TYPE_DIR ||
        root.start_sector != FS_ROOT_DIR ||
        root.sector_count != 1) {

        kernel_debug("[FS] ROOT INODE INVALID -> FORMAT\r\n");
        return fs_format();
    }

    if (!block_read_sector(FS_ROOT_DIR, sector_buffer)) {
        kernel_debug("[FS] ROOT DIR READ FAILED -> FORMAT\r\n");
        return fs_format();
    }

    kernel_debug("[FS] ROOT INODE VALID, NO FORMAT\r\n");

    return 1;
}

int fs_touch(const char *name)
{
    uint32_t inode_number;
    FS_Inode inode;

    if (!name || !name[0])
        return 0;

    if (fs_find_entry(name, &inode_number))
        return 1;

    if (!fs_find_free_inode(&inode_number))
        return 0;

    fs_zero(&inode, sizeof(inode));

    inode.type = FS_TYPE_FILE;
    inode.size = 0;
    inode.start_sector = 0;
    inode.sector_count = 0;

    if (!fs_write_inode(inode_number, &inode))
        return 0;

    return fs_add_entry(
        inode_number,
        FS_TYPE_FILE,
        name
    );
}

int fs_mkdir(const char *name)
{
    uint32_t inode_number;
    FS_Inode inode;

    if (!name || !name[0])
        return -1;

    if (fs_find_entry(name, &inode_number))
        return -2;

    if (!fs_find_free_inode(&inode_number))
        return -3;

    fs_zero(&inode, sizeof(inode));

    inode.type = FS_TYPE_DIR;
    inode.size = 0;
    inode.start_sector = 0;
    inode.sector_count = 0;

    if (!fs_write_inode(inode_number, &inode))
        return -4;

    if (!fs_add_entry(
            inode_number,
            FS_TYPE_DIR,
            name))
        return -5;

    return 0;
}

#define FS_TOTAL_SECTORS 65536
#define FS_TEST_SECTOR   65535

static int fs_sector_used(uint32_t sector, uint32_t ignore_inode)
{
    FS_Inode inode;

    for (uint32_t i = 1; i < FS_INODE_COUNT; i++) {
        if (i == ignore_inode)
            continue;

        if (!fs_read_inode(i, &inode))
            return 1;

        if (inode.type != FS_TYPE_FILE)
            continue;

        if (inode.sector_count == 0)
            continue;

        uint32_t start = inode.start_sector;
        uint32_t end = start + inode.sector_count;

        if (sector >= start && sector < end)
            return 1;
    }

    return 0;
}

static int fs_find_free_sectors(
    uint32_t count,
    uint32_t ignore_inode,
    uint32_t *result
)
{
    if (count == 0)
        return 0;

    for (uint32_t start = FS_DATA_START;
         start + count <= FS_TEST_SECTOR;
         start++) {

        int free = 1;

        for (uint32_t i = 0; i < count; i++) {
            if (fs_sector_used(
                    start + i,
                    ignore_inode)) {
                free = 0;
                break;
            }
        }

        if (free) {
            *result = start;
            return 1;
        }
    }

    return 0;
}

int fs_write_file(
    const char *name,
    const char *data,
    uint32_t size
)
{
    uint32_t inode_number;
    FS_Inode inode;

    if (!name || !name[0])
        return 0;

    if (!data && size != 0)
        return 0;

    if (size > 0xFFFFFFFFU)
        return 0;

    if (fs_find_entry(name, &inode_number)) {
        if (!fs_read_inode(inode_number, &inode))
            return 0;

        if (inode.type != FS_TYPE_FILE)
            return 0;
    } else {
        if (!fs_find_free_inode(&inode_number))
            return 0;

        fs_zero(&inode, sizeof(inode));
        inode.type = FS_TYPE_FILE;

        if (!fs_write_inode(inode_number, &inode))
            return 0;

        if (!fs_add_entry(
                inode_number,
                FS_TYPE_FILE,
                name))
            return 0;
    }

    uint32_t sector_count =
        (size + FS_SECTOR_SIZE - 1) /
        FS_SECTOR_SIZE;

    uint32_t start_sector = 0;

    if (sector_count != 0) {
        if (!fs_find_free_sectors(
                sector_count,
                inode_number,
                &start_sector))
            return 0;

        for (uint32_t i = 0;
             i < sector_count;
             i++) {

            fs_zero(
                sector_buffer,
                FS_SECTOR_SIZE
            );

            uint32_t offset =
                i * FS_SECTOR_SIZE;

            uint32_t remaining =
                size - offset;

            uint32_t amount =
                remaining > FS_SECTOR_SIZE
                ? FS_SECTOR_SIZE
                : remaining;

            if (amount != 0) {
                fs_copy(
                    sector_buffer,
                    data + offset,
                    amount
                );
            }

            if (!block_write_sector(
                    start_sector + i,
                    sector_buffer))
                return 0;
        }
    }
    inode.size = size;
    inode.start_sector = start_sector;
    inode.sector_count = sector_count;

    return fs_write_inode(
        inode_number,
        &inode
    );
}

int fs_read_file(
    const char *name,
    char *buffer,
    uint32_t buffer_size
)
{
    uint32_t inode_number;
    FS_Inode inode;

    if (!name || !name[0])
        return -1;

    if (!buffer || buffer_size == 0)
        return -1;

    if (!fs_find_entry(name, &inode_number))
        return -1;

    if (!fs_read_inode(inode_number, &inode))
        return -1;

    if (inode.type != FS_TYPE_FILE)
        return -1;

    if (inode.size + 1 > buffer_size)
        return -2;

    uint32_t remaining = inode.size;
    uint32_t position = 0;

    for (uint32_t i = 0;
         i < inode.sector_count;
         i++) {

        if (!block_read_sector(
                inode.start_sector + i,
                sector_buffer))
            return -1;

        uint32_t amount =
            remaining > FS_SECTOR_SIZE
            ? FS_SECTOR_SIZE
            : remaining;

        if (amount != 0) {
            fs_copy(
                buffer + position,
                sector_buffer,
                amount
            );

            position += amount;
            remaining -= amount;
        }
    }

    buffer[position] = 0;

    return (int)position;
}

int fs_file_exists(
    const char *name,
    uint32_t *inode_number
)
{
    if (!name || !name[0])
        return 0;

    return fs_find_entry(
        name,
        inode_number
    );
}

uint32_t fs_list(
    char names[][FS_NAME_SIZE],
    uint8_t *types,
    uint32_t max_entries
)
{
    if (!block_read_sector(FS_ROOT_DIR, sector_buffer))
        return 0;

    FS_DirEntry *entries =
        (FS_DirEntry *)sector_buffer;

    uint32_t count = 0;

    for (uint32_t i = 0;
         i < FS_ROOT_ENTRIES &&
         count < max_entries;
         i++) {

        if (entries[i].inode == 0)
            continue;

        fs_copy(
            names[count],
            entries[i].name,
            FS_NAME_SIZE
        );

        types[count] = entries[i].type;

        count++;
    }

    return count;
}
