
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"

EFI="$DIR/efi.img"
DISK="$DIR/disk.img"
OUT="$DIR/NicOS-usb.img"

SECTOR=512

EFI_SIZE=$((64 * 1024 * 1024))
EFI_SECTORS=$((EFI_SIZE / SECTOR))

PART1_START=2048
PART1_END=$((PART1_START + EFI_SECTORS - 1))

DISK_SIZE=$(stat -c %s "$DISK")

if [ $((DISK_SIZE % SECTOR)) -ne 0 ]; then
    echo "ERREUR: disk.img n'est pas aligné sur 512 octets"
    exit 1
fi

DISK_SECTORS=$((DISK_SIZE / SECTOR))

PART2_START=$((PART1_END + 1))
PART2_END=$((PART2_START + DISK_SECTORS - 1))

TOTAL_SECTORS=$((PART2_END + 2048))
TOTAL_SIZE=$((TOTAL_SECTORS * SECTOR))

echo "[USB] Vérification..."

[ -f "$EFI" ] || {
    echo "ERREUR: efi.img introuvable"
    exit 1
}

[ -f "$DISK" ] || {
    echo "ERREUR: disk.img introuvable"
    exit 1
}

EFI_ACTUAL=$(stat -c %s "$EFI")

[ "$EFI_ACTUAL" -eq "$EFI_SIZE" ] || {
    echo "ERREUR: efi.img doit faire exactement 64 MiB"
    exit 1
}

echo "[USB] Mise à jour du kernel..."
mcopy -o -i "$EFI" "$DIR/kernel.elf" ::/EFI/BOOT/kernel.elf

echo "[USB] Création de l'image..."
rm -f "$OUT"
truncate -s "$TOTAL_SIZE" "$OUT"

echo "[USB] Création GPT..."

sgdisk --zap-all "$OUT"

sgdisk \
    --new=1:${PART1_START}:${PART1_END} \
    --typecode=1:EF00 \
    --change-name=1:NIC0S-ESP \
    --new=2:${PART2_START}:${PART2_END} \
    --typecode=2:8300 \
    --change-name=2:NIC0S-FS \
    "$OUT"

echo "[USB] Copie de l'ESP..."

dd if="$EFI" \
   of="$OUT" \
   bs=512 \
   seek="$PART1_START" \
   conv=notrunc

echo "[USB] Copie du filesystem NicOS..."

dd if="$DISK" \
   of="$OUT" \
   bs=512 \
   seek="$PART2_START" \
   conv=notrunc

sync

echo
echo "[USB] Vérification finale:"
fdisk -l "$OUT"

echo
echo "[USB] Vérification GPT:"
sgdisk -p "$OUT"

echo
echo "[USB] OK: $OUT"
