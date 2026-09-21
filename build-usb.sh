
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"

EFI="$DIR/efi.img"
OUT="$DIR/NicOS-usb.img"

SECTOR=512
EFI_SIZE=$((64 * 1024 * 1024))
EFI_SECTORS=$((EFI_SIZE / SECTOR))

PART_START=2048
PART_END=$((PART_START + EFI_SECTORS - 1))


TOTAL_SECTORS=$((PART_END + 2048))
TOTAL_SIZE=$((TOTAL_SECTORS * SECTOR))

echo "[USB] Vérification de efi.img..."

[ -f "$EFI" ] || {
    echo "ERREUR: efi.img introuvable"
    exit 1
}

EFI_ACTUAL=$(stat -c %s "$EFI")

[ "$EFI_ACTUAL" -eq "$EFI_SIZE" ] || {
    echo "ERREUR: efi.img doit faire 64 MiB"
    echo "Taille actuelle: $EFI_ACTUAL octets"
    exit 1
}

echo "[USB] Mise à jour du kernel dans efi.img..."
mcopy -o -i "$EFI" "$DIR/kernel.elf" ::/EFI/BOOT/kernel.elf

echo "[USB] Création de l'image..."
rm -f "$OUT"
truncate -s "$TOTAL_SIZE" "$OUT"

echo "[USB] Création de la GPT..."

sgdisk --zap-all "$OUT"

sgdisk \
    --new=1:${PART_START}:${PART_END} \
    --typecode=1:EF00 \
    --change-name=1:NIC0S-ESP \
    "$OUT"

echo "[USB] Copie de l'ESP..."

dd if="$EFI" \
   of="$OUT" \
   bs=512 \
   seek="$PART_START" \
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
