*NicOS — Manuel technique*

> Architecture et état réel de l'implémentation.
> NicOS est un système d'exploitation expérimental x86_64 développé from scratch.
> 
Légende :
✅ Implémenté et testé | 🚧 En cours / incomplet | ⏳ Prévu | ❓ À confirmer dans le code
1. Présentation et Architecture Globale
 * Le kernel est compilé en environnement freestanding sans libc hôte.
 * Les applications utilisent une runtime C spécifique à NicOS et communiquent via syscalls.
 * Séparation stricte : Ring 0 (kernel/privilégié) et Ring 3 (espace utilisateur).
Boot UEFI → BOOTX64.EFI → kernel.elf → Kernel Ring 0 → Syscalls → Ring 3 → Runtime C → Apps .nic

2. Organisation et Build
Le projet est compilé via un Makefile centralisé. La commande make génère l'image USB bootable.
| Domaine | Fichiers |
|---|---|
| Boot / init | boot.c, kernel.c |
| CPU / mémoire | paging.c, gdt.c, idt.c, pmm.c, heap.c |
| Ring 3 / syscalls | ring3.S, ring3.h, syscall.c, syscall.h |
| Stockage | block.c, fs.c, ata.c, ahci.c |
| Bus / USB | pci.c, xhci.c |
| Entrées / affichage | input.c, window.c, graphics.h |
3. Boot UEFI
Le firmware charge EFI/BOOT/BOOTX64.EFI, qui charge kernel.elf.
 * Adresse de chargement : 0x00100000 (1 MiB).
 * Initialisation : Copie des segments PT_LOAD, mise à zéro du .bss.
 * Transmission au kernel : Structure BootInfo contenant le framebuffer (GOP), le RSDP (ACPI) et la memory map (descripteurs de 48 octets).
 * Transition : Appel réussi à ExitBootServices() (retour 0), puis saut vers le kernel.
4. GDT, TSS et Kernel Stack ✅
| Sélecteur | Usage | Access |
|---|---|---|
| 0x08 | Kernel code | 0x9A |
| 0x10 | Kernel data | 0x92 |
| 0x1B (0x18 | 3) | User code |
| 0x23 (0x20 | 3) | User data |
| 0x28 | TSS | 0x89 |
La kernel stack est allouée dans le .bss (16 KiB). Le TSS (sélecteur 0x28) a son champ RSP0 pointant sur le sommet de cette pile pour les transitions Ring 3 → Ring 0.
5. Pagination et Espace Utilisateur
| Constante | Valeur |
|---|---|
| USER_BASE | 0x0000010000000000 (0x10000000000) |
| USER_SIZE | 0x200000 (2 MiB) |
| Page size | 0x1000 (4 KiB) |
| USER_STACK_TOP | USER_BASE + USER_SIZE - 16 (0x100001FFFF0) |
 * Tables : PML4[0]/[1] pour le kernel, PML4[2] pour l'utilisateur.
 * Limites 🚧 : Un seul espace d'adressage partagé, pas de PML4 indépendante par programme. Une seule page allouée pour la stack utilisateur.
6. Passage Ring 0 → Ring 3 ✅
Implémenté via une frame iretq dans ring3.S :
SS (0x23) → RSP (USER_STACK_TOP) → RFLAGS (0x002) → CS (0x1B) → RIP (USER_BASE) → iretq

Note : RFLAGS = 0x002 désactive les interruptions matérielles (IF=0) en Ring 3.
7. Syscalls
Déclenchement via int 0x80. Registres : RAX (numéro), RDI, RSI, RDX, RCX (arguments), RAX (retour).
| N° | Syscall | État |
|---|---|---|
| 0 | SYS_EXIT | 🚧 Fonctionne mais boucle sur hlt (pas de retour au shell) |
| 1 | SYS_WRITE | ✅ Écrit dans le framebuffer graphique |
| 2-9 | Divers | ⏳ Définis dans l'ABI (Read, Open, Close, Malloc, Window...) |
8. Runtime C et Binaire .nic
 * Runtime C 🚧 : Fournit _start (appel main puis nicos_exit) et mappe les appels vers l'ABI syscall.
 * Format .nic ✅ : Binaire plat lié statiquement avec ENTRY(_start) en offset 0. Le chargeur ne gère pas encore l'initialisation du .bss utilisateur.
9. Loader et Exécution (r) ✅
La fonction command_run() charge les binaires à USER_BASE :
 * Lecture et vérification de la taille (refuse les fichiers de 0 octet).
 * Affichage des métadonnées (inode, taille, avertissement de troncature si limite atteinte).
 * Allocation des pages et copie du code.
 * Passage en Ring 3 via ring3_enter().
10. Filesystem ✅
Système de fichiers rudimentaire injecté via inject.py.
| Paramètre | Valeur |
|---|---|
| Secteur / Inode / Entrée | 512 octets / 64 octets / 64 octets |
| Limites | 128 inodes, noms de 56 octets max |
| Secteur 0 | Superblock (magic 0x4E49434F53465331) |
| Secteurs 1–16 | Table des inodes |
| Secteur 17 | Répertoire racine (inode 0, 8 entrées max) |
| Secteur 18+ | Zone de données (blocs contigus par fichier) |
L'allocation parcourt séquentiellement les inodes existants pour trouver l'espace libre.
11. Shell et Commandes ✅
Le shell utilise un parsing sécurisé (command_prefix()) et intègre des protections (terminaison NUL forcée sur les buffers).
 * Commandes : help, ls, cat <name>, write <name> <text>, touch <name>, mkdir <name>, r <app>.
12. Pilotes et Matériel
L'abstraction de stockage est : Filesystem → Block layer → AHCI / ATA PIO → Disque.
| Sous-système | État | Détail |
|---|---|---|
| PCI | ✅ | Énumération fonctionnelle |
| ATA PIO / AHCI | ✅ | Couche bloc priorisant AHCI |
| XHCI (USB) | 🚧 | Initialisé au boot, émet des logs fonctionnels |
| Window / Graphics | 🚧 | Framebuffer GOP actif, Window manager basique |
| Input | 🚧 | Infrastructure limitée |
13. Limites Connues
 * Retour système : SYS_EXIT ne rend pas la main au shell (redémarrage requis après un r).
 * Interruptions : IF désactivé en Ring 3, empêchant le multitâche préemptif.
 * Ségrégation mémoire : Espace d'adressage unique partagé pour tous les processus utilisateurs.
 * Logs : Les logs XHCI polluent l'affichage standard à l'écran.
 * FS : Racine limitée à 8 entrées sur un seul secteur, pas de sous-répertoires gérés dynamiquement.

signal group:
https://signal.group/#CjQKIKGjfN-TuyOV8DlKYTErGCwZeLi_u0Oe0fz3nQR1Qz4bEhCO3Uybevbw2xwkpPmKrD_h
