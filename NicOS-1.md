# NicOS — Manuel technique

> Architecture et **état réel de l'implémentation**.
> NicOS est un système d'exploitation expérimental x86_64 développé *from scratch*.

**Légende utilisée dans tout le document :**

| Marque | Signification |
|---|---|
| ✅ | Présent et utilisé / testé |
| 🚧 | Présent mais incomplet ou en cours de développement |
| ⏳ | Prévu, pas encore implémenté |
| ❓ | Décrit dans la conception, à confirmer dans le code |

---

## Sommaire

1. [Présentation](#1-présentation)
2. [Organisation du projet](#2-organisation-du-projet)
3. [Boot UEFI](#3-boot-uefi)
4. [GDT, TSS et kernel stack](#4-gdt-tss-et-kernel-stack)
5. [Pagination et espace utilisateur](#5-pagination-et-espace-utilisateur)
6. [Passage Ring 0 → Ring 3](#6-passage-ring-0--ring-3)
7. [Syscalls](#7-syscalls)
8. [Runtime C et format `.nic`](#8-runtime-c-et-format-nic)
9. [Loader et commande `r`](#9-loader-et-commande-r)
10. [Filesystem](#10-filesystem)
11. [Shell](#11-shell)
12. [Pilotes et sous-systèmes](#12-pilotes-et-sous-systèmes)
13. [Chaîne complète d'une application](#13-chaîne-complète-dune-application)
14. [Tableau de l'état actuel](#14-tableau-de-létat-actuel)
15. [Valeurs de référence](#15-valeurs-de-référence)
16. [Limites connues et points d'attention](#16-limites-connues-et-points-dattention)
17. [Évolutions prévues](#17-évolutions-prévues)
18. [Annexe — Architecture interne de la mémoire et du stockage](#18-annexe--architecture-interne-de-la-mémoire-et-du-stockage)

---

## 1. Présentation

NicOS est organisé autour des composants suivants :

```
Boot UEFI
    ↓
BOOTX64.EFI
    ↓
kernel.elf
    ↓
Kernel Ring 0
    ├── Paging
    ├── GDT / TSS
    ├── IDT / ISR
    ├── PMM / Heap
    ├── Filesystem
    ├── Block layer
    ├── PCI
    ├── ATA / AHCI
    ├── XHCI
    ├── Input
    ├── Graphics / Window
    └── Syscalls
            ↓
        Ring 3
            ↓
       Runtime C
            ↓
       Applications .nic
```

- Le kernel est compilé en environnement **freestanding** et ne dépend pas de la libc de l'hôte.
- Les applications utilisent une **runtime C spécifique à NicOS** : elles sont écrites principalement en C et passent par des **syscalls** pour toute opération privilégiée.
- Le principe central est la séparation entre le code privilégié (Ring 0) et le code utilisateur (Ring 3) : le kernel garde le contrôle du matériel, les programmes `.nic` communiquent avec lui uniquement via l'ABI de syscalls.

---

## 2. Organisation du projet

Fichiers importants du kernel :

| Domaine | Fichiers |
|---|---|
| Boot / init | `boot.c`, `kernel.c` |
| CPU / mémoire | `paging.c`, `gdt.c`, `idt.c`, `pmm.c`, `heap.c` |
| Ring 3 / syscalls | `ring3.S`, `ring3.h`, `syscall.c`, `syscall.h` |
| Stockage | `block.c`, `fs.c`, `ata.c`, `ahci.c` |
| Bus / USB | `pci.c`, `xhci.c` |
| Entrées / affichage | `input.c`, `window.c`, `graphics.h` |

> Il n'existe pas de `ring3.c` ni de `isr.c`. La partie bas niveau du passage en Ring 3 est implémentée dans `ring3.S`.

---

## 3. Boot UEFI

### 3.1 Programmes EFI

Le firmware UEFI charge `EFI/BOOT/BOOTX64.EFI`, premier composant spécifique à NicOS. Ce bootloader recherche ensuite `EFI/BOOT/kernel.elf` (ELF64 x86-64).

### 3.2 Chargement de `kernel.elf`

- Adresse de link/chargement du kernel : **`0x00100000`** (1 MiB).
- Point d'entrée ELF (`readelf`) : `0x100000`, lu via `eh->e_entry`.
- Le bootloader parcourt les segments `PT_LOAD` :

| Champ ELF | Rôle |
|---|---|
| `p_vaddr` | adresse de destination |
| `p_filesz` | octets copiés depuis le fichier |
| `p_memsz` | taille totale en mémoire |

Les `p_filesz` octets sont copiés, puis l'espace jusqu'à `p_memsz` est mis à zéro. Cela gère le `.bss` du kernel.

### 3.3 Structure `BootInfo`

Le bootloader construit une structure `BootInfo` et la transmet au kernel :

```c
KERNEL_ENTRY(BootInfo*)
```

Contenu prévu/transporté :

- framebuffer (GOP) : `width`, `height`, `pixels_per_scanline` / pitch, pixel format ;
- `RSDP` (point d'entrée des tables ACPI) ;
- memory map : pointeur, taille, taille de descripteur, version de descripteur.

### 3.4 Memory map UEFI

Valeurs observées lors des tests :

| Élément | Valeur |
|---|---|
| Taille nécessaire de la memory map | `0x1380` (4992 octets) |
| Taille d'un descripteur | `0x30` (48 octets) |

Le kernel peut s'en servir pour initialiser son gestionnaire de mémoire physique.

### 3.5 GOP et framebuffer

Le framebuffer fourni par UEFI GOP est transmis au kernel via `BootInfo`. Le sous-système graphique écrit directement dedans.

### 3.6 `ExitBootServices()`

Le bootloader appelle bien `ExitBootServices()` avant le transfert de contrôle. Retour observé dans les tests :

```
[BOOT] ExitBootServices returned: 0000000000000000
```

`0` = `EFI_SUCCESS`.

**État actuel :** ✅ le boot ne s'arrête pas à ce point. Le kernel est bien atteint et exécuté : après `ExitBootServices()`, la suite de l'initialisation se déroule et les derniers logs visibles au démarrage sont ceux de l'init **USB (XHCI)**.

Les messages `[BOOT] segments loaded` et `[BOOT] jumping to kernel` ne sont pas visibles à l'écran, ce qui est cohérent avec le fait que la console UEFI (`Print()`/`ConOut`) n'est plus utilisable une fois les Boot Services quittés.

L'écran continue ensuite d'afficher de l'activité : logs d'écriture / commandes USB (XHCI) et sorties des commandes lancées depuis le shell. Le système n'est donc pas bloqué au boot ; voir la [section 16](#16-limites-connues-et-points-dattention) pour les points restants.

---

## 4. GDT, TSS et kernel stack

### 4.1 GDT ✅

| Sélecteur | Usage | Access byte |
|---|---|---|
| `0x08` | Kernel code | `0x9A` |
| `0x10` | Kernel data | `0x92` |
| `0x18` | User code | `0xFA` |
| `0x20` | User data | `0xF2` |
| `0x28` | TSS | `0x89` |

Pour le code Ring 3, le RPL 3 est ajouté au sélecteur :

```
User CS = 0x18 | 3 = 0x1B
User DS = 0x20 | 3 = 0x23
```

### 4.2 TSS et kernel stack ✅

Le TSS x86_64 est décrit dans la GDT au sélecteur `0x28`. Son champ **`RSP0`** est la pile kernel utilisée lors d'une transition Ring 3 → Ring 0.

- Kernel stack actuelle : `kernel_stack[16384]` (**16 KiB**), située dans le `.bss`.
- `RSP0` pointe sur l'extrémité haute :

```c
tss.rsp0 = (uint64_t)(kernel_stack + sizeof(kernel_stack));
```

```
adresse basse
     │
     ▼
┌─────────────────────┐
│   kernel_stack      │
│      16 KiB         │
└─────────────────────┘
     ▲
     │
    RSP0
adresse haute
```

La fonction `gdt_set_kernel_stack()` permet de mettre à jour cette valeur.

---

## 5. Pagination et espace utilisateur

### 5.1 Constantes ✅

| Constante | Valeur |
|---|---|
| `USER_BASE` | `0x0000010000000000` (`0x10000000000`) |
| `USER_SIZE` | `0x200000` (2 MiB) |
| Taille de page | `0x1000` (4 KiB) |

Flags de pages définis :

```c
PAGE_PRESENT = 1 << 0
PAGE_WRITE   = 1 << 1
PAGE_USER    = 1 << 2
PAGE_HUGE    = 1 << 7
```

### 5.2 Organisation des tables

```
PML4[0], PML4[1]  →  mappings kernel
PML4[2]           →  espace utilisateur
```

L'espace utilisateur occupe `0x0000010000000000` → `0x0000010000200000`. Les programmes sont chargés à `USER_BASE`.

### 5.3 User stack ✅

```
USER_STACK_TOP = USER_BASE + USER_SIZE - 16 = 0x00000100001FFFF0
```

La dernière page de la région (`0x100001FF000` → `0x10000200000`) est réservée à la stack. Le loader ne réserve **qu'une seule page** de stack.

### 5.4 Espace d'adressage 🚧

Ce n'est pas encore un modèle de processus complet : il n'y a **pas** de PML4 indépendante par programme. L'infrastructure actuelle est une première base Ring 3. Une isolation entre plusieurs processus simultanés nécessitera un espace d'adressage distinct par processus, ou au minimum une gestion explicite de ce qui est partagé.

---

## 6. Passage Ring 0 → Ring 3

Implémenté dans `ring3.S` ✅. Le kernel prépare une frame `iretq` :

| Champ | Valeur |
|---|---|
| `SS` | `0x23` |
| `RSP` | `USER_STACK_TOP` |
| `RFLAGS` | `0x002` |
| `CS` | `0x1B` |
| `RIP` | `USER_BASE` |

Ordre de la frame (du plus profond au sommet de pile) :

```
SS
RSP
RFLAGS
CS
RIP
   ↓ iretq
```

Le processeur restaure alors le contexte et démarre l'exécution du programme en Ring 3. L'appel côté loader est :

```c
ring3_enter(USER_BASE, USER_STACK_TOP);
```

> `RFLAGS = 0x002` ne positionne que le bit réservé (bit 1) : le flag `IF` est à 0, donc les interruptions matérielles sont masquées en Ring 3. Voir section 16.

---

## 7. Syscalls

### 7.1 ABI

- Le numéro du syscall est dans `RAX`, les arguments dans `RDI`, `RSI`, `RDX`, `RCX`.
- Le résultat est renvoyé dans `RAX`.
- La structure `SyscallFrame` conserve les registres dans l'ordre `r15 … rax`.
- Mécanisme de déclenchement : `int 0x80`.

### 7.2 Numéros définis

| N° | Syscall | Remarque |
|---|---|---|
| 0 | `SYS_EXIT` | 🚧 boucle sur `hlt` (voir 7.4) |
| 1 | `SYS_WRITE` | ✅ écrit dans le framebuffer |
| 2 | `SYS_READ` | défini dans l'ABI |
| 3 | `SYS_OPEN` | défini dans l'ABI |
| 4 | `SYS_CLOSE` | défini dans l'ABI |
| 5 | `SYS_MALLOC` | défini dans l'ABI |
| 6 | `SYS_FREE` | défini dans l'ABI |
| 7 | `SYS_TIME` | défini dans l'ABI |
| 8 | `SYS_WINDOW_CREATE` | défini dans l'ABI, window manager limité |
| 9 | `SYS_WINDOW_TEXT` | défini dans l'ABI, window manager limité |

### 7.3 `SYS_WRITE` ✅

La sortie se fait **via le framebuffer**, pas via COM1 ni via le port `0x402`.

```
Application → nicos_write() → SYS_WRITE (1) → syscall_dispatch() → framebuffer → écran
```

### 7.4 `SYS_EXIT` 🚧

Après avoir traité la demande, l'implémentation actuelle **entre dans une boucle `hlt`** : il n'y a pas encore de retour propre au shell.

**Comportement observé :** un programme Ring 3 lancé avec `r` ne se termine jamais réellement et ne rend pas la main au shell : il faut **redémarrer** NicOS pour reprendre la main.

```
main() → nicos_exit() → SYS_EXIT → kernel → [hlt]      (état actuel)
main() → nicos_exit() → SYS_EXIT → kernel → shell      (cible)
```

Le passage Ring 3 → Ring 0 fonctionne comme mécanisme de syscall ; la gestion complète du cycle de vie du programme reste à terminer.

---

## 8. Runtime C et format `.nic`

### 8.1 Runtime C 🚧

Petite couche équivalente, dans son principe, à une libc très réduite (elle n'est **pas** une libc complète) :

| Fichier | Rôle |
|---|---|
| `nicos.h` | déclarations publiques (`nicos_write`, `nicos_exit`, …) |
| `nicos_runtime.c` | implémentation : prépare les registres et déclenche le syscall |
| `runtime_entry.S` | fournit `_start`, qui appelle `main()` puis `nicos_exit()` |

```
_start:
    ...
    call main
    ...
    call nicos_exit
```

La valeur de retour de `main()` devient le code de sortie transmis à `SYS_EXIT`.

### 8.2 Format `.nic` ✅ (modèle plat)

Un `.nic` est un **binaire plat** destiné au loader NicOS, pas un ELF autonome.

```
hello_runtime.c
      ↓  compilation freestanding
hello_runtime.o
      ↓  ld.lld + runtime.ld
ELF temporaire
      ↓  llvm-objcopy
hello.nic
```

- Le linker script utilise `ENTRY(_start)`, avec la section d'entrée placée au début du binaire.
- Sections actuellement gérées : `.text`, `.rodata`.
- `.data` et `.bss` : 🚧 gestion **non équivalente à un loader ELF** ; le `.bss` utilisateur n'est notamment pas alloué/zéro-initialisé par le loader.

Le `.nic` ne supporte donc pas automatiquement toutes les sémantiques ELF : c'est un programme statiquement lié et aplati.

---

## 9. Loader et commande `r`

`command_run()` charge le binaire plat :

```
lit le fichier
   ↓
alloue les pages nécessaires
   ↓
mappe la zone utilisateur
   ↓
copie le .nic à USER_BASE
   ↓
réserve la dernière page pour la stack
   ↓
ring3_enter(USER_BASE, USER_STACK_TOP)
```

- Le point d'entrée du programme est le début du binaire (offset 0 → `_start`) : `hello.nic + 0` = `USER_BASE` = `0x0000010000000000`.
- Forme fiable pour lancer un programme : **`r hello.nic`**.
- Le `-r` de `r -r hello.nic` correspondait à une conception initiale d'un « mode runtime » ; il n'a pas de comportement distinct implémenté pour l'instant.

---

## 10. Filesystem

Filesystem personnalisé, en lecture/écriture ✅ (fonctionnalités rudimentaires).

### 10.1 Constantes

| Constante | Valeur |
|---|---|
| `FS_SECTOR_SIZE` | 512 octets |
| `FS_INODE_SIZE` | 64 octets |
| `FS_INODE_COUNT` | 128 |
| `FS_NAME_SIZE` | 56 octets |
| `FS_DIR_ENTRY_SIZE` | 64 octets |
| `FS_ROOT_DIR` | secteur 17 |
| `FS_DATA_START` | secteur 18 |

### 10.2 Disposition sur `disk.img`

```
Secteur 0        Superblock (magic 0x4E49434F53465331)
Secteurs 1–16    Table des inodes (128 × 64 octets = 8192 octets = 16 secteurs)
Secteur 17       Répertoire racine
Secteur 18+      Zone de données (fichiers / programmes)
```

> Il n'y a pas de bitmap d'allocation dans ce layout : les secteurs utilisés sont déduits en parcourant les inodes. Une bitmap est une évolution envisagée (voir [section 18](#18-annexe--architecture-interne-de-la-mémoire-et-du-stockage)).

### 10.3 Inodes et répertoire racine

- Un inode contient : type, taille, secteur de début, nombre de secteurs.
- L'**inode 0** est la racine : type répertoire, `start_sector = FS_ROOT_DIR`, `sector_count = 1`.
- Un répertoire associe un nom (56 octets max) à un inode. Le répertoire racine tient dans un secteur : **8 entrées** (512 / 64).

### 10.4 Allocation

La recherche de secteurs libres démarre à `FS_DATA_START` (secteur 18). L'allocateur analyse les inodes existants pour déterminer les secteurs utilisés et ignore les inodes qui ne sont pas de type fichier.

### 10.5 Injection de fichiers

L'outil externe `inject.py` construit/modifie `disk.img` et y place les exécutables `.nic` avant le démarrage.

---

## 11. Shell

Commandes actuellement présentes :

| Commande | Rôle |
|---|---|
| `help` | aide |
| `ls` | lister |
| `cat <name>` | lire un fichier |
| `write <name> <text>` | écrire un fichier |
| `touch <name>` | créer un fichier |
| `mkdir <name>` | créer un répertoire |
| `r <app>` | charger et exécuter un programme `.nic` |

---

## 12. Pilotes et sous-systèmes

Trois niveaux à distinguer : *présent dans le code*, *fonctionnel/testé*, *simplement préparé ou incomplet*. L'existence d'un fichier source ne prouve pas sa maturité.

| Sous-système | Fichier | État | Détail |
|---|---|---|---|
| PCI | `pci.c` | ✅ | Énumération des périphériques ; pas un support complet de tous les périphériques PCI |
| ATA | `ata.c` | ✅ | Chemin **ATA PIO**, utilisé en fallback |
| AHCI | `ahci.c` | ✅ | Chemin de stockage privilégié |
| Block layer | `block.c` | ✅ | AHCI si disponible, sinon ATA PIO |
| XHCI | `xhci.c` | 🚧 | Intégré au build et initialisé au boot ; ses logs (écritures / commandes USB) sont les derniers visibles au démarrage et continuent de s'afficher ; maturité inférieure au stockage |
| Input | `input.c` | 🚧 | Infrastructure présente, limitée ; pas une pile HID/USB complète |
| Window / Graphics | `window.c`, `graphics.h` | 🚧 | Framebuffer fonctionnel ; syscalls fenêtres dans l'ABI mais window manager limité |

Abstraction de stockage :

```
Filesystem → Block layer → AHCI (si dispo) / ATA PIO → Contrôleur → Disque
```

---

## 13. Chaîne complète d'une application

```
hello_runtime.c
   ↓  compilation freestanding
hello_runtime.o
   ↓  ld.lld + linker script
ELF temporaire
   ↓  llvm-objcopy
hello.nic
   ↓  inject.py
disk.img
   ↓  boot NicOS
shell
   ↓  r hello.nic
command_run()
   ↓
USER_BASE  (pages user + stack)
   ↓  iretq
Ring 3  →  _start  →  main()
   ↓  nicos_write()
SYS_WRITE  →  framebuffer
   ↓  nicos_exit()
SYS_EXIT  →  hlt  (retour au shell à implémenter)
```

---

## 14. Tableau de l'état actuel

| Sous-système | Fichier | État |
|---|---|---|
| Boot UEFI | `boot.c` | ✅ Le kernel est atteint et exécuté ; les derniers logs visibles au boot sont ceux de l'init USB (XHCI) |
| Chargeur ELF du kernel | `boot.c` | ✅ `PT_LOAD`, `.bss` kernel géré |
| GOP / framebuffer | `boot.c`, graphics | ✅ Utilisé |
| ACPI / RSDP | `boot.c` | ✅ Récupéré et transmis |
| Memory map UEFI | `boot.c` | ✅ Récupérée et transmise |
| GDT | `gdt.c` | ✅ |
| TSS | `gdt.c` | ✅ `RSP0` configuré |
| Kernel stack | `.bss` | ✅ 16 KiB |
| Paging | `paging.c` | ✅ |
| Ring 3 | `ring3.S` | ✅ |
| Syscalls | `syscall.c` | ✅ ABI actuelle |
| Loader `.nic` | `command_run()` | ✅ Chargement plat à `USER_BASE` |
| Filesystem | `fs.c` | ✅ Pour les opérations implémentées |
| Block layer | `block.c` | ✅ AHCI puis ATA PIO |
| PCI / ATA / AHCI | `pci.c`, `ata.c`, `ahci.c` | ✅ |
| XHCI | `xhci.c` | 🚧 |
| Input | `input.c` | 🚧 |
| Window | `window.c` | 🚧 |
| Runtime C | runtime | 🚧 En cours d'intégration |
| `SYS_EXIT` | `syscall.c` | 🚧 Bloque avec `hlt` : un programme lancé par `r` ne rend jamais la main, redémarrage nécessaire |
| Scheduler / multitâche | — | ⏳ |
| Processus indépendants (PML4 par processus) | — | ⏳ |
| `.bss` utilisateur, ELF utilisateur | — | ⏳ |
| USB HID complet, window manager complet | — | ⏳ |

---

## 15. Valeurs de référence

```
Kernel load/link address   0x00100000
User base                  0x0000010000000000
User size                  0x00200000  (2 MiB)
User stack top             0x00000100001FFFF0
Page size                  0x1000 (4096)
Kernel stack               16384 octets (16 KiB)

Kernel CS  0x08     Kernel DS  0x10
User CS    0x1B     User DS    0x23
TSS        0x28

Filesystem sector          512 octets
Inode                      64 octets
Inodes                     128
Filename                   56 octets
Directory entry            64 octets
Root directory             secteur 17 (8 entrées)
Data start                 secteur 18
Root inode                 inode 0
FS magic                   0x4E49434F53465331
```

---

## 16. Limites connues et points d'attention

À traiter en priorité :

1. **Logs de boot et logs USB (XHCI).**
   Le boot n'est pas bloqué : le kernel est atteint et les derniers logs visibles au démarrage sont ceux de l'init USB. Les logs d'écriture / de commandes USB continuent ensuite de s'afficher et se mélangent à la sortie du shell et des programmes Ring 3. Pistes : limiter la verbosité XHCI (niveau de log ou flag de debug) et rediriger les logs kernel vers le port `0x402` ou un port série pour garder l'écran lisible.
   Les logs émis par le bootloader après `ExitBootServices()` ne peuvent plus passer par `Print()`/`ConOut` : pour les voir, écrire dans le framebuffer ou sur `0x402`. À vérifier aussi : les segments `PT_LOAD` sont copiés à `0x100000` **avant** `ExitBootServices()`, et la clé de la memory map provient du dernier `GetMemoryMap()`.
2. **`SYS_EXIT` sans retour au shell.** Un programme lancé avec `r` ne se termine jamais réellement : il faut redémarrer NicOS pour reprendre la main. Il faut sauvegarder le contexte kernel au moment de `ring3_enter()` puis le restaurer dans `SYS_EXIT` pour rendre la main au shell.
3. **`RFLAGS = 0x002`** : `IF` désactivé en Ring 3. Sans `IF` (`0x202`), aucun timer ni IRQ ne peut interrompre un programme utilisateur, ce qui bloquera le futur multitâche préemptif.
4. **`int 0x80` depuis Ring 3** : les syscalls fonctionnent (`SYS_WRITE` observé), ce qui confirme que la gate IDT est accessible depuis Ring 3 (DPL 3) et que `TSS.RSP0` est valide.
5. **`.bss` utilisateur** : le binaire plat ne contient aucune métadonnée. Un header `.nic` (magic, entrée, taille du code, taille du `.bss`) permettrait au loader de zéro-initialiser le `.bss`.
6. **Validation des pointeurs de syscall** : vérifier que toute la plage `[ptr, ptr + size)` est dans l'espace utilisateur, sans dépassement d'entier sur `ptr + size`.
7. **Un seul espace d'adressage** partagé (`PML4[2]`) et une seule page de stack utilisateur.
8. **Répertoire racine limité à 8 entrées**, sans sous-répertoires exploitables dans un seul secteur (`mkdir` existe, mais la structure reste rudimentaire).
9. **Pages utilisateur** : le bit `U/S` doit être positionné à chaque niveau de la hiérarchie (PML4, PDPT, PD, PT) ; à terme, activer NX et SMEP/SMAP.

---

## 17. Évolutions prévues

- Retour propre après `SYS_EXIT`
- Gestion des processus : PID, états Running / Ready / Sleeping, scheduler
- Multitâche préemptif, threads
- Espace d'adressage distinct par processus ; séparation processus / thread / kernel stack / user stack
- Gestion du `.bss` utilisateur ; chargement ELF natif
- Descripteurs de fichiers (stdin/stdout/stderr), VFS, permissions
- USB HID complet, gestion d'entrée complète
- Window manager et API graphique complets
- Réseau, TCP/IP
- Éventuellement les instructions `syscall` / `sysret` à la place de `int 0x80`
- Bitmap d'allocation des blocs du filesystem
- File layer (`file.c`) : file descriptors, position de lecture/écriture, objets ouverts
- Block cache, puis page cache (voir [section 18](#18-annexe--architecture-interne-de-la-mémoire-et-du-stockage))

---

## 18. Annexe — Architecture interne de la mémoire et du stockage

Cette annexe décrit le **modèle de conception** de la mémoire et du stockage de NicOS. Elle mélange des éléments déjà présents dans le code et des éléments cibles : chaque point est donc marqué avec la légende du début du document.

### 18.1 Vue d'ensemble

NicOS comporte deux systèmes distincts mais fortement liés :

- la **mémoire** gère les données *pendant l'exécution* ;
- le **stockage** conserve les données *de manière persistante*.

```
                    NICOS
                      │
        ┌─────────────┴─────────────┐
        │                           │
     MÉMOIRE                     STOCKAGE
        │                           │
 ┌──────┴──────┐             ┌──────┴──────┐
 │             │             │             │
PMM         Paging        Block layer   Filesystem
 │             │             │             │
 └──────┬──────┘             └──────┬──────┘
        │                            │
       RAM                     périphérique
        │
      Heap
```

Le stockage n'est pas de la « RAM très lente » : il possède son propre espace d'adressage logique, fait de blocs.

| Question | Couche qui y répond |
|---|---|
| Où placer les données pendant leur utilisation ? | Mémoire |
| Où les conserver quand elles ne sont plus actives ? | Stockage |
| Comment les retrouver et les organiser ? | Filesystem |
| Comment fournir et protéger la mémoire pour les manipuler ? | Gestionnaire mémoire (PMM + paging + heap) |

### 18.2 Mémoire physique — PMM (`pmm.c`)

- La RAM est découpée en pages de **4096 octets (4 KiB)**, unité de base du PMM.
- Le PMM maintient l'état de chaque page (libre / utilisée). ❓ Une **bitmap** (`0` = libre, `1` = utilisée) est la représentation décrite dans la conception.
- Interface décrite ❓ :

```c
page = page_alloc();   // cherche une page libre, la marque utilisée, retourne son adresse physique
page_free(page);       // la page redevient disponible
```

`pmm.c` est présent dans le code ✅ ; les noms de fonctions et la représentation exacte de l'état des pages sont à confirmer.

### 18.3 Heap du kernel (`heap.c`)

Le PMM manipule des pages entières : appeler `kmalloc(37)` ne doit pas réserver 4096 octets. Le heap est la couche intermédiaire :

```
kmalloc() → heap → (si manque de place) → PMM → pages physiques
```

- Blocs de taille variable, chacun précédé de métadonnées ; le heap demande de nouvelles pages au PMM lorsqu'il est plein.
- Le heap ne gère donc pas directement toute la RAM.
- Fonctions décrites : `kmalloc()`, `kfree()`. ❓ (à confirmer)

### 18.4 Paging (`paging.c`) ✅

Le programme manipule des **adresses virtuelles** ; le paging les traduit en **adresses physiques** via les tables de pages. Une entrée de page porte notamment : adresse physique, `present`, `writable`, `user`, `executable` (voir section 5 pour les flags réellement définis : `PAGE_PRESENT`, `PAGE_WRITE`, `PAGE_USER`, `PAGE_HUGE`).

Le bit `USER` distingue ce qui est réservé au kernel (`USER = 0`) de ce qui est accessible à un programme (`USER = 1`). C'est cette distinction qui permettra d'exécuter plusieurs programmes sans leur donner accès à toute la mémoire du kernel.

### 18.5 Les trois niveaux de mémoire

```
Application
    │  kmalloc()
    ▼
  HEAP        blocs de taille variable
    │  pages nécessaires
    ▼
  PMM         pages physiques (4 KiB)
    │
    ▼
 PAGING       adresses virtuelles → physiques, permissions
    │
    ▼
  CPU
```

| Couche | Rôle |
|---|---|
| PMM | possède les pages physiques |
| Paging | traduit et protège les adresses |
| Heap | distribue la mémoire dynamique |

### 18.6 Stockage et block layer (`block.c`) ✅

- Un **bloc logique** fait **512 octets** (identique au secteur du filesystem).
- Le block layer expose deux opérations, sans rien savoir des fichiers :

```c
block_read(100, buffer);    // bloc 100  → buffer en RAM
block_write(100, buffer);   // buffer RAM → bloc 100
```

Pour lui, « bloc 100 » est simplement un bloc. Derrière, il délègue à AHCI ou, à défaut, ATA PIO (section 12).

### 18.7 Filesystem (`fs.c`)

Le filesystem donne une signification aux blocs : sans lui, le bloc 500 n'a aucun sens ; avec lui, il contient les données de `photo.jpg`. La chaîne fondamentale d'accès à un fichier est :

```
nom de fichier
      ↓
entrée de répertoire   (nom → inode)
      ↓
inode                  (métadonnées)
      ↓
blocs du fichier
      ↓
block layer → driver → stockage
```

**Inode** ✅ — structure décrite :

```c
typedef struct {
    uint8_t  type;
    uint32_t size;
    uint32_t start_sector;
    uint32_t sector_count;
} FS_Inode;
```

L'inode décrit le fichier (type, taille, premier bloc, nombre de blocs) mais ne contient pas nécessairement son nom. Les blocs d'un fichier sont **contigus** : `start_sector` + `sector_count`.

**Répertoire** ✅ — sert d'index `nom → inode` ; chaque entrée contient un inode, un type et un nom. Séparer les deux évite que le répertoire ait à stocker directement nom, taille, blocs, permissions, dates, etc. Un répertoire pourrait ainsi contenir `doom`, `quake`, `test.nic`, chacun pointant vers un inode.

**Exemple illustratif** (valeurs d'exemple, pas l'état réel du disque) :

```
Répertoire :  name = hello.nic → inode = 1
Inode 1    :  type = FILE, size = 1200, start = 18, count = 3
Blocs      :  18, 19, 20  →  [contenu réel du fichier]
```

**Arbre logique** : physiquement, l'arbre de répertoires n'existe pas comme un arbre en RAM ; il est reconstruit à partir des répertoires et des inodes stockés sur le disque. À ce stade, seul le répertoire racine est réellement exploité (section 10).

**Superblock** ✅ — décrit le filesystem lui-même : magic, version, taille, nombre d'inodes, nombre de blocs, emplacement des structures. Au montage : lecture du superblock → vérification du magic → lecture de la configuration → filesystem disponible.

**Layout** :

| Structure | État |
|---|---|
| Superblock (secteur 0) | ✅ |
| Table des inodes (secteurs 1–16) | ✅ |
| Répertoire racine (secteur 17) | ✅ |
| Zone de données (secteur 18+) | ✅ |
| Bitmap d'allocation | ⏳ envisagée ; aujourd'hui l'allocation parcourt les inodes |

### 18.8 Lecture et écriture via l'API fichier ⏳

Le stockage n'est jamais exposé directement au programme. Chemin décrit pour `read(fd, buffer, 1000)` :

```
fd → File object → inode → position courante → blocs nécessaires
   → block layer → stockage → RAM → buffer utilisateur
```

Pour `write(fd, data, 1000)` : buffer utilisateur → kernel → filesystem (allocation de blocs, modification de l'inode, écriture des données) → block layer → stockage. Une seule opération logique peut modifier plusieurs structures.

`SYS_OPEN`, `SYS_CLOSE`, `SYS_READ` existent dans l'ABI (section 7), mais la couche fichier complète (file descriptors, position, objets ouverts) reste à écrire.

### 18.9 Caches ⏳

- **Block cache** : conserver en RAM les blocs récemment utilisés (première lecture → stockage, lectures suivantes → RAM). C'est un point de rencontre direct entre mémoire et stockage.
- **Page cache** : à un niveau plus avancé, le cache utilise directement des pages mémoire. Un fichier de 12 KiB devient : page 0 → fichier +0, page 1 → +4096, page 2 → +8192.

```
filesystem → page cache → pages RAM → block layer → storage
```

### 18.10 Responsabilités par fichier

| Fichier | Responsabilité | État |
|---|---|---|
| `paging.c` | tables de pages, mappings virtuels, permissions mémoire | ✅ |
| `pmm.c` | pages physiques libres/utilisées, allocation de pages | ✅ (détails ❓) |
| `heap.c` | `kmalloc()`, `kfree()`, allocations dynamiques | ✅ (détails ❓) |
| `block.c` | lecture/écriture de blocs, abstraction du périphérique | ✅ |
| `ahci.c` | communication avec le contrôleur, commandes de stockage, DMA | ✅ |
| `fs.c` | superblock, inodes, répertoires, allocation, fichiers | ✅ |
| `file.c` | file descriptors, position de lecture/écriture, objets ouverts | ⏳ |

> Il n'existe pas de `memory.c` : c'est `pmm.c` qui joue ce rôle.

Règle d'architecture : `fs.c` ne doit contenir aucun registre AHCI, et `ahci.c` ne doit pas connaître l'existence d'un fichier appelé `hello.nic`. Chaque couche ignore l'implémentation interne des couches situées plus bas.

### 18.11 Architecture cible

```
USER PROGRAM
      │
      ▼
 SYSTEM CALLS
      │
 ┌────┴──────────────┐
 │                   │
MEMORY            FILE API
 │                   │
HEAP              FILE LAYER
 │                   │
PAGING            FILESYSTEM
 │                   │
PMM               PAGE CACHE
 │                   │
 │              BLOCK LAYER
 │                   │
 │             STORAGE DRIVER
 │                   │
 └─────────┬─────────┘
           │
          RAM
```

Chemin d'un fichier en **lecture** : stockage → driver → block layer → cache / buffer RAM → filesystem → inode → file object → `read()` → programme.

### 18.12 Résumé

```
MÉMOIRE  = PMM (pages physiques) + Paging (adresses virtuelles) + Heap (allocations dynamiques)
STOCKAGE = Block layer (blocs bruts) + Filesystem (fichiers / dossiers)
RENCONTRE = Heap et Page cache en RAM, page cache au-dessus du block layer
```

| Couche | Rôle |
|---|---|
| PMM | possède les pages physiques |
| Paging | traduit et protège les adresses |
| Heap | distribue la mémoire dynamique |
| Block | manipule des blocs |
| FS | transforme les blocs en fichiers |
| Cache | maintient des copies des blocs en RAM |

---

*Règle de rédaction pour la suite : ne jamais présenter comme terminé un sous-système dont l'implémentation effective ne le justifie pas (XHCI, input, windowing, multitâche, retour de `SYS_EXIT`).*
