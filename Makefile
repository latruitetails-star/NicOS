SHELL := /data/data/com.termux/files/usr/bin/bash

CC      := clang
LD      := ld.lld
PYTHON  := python3
MCOPY   := mcopy
SGDISK  := sgdisk
DD      := dd
XORRISO := xorriso

CFLAGS := -target x86_64-unknown-elf \
          -ffreestanding \
          -fno-stack-protector \
          -mno-red-zone \
          -O2 \
          -Wall -Wextra \
          -MMD -MP

LDFLAGS := -T kernel.ld

KERNEL     := kernel.elf
BOOTLOADER := BOOTX64.EFI
EFI        := efi.img
DISK       := disk.img
USB        := NicOS-usb.img
ISO        := NicOS.iso

OBJS := \
kernel.o \
paging.o \
heap.o \
fs.o \
block.o \
ahci.o \
ata.o \
input.o \
pci.o \
xhci.o \
gdt.o \
idt.o \
isr.o \
syscall.o \
ring3.o \
window.o \
	graphics.o

DEPS := $(OBJS:.o=.d)

.PHONY: all kernel disk efi usb iso clean rebuild

# ============================================================
# BUILD PRINCIPAL
# ============================================================

all: usb

# ============================================================
# KERNEL
# ============================================================

kernel: $(KERNEL)

$(KERNEL): $(OBJS) kernel.ld
	@echo "[LD] $@"
	$(LD) $(LDFLAGS) $(OBJS) -o $@
	@echo "[LD] Kernel OK"

%.o: %.c
	@echo "[CC] $<"
	$(CC) $(CFLAGS) -c $< -o $@

isr.o: isr.S
	@echo "[AS] $<"
	$(CC) -target x86_64-unknown-elf -ffreestanding -mno-red-zone -c $< -o $@

ring3.o: ring3.S
	@echo "[AS] $<"
	$(CC) -target x86_64-unknown-elf -ffreestanding -mno-red-zone -c $< -o $@

# ============================================================
# FILESYSTEM
# ============================================================

.PHONY: disk

disk: hello.nic inject.py disk.img
	@echo "[DISK] Injection de hello.nic..."
	$(PYTHON) inject.py
	@echo "[DISK] Injection OK"

# ============================================================
# EFI
# ============================================================

efi: $(KERNEL) $(BOOTLOADER) $(EFI)
	@echo "[EFI] Mise à jour..."
	$(MCOPY) -o -i $(EFI) $(BOOTLOADER) ::/EFI/BOOT/BOOTX64.EFI
	$(MCOPY) -o -i $(EFI) $(KERNEL) ::/EFI/BOOT/kernel.elf
	@echo "[EFI] OK"

# ============================================================
# IMAGE USB PRINCIPALE
# ============================================================

usb: $(USB)

$(USB): $(KERNEL) $(BOOTLOADER) $(EFI) disk
	@echo "[USB] Vérification..."

	@test -f $(EFI) || { echo "ERREUR: $(EFI) introuvable"; exit 1; }
	@test -f $(DISK) || { echo "ERREUR: $(DISK) introuvable"; exit 1; }

	@EFI_SIZE=$$(stat -c %s "$(EFI)"); \
	if [ "$$EFI_SIZE" -ne 67108864 ]; then \
		echo "ERREUR: efi.img doit faire exactement 64 MiB"; \
		exit 1; \
	fi

	@DISK_SIZE=$$(stat -c %s "$(DISK)"); \
	if [ $$((DISK_SIZE % 512)) -ne 0 ]; then \
		echo "ERREUR: disk.img n'est pas aligné sur 512 octets"; \
		exit 1; \
	fi

	@echo "[USB] Mise à jour du bootloader..."
	$(MCOPY) -o -i $(EFI) $(BOOTLOADER) ::/EFI/BOOT/BOOTX64.EFI

	@echo "[USB] Mise à jour du kernel..."
	$(MCOPY) -o -i $(EFI) $(KERNEL) ::/EFI/BOOT/kernel.elf

	@echo "[USB] Création de $(USB)..."
	rm -f $(USB)

	@EFI_SECTORS=131072; \
	PART1_START=2048; \
	PART1_END=$$((PART1_START + EFI_SECTORS - 1)); \
	DISK_SIZE=$$(stat -c %s "$(DISK)"); \
	DISK_SECTORS=$$((DISK_SIZE / 512)); \
	PART2_START=$$((PART1_END + 1)); \
	PART2_END=$$((PART2_START + DISK_SECTORS - 1)); \
	TOTAL_SECTORS=$$((PART2_END + 2048)); \
	TOTAL_SIZE=$$((TOTAL_SECTORS * 512)); \
	truncate -s "$$TOTAL_SIZE" "$(USB)"; \
	echo "[USB] Création GPT..."; \
	$(SGDISK) --zap-all "$(USB)"; \
	$(SGDISK) \
		--new=1:$$PART1_START:$$PART1_END \
		--typecode=1:EF00 \
		--change-name=1:NIC0S-ESP \
		--new=2:$$PART2_START:$$PART2_END \
		--typecode=2:8300 \
		--change-name=2:NIC0S-FS \
		"$(USB)"

	@echo "[USB] Copie de l'ESP..."
	@PART1_START=2048; \
	$(DD) if="$(EFI)" \
		of="$(USB)" \
		bs=512 \
		seek="$$PART1_START" \
		conv=notrunc

	@echo "[USB] Copie du filesystem..."
	@PART2_START=$$(($(shell echo 2048 + 131072))); \
	$(DD) if="$(DISK)" \
		of="$(USB)" \
		bs=512 \
		seek="$$PART2_START" \
		conv=notrunc

	sync

	@echo
	@echo "[USB] Vérification GPT:"
	$(SGDISK) -p $(USB)

	@echo
	@echo "[USB] NicOS prêt:"
	@echo "       $(USB)"

# ============================================================
# ISO SECONDAIRE
# ============================================================

iso: $(ISO)

$(ISO): $(KERNEL) $(BOOTLOADER) $(EFI) disk
	@echo "[ISO] Mise à jour EFI..."
	$(MCOPY) -o -i $(EFI) $(BOOTLOADER) ::/EFI/BOOT/BOOTX64.EFI
	$(MCOPY) -o -i $(EFI) $(KERNEL) ::/EFI/BOOT/kernel.elf

	@echo "[ISO] Préparation..."
	mkdir -p iso_root/EFI/BOOT
	cp $(EFI) iso_root/efi.img
	cp $(KERNEL) iso_root/EFI/BOOT/kernel.elf
	cp $(BOOTLOADER) iso_root/EFI/BOOT/BOOTX64.EFI

	@echo "[ISO] Création de $(ISO)..."
	rm -f $(ISO)

	$(XORRISO) \
		-as mkisofs \
		-R \
		-J \
		-V NICOS \
		-e efi.img \
		-no-emul-boot \
		-o $(ISO) \
		iso_root

	@echo "[ISO] OK: $(ISO)"

# ============================================================
# NETTOYAGE
# ============================================================

clean:
	rm -f $(OBJS)
	rm -f $(DEPS)
	rm -f $(KERNEL)
	rm -f $(USB)
	rm -f $(ISO)

rebuild: clean all

# ============================================================
# DEPENDANCES AUTOMATIQUES
# ============================================================

-include $(DEPS)
