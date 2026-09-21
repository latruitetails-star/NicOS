import struct
from pathlib import Path

DISK = Path("disk.img")
FILE = Path("hello.nic")
NAME = b"hello.nic"

SECTOR = 512
INODE_SIZE = 64
INODE_COUNT = 128
INODE_START = 1
ROOT_DIR = 17
DATA_START = 18
ROOT_ENTRIES = 8
DIR_ENTRY_SIZE = 64

if not DISK.exists():
    raise SystemExit("disk.img introuvable")

if not FILE.exists():
    raise SystemExit("hello.nic introuvable")

data = FILE.read_bytes()
size = len(data)
sector_count = (size + SECTOR - 1) // SECTOR

with DISK.open("r+b") as f:
    
    f.seek(0)
    sb = f.read(SECTOR)

    magic, version, sector_size, inode_start, inode_count, data_start, root_inode = struct.unpack_from(
        "<QIIIIII", sb, 0
    )

    if magic != 0x4E49434F53465331:
        raise SystemExit("Magic FS invalide")

    if sector_size != SECTOR:
        raise SystemExit(f"Taille secteur inattendue: {sector_size}")

    if inode_start != INODE_START or inode_count != INODE_COUNT:
        raise SystemExit("Paramètres inode inattendus")

    if data_start != DATA_START:
        raise SystemExit("DATA_START inattendu")

    
    f.seek(ROOT_DIR * SECTOR)
    root = bytearray(f.read(SECTOR))

    existing_inode = None

    for i in range(ROOT_ENTRIES):
        off = i * DIR_ENTRY_SIZE
        inode, typ = struct.unpack_from("<IB", root, off)
        name = bytes(root[off + 8:off + 64]).split(b"\0", 1)[0]

        if name == NAME:
            existing_inode = inode
            print(f"hello.nic existe déjà, inode {inode}")
            break

    
    if existing_inode is None:
        inode_number = None

        for i in range(1, INODE_COUNT):
            off = INODE_START * SECTOR + i * INODE_SIZE
            f.seek(off)
            inode = f.read(INODE_SIZE)

            if inode[0] == 0:
                inode_number = i
                break

        if inode_number is None:
            raise SystemExit("Aucun inode libre")

        
        dir_slot = None

        for i in range(ROOT_ENTRIES):
            off = i * DIR_ENTRY_SIZE
            inode, typ = struct.unpack_from("<IB", root, off)

            if inode == 0 and typ == 0:
                dir_slot = i
                break

        if dir_slot is None:
            raise SystemExit("Aucune entrée libre dans le root directory")

    else:
        inode_number = existing_inode
        dir_slot = None

    
    
    used = set()

    for i in range(1, INODE_COUNT):
        off = INODE_START * SECTOR + i * INODE_SIZE
        f.seek(off)
        inode = f.read(INODE_SIZE)

        typ = inode[0]

        if typ != 1:
            continue

        inode_size, start_sector, count = struct.unpack_from(
            "<III", inode, 4
        )

        if count:
            for s in range(start_sector, start_sector + count):
                used.add(s)

    start_sector = None

    if sector_count:
        max_sector = DISK.stat().st_size // SECTOR

        for start in range(DATA_START, max_sector - sector_count + 1):
            if all(
                (start + i) not in used
                for i in range(sector_count)
            ):
                start_sector = start
                break

        if start_sector is None:
            raise SystemExit("Aucun secteur libre")

    
    for i in range(sector_count):
        chunk = data[i * SECTOR:(i + 1) * SECTOR]
        chunk = chunk.ljust(SECTOR, b"\0")

        f.seek((start_sector + i) * SECTOR)
        f.write(chunk)

    
    inode_data = bytearray(INODE_SIZE)

    inode_data[0] = 1  

    struct.pack_into("<III", inode_data, 4,
                     size,
                     start_sector or 0,
                     sector_count)

    inode_offset = INODE_START * SECTOR + inode_number * INODE_SIZE

    f.seek(inode_offset)
    f.write(inode_data)

    
    if existing_inode is None:
        off = dir_slot * DIR_ENTRY_SIZE

        entry = bytearray(DIR_ENTRY_SIZE)

        struct.pack_into("<I", entry, 0, inode_number)
        entry[4] = 1
        entry[8:8 + len(NAME)] = NAME

        f.seek(ROOT_DIR * SECTOR + off)
        f.write(entry)

    f.flush()

print("Injection OK")
print(f"  fichier       : hello.nic")
print(f"  taille        : {size} octets")
print(f"  inode         : {inode_number}")
print(f"  secteur       : {start_sector}")
print(f"  secteurs      : {sector_count}")
