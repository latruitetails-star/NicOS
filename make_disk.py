import struct

DISK = "disk.img"
APP = "hello.nic"

SECTOR = 512
INODE_SIZE = 64
INODE_COUNT = 128

FS_MAGIC = 0x4E49434F53465331
FS_VERSION = 1

FS_SUPERBLOCK = 0
FS_INODE_START = 1
FS_INODE_SECTORS = 16
FS_ROOT_DIR = 17
FS_DATA_START = 18

FS_TYPE_FREE = 0
FS_TYPE_FILE = 1
FS_TYPE_DIR = 2

FS_NAME_SIZE = 56
FS_DIR_ENTRY_SIZE = 64
FS_ROOT_ENTRIES = 8

with open(APP, "rb") as f:
    data = f.read()

if len(data) > 0xFFFFFFFF:
    raise SystemExit("hello.nic trop gros")

with open(DISK, "r+b") as f:

    
    superblock = bytearray(SECTOR)

    struct.pack_into("<Q", superblock, 0, FS_MAGIC)
    struct.pack_into("<I", superblock, 8, FS_VERSION)
    struct.pack_into("<I", superblock, 12, SECTOR)
    struct.pack_into("<I", superblock, 16, FS_INODE_START)
    struct.pack_into("<I", superblock, 20, INODE_COUNT)
    struct.pack_into("<I", superblock, 24, FS_DATA_START)
    struct.pack_into("<I", superblock, 28, 0)  

    f.seek(FS_SUPERBLOCK * SECTOR)
    f.write(superblock)

    
    f.seek(FS_INODE_START * SECTOR)
    f.write(b"\x00" * (FS_INODE_SECTORS * SECTOR))

    
    root_inode = bytearray(INODE_SIZE)
    root_inode[0] = FS_TYPE_DIR
    struct.pack_into("<I", root_inode, 4, 0)
    struct.pack_into("<I", root_inode, 8, FS_ROOT_DIR)
    struct.pack_into("<I", root_inode, 12, 1)

    f.seek(FS_INODE_START * SECTOR)
    f.write(root_inode)

    
    sectors = (len(data) + SECTOR - 1) // SECTOR

    hello_inode = bytearray(INODE_SIZE)
    hello_inode[0] = FS_TYPE_FILE
    struct.pack_into("<I", hello_inode, 4, len(data))
    struct.pack_into("<I", hello_inode, 8, FS_DATA_START)
    struct.pack_into("<I", hello_inode, 12, sectors)

    f.seek(FS_INODE_START * SECTOR + INODE_SIZE)
    f.write(hello_inode)

    
    root_dir = bytearray(SECTOR)

    
    
    
    
    
    struct.pack_into("<I", root_dir, 0, 1)
    root_dir[4] = FS_TYPE_FILE
    root_dir[8:8 + len(b"hello.nic")] = b"hello.nic"

    f.seek(FS_ROOT_DIR * SECTOR)
    f.write(root_dir)

    
    f.seek(FS_DATA_START * SECTOR)

    padded = data + b"\x00" * (sectors * SECTOR - len(data))
    f.write(padded)

print(f"Filesystem créé dans {DISK}")
print(f"hello.nic : {len(data)} octets")
print(f"inode     : 1")
print(f"secteur   : {FS_DATA_START}")
print(f"secteurs  : {sectors}")
