

set -e

echo "[BUILD] Compilation kernel..."

clang -target x86_64-unknown-elf \
  -ffreestanding \
  -fno-stack-protector \
  -mno-red-zone \
  -O2 \
  -Wall -Wextra \
  -c kernel.c -o kernel.o

ld.lld -T kernel.ld \
  kernel.o paging.o heap.o fs.o block.o ahci.o ata.o input.o pci.o \
  xhci.o gdt.o idt.o isr.o syscall.o ring3.o window.o \
  -o kernel.elf

echo "[DISK] Injection de hello.nic..."

python3 <<'PY'
from pathlib import Path
import struct

disk = Path("disk.img")
app = Path("hello.nic")

data = app.read_bytes()

SECTOR = 512
ROOT_DIR = 17
DATA_START = 18

if len(data) > SECTOR:
    raise SystemExit("hello.nic dépasse un secteur")

with disk.open("r+b") as f:
    
    f.seek(ROOT_DIR * SECTOR)

    entry = bytearray(SECTOR)

    
    struct.pack_into("<I", entry, 0, 1)

    
    entry[4] = 1

    
    name = b"hello.nic"
    entry[8:8 + len(name)] = name

    f.write(entry)

    
    f.seek(SECTOR + 64)

    inode = bytearray(64)

    
    inode[0] = 1

    
    struct.pack_into("<I", inode, 4, len(data))

    
    struct.pack_into("<I", inode, 8, DATA_START)

    
    struct.pack_into("<I", inode, 12, 1)

    f.write(inode)

    
    f.seek(DATA_START * SECTOR)

    sector = bytearray(SECTOR)
    sector[:len(data)] = data
    f.write(sector)

print(f"[DISK] hello.nic: {len(data)} octets")
print("[DISK] inode 1")
print("[DISK] root entry: hello.nic")
print("[DISK] data sector: 18")
PY

echo "[ISO] Mise à jour EFI..."

mcopy -o -i efi.img kernel.elf ::/EFI/BOOT/kernel.elf

cp efi.img iso_root/efi.img
cp kernel.elf iso_root/EFI/BOOT/kernel.elf

rm -f NicOS.iso

xorriso \
  -as mkisofs \
  -R \
  -J \
  -V NICOS \
  -e efi.img \
  -no-emul-boot \
  -o NicOS.iso \
  iso_root

pkill qemu-system-x86_64 2>/dev/null || true

echo "[QEMU] Lancement..."

qemu-system-x86_64 \
  -M q35 \
  -m 512M \
  -bios OVMF_CODE.fd \
  -drive format=raw,file=NicOS.iso \
  -drive format=raw,file=disk.img \
  -device qemu-xhci,id=xhci \
  -device usb-kbd,bus=xhci.0 \
  -debugcon stdio \
  -global isa-debugcon.iobase=0x402 \
  -display sdl,gl=off
