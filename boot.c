#include <stdint.h>

typedef void* EFI_HANDLE;


static void debug_putc(char c)
{
    __asm__ volatile (
        "outb %0, %1"
        :
        : "a"(c), "Nd"((uint16_t)0x402)
    );
}

static void debug_puts(const char *s)
{
    while (*s)
        debug_putc(*s++);
}

static void debug_hex64(uint64_t value)
{
    const char *hex = "0123456789ABCDEF";

    for (int i = 15; i >= 0; i--)
        debug_putc(hex[(value >> (i * 4)) & 0xF]);

    debug_putc('\r');
    debug_putc('\n');
}




static void serial_putc(char c)
{
    volatile uint8_t *com = (volatile uint8_t *)0x3F8;

    while ((com[5] & 0x20) == 0)
        ;

    com[0] = (uint8_t)c;
}

static void serial_puts(const char *s)
{
    while (*s)
        serial_putc(*s++);
}



void *memset(void *dst, int value, uint64_t size)
{
    uint8_t *p = (uint8_t *)dst;

    for (uint64_t i = 0; i < size; i++)
        p[i] = (uint8_t)value;

    return dst;
}


typedef uint64_t EFI_STATUS;
typedef uint16_t CHAR16;

#define EFI_SUCCESS 0
#define EFI_FILE_MODE_READ 1
#define EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL 2
#define EFI_ALLOCATE_ADDRESS 2
#define EFI_LOADER_DATA 4

typedef struct {
    uint64_t _Reset;
    EFI_STATUS (*OutputString)(void* This, CHAR16* String);
} EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL;

typedef struct {
    uint32_t d1;
    uint16_t d2;
    uint16_t d3;
    uint8_t d4[8];
} EFI_GUID;

typedef struct {
    uint32_t Version;
    uint32_t HorizontalResolution;
    uint32_t VerticalResolution;
    uint32_t PixelFormat;
    uint32_t PixelInformation[4];
    uint32_t PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFO;

typedef struct {
    uint32_t MaxMode;
    uint32_t Mode;
    EFI_GRAPHICS_OUTPUT_MODE_INFO* Info;
    uint64_t SizeOfInfo;
    uint64_t FrameBufferBase;
    uint64_t FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;

typedef struct {
    void* QueryMode;
    void* SetMode;
    void* Blt;
    EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE* Mode;
} EFI_GRAPHICS_OUTPUT_PROTOCOL;

typedef struct {
    uint8_t pad0[0x28];

    EFI_STATUS (*AllocatePages)(
        uint32_t Type,
        uint32_t MemoryType,
        uint64_t Pages,
        uint64_t* Memory
    ) __attribute__((ms_abi));

    uint8_t pad1[0x08];

    EFI_STATUS (*GetMemoryMap)(
        uint64_t* MemoryMapSize,
        void* MemoryMap,
        uint64_t* MapKey,
        uint64_t* DescriptorSize,
        uint32_t* DescriptorVersion
    ) __attribute__((ms_abi));

    EFI_STATUS (*AllocatePool)(
        uint32_t PoolType,
        uint64_t Size,
        void** Buffer
    ) __attribute__((ms_abi));

    




 
    uint8_t pad2[0xD0];

    EFI_STATUS (*OpenProtocol)(
        EFI_HANDLE Handle,
        EFI_GUID* Protocol,
        void** Interface,
        EFI_HANDLE AgentHandle,
        EFI_HANDLE ControllerHandle,
        uint32_t Attributes
    ) __attribute__((ms_abi));

    uint8_t pad3[0x20];

    EFI_STATUS (*LocateProtocol)(
        EFI_GUID* Protocol,
        void* Registration,
        void** Interface
    ) __attribute__((ms_abi));

    uint8_t pad4[0x28];

} EFI_BOOT_SERVICES;

typedef struct {
    uint64_t Header[3];

    void* FirmwareVendor;
    uint32_t FirmwareRevision;

    EFI_HANDLE ConsoleInHandle;
    void* ConIn;

    EFI_HANDLE ConsoleOutHandle;
    EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL* ConOut;

    EFI_HANDLE StandardErrorHandle;
    void* StdErr;

    void* RuntimeServices;
    EFI_BOOT_SERVICES* BootServices;

    uint64_t NumberOfTableEntries;
    void* ConfigurationTable;
} EFI_SYSTEM_TABLE;


 

typedef struct EFI_FILE EFI_FILE;

typedef EFI_STATUS (*EFI_FILE_OPEN)(
    EFI_FILE* This,
    EFI_FILE** NewHandle,
    CHAR16* FileName,
    uint64_t OpenMode,
    uint64_t Attributes
) __attribute__((ms_abi));

typedef EFI_STATUS (*EFI_FILE_CLOSE)(
    EFI_FILE* This
);

typedef EFI_STATUS (*EFI_FILE_READ)(
    EFI_FILE* This,
    uint64_t* BufferSize,
    void* Buffer
);

struct EFI_FILE {
    uint64_t Revision;
    EFI_FILE_OPEN Open;
    EFI_FILE_CLOSE Close;
    void* Delete;
    EFI_FILE_READ Read;
    void* Write;
    void* GetPosition;
    void* SetPosition;
    void* GetInfo;
    void* SetInfo;
    void* Flush;
};

typedef struct {
    uint64_t Revision;
    EFI_STATUS (*OpenVolume)(void* This, EFI_FILE** Root) __attribute__((ms_abi));
} EFI_SIMPLE_FILE_SYSTEM_PROTOCOL;


 

typedef struct {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} ELF64_EHDR;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} ELF64_PHDR;

#define PT_LOAD 1

#define EFI_BUFFER_TOO_SMALL 0x8000000000000005ULL


typedef struct {
    uint64_t framebuffer;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t pixel_format;

    uint64_t rsdp;

    uint64_t memory_map;
    uint64_t memory_map_size;
    uint64_t memory_descriptor_size;
    uint32_t memory_descriptor_version;
} BootInfo;


static void halt(void)
{
    for (;;)
        __asm__ volatile("hlt");
}


 

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t Data4[8];
} EFI_GUID_LOCAL;

typedef struct {
    EFI_GUID_LOCAL VendorGuid;
    uint64_t VendorTable;
} EFI_CONFIGURATION_TABLE;

static EFI_GUID_LOCAL ACPI20_GUID = {
    0x8868e871,
    0xe4f1,
    0x11d3,
    {0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81}
};

static uint64_t find_rsdp(EFI_SYSTEM_TABLE* st)
{
    uint64_t number =
        *(uint64_t *)((uint8_t *)st + 0x68);

    EFI_CONFIGURATION_TABLE* tables =
        *(EFI_CONFIGURATION_TABLE **)((uint8_t *)st + 0x70);

    for (uint64_t i = 0; i < number; i++) {
        if (tables[i].VendorGuid.Data1 == ACPI20_GUID.Data1 &&
            tables[i].VendorGuid.Data2 == ACPI20_GUID.Data2 &&
            tables[i].VendorGuid.Data3 == ACPI20_GUID.Data3) {

            for (int j = 0; j < 8; j++) {
                if (tables[i].VendorGuid.Data4[j] !=
                    ACPI20_GUID.Data4[j])
                    goto next;
            }

            return tables[i].VendorTable;
        }

    next:
        ;
    }

    return 0;
}

typedef EFI_STATUS (*EFI_EXIT_BOOT_SERVICES)(
    EFI_HANDLE ImageHandle,
    uint64_t MapKey
) __attribute__((ms_abi));

typedef EFI_STATUS (*EFI_GET_MEMORY_MAP)(
    uint64_t* MemoryMapSize,
    void* MemoryMap,
    uint64_t* MapKey,
    uint64_t* DescriptorSize,
    uint32_t* DescriptorVersion
) __attribute__((ms_abi));

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE* ST)
{
    debug_puts("[BOOT] efi_main\r\n");
    EFI_BOOT_SERVICES* BS = ST->BootServices;

     

    EFI_GUID gopGuid = {
        0x9042a9de, 0x23dc, 0x4a38,
        {0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a}
    };

    EFI_GRAPHICS_OUTPUT_PROTOCOL* gop = 0;

    debug_puts("[BOOT] Locate GOP\r\n");
    if (BS->LocateProtocol(&gopGuid, 0, (void**)&gop) != EFI_SUCCESS)
        halt();

    debug_puts("[BOOT] GOP OK\\r\\n");

    uint32_t* framebuffer =
        (uint32_t*)(uintptr_t)gop->Mode->FrameBufferBase;

    uint32_t width =
        gop->Mode->Info->HorizontalResolution;

    uint32_t height =
        gop->Mode->Info->VerticalResolution;

    uint32_t pitch =
        gop->Mode->Info->PixelsPerScanLine;

    uint32_t pixel_format =
        gop->Mode->Info->PixelFormat;


     

    EFI_GUID fsGuid = {
        0x964e5b22, 0x6459, 0x11d2,
        {0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b}
    };

    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL* fs = 0;

    debug_puts("[BOOT] GOP OK\r\n");
    debug_puts("[BOOT] Locate filesystem\r\n");
    if (BS->LocateProtocol(&fsGuid, 0, (void**)&fs) != EFI_SUCCESS)
        halt();

    EFI_FILE* root = 0;

    debug_puts("[BOOT] filesystem OK\r\n");
    debug_puts("[BOOT] OpenVolume\r\n");
    if (fs->OpenVolume(fs, &root) != EFI_SUCCESS)
        halt();


     

    static CHAR16 kernelName[] = {
        '\\','E','F','I','\\','B','O','O','T','\\',
        'k','e','r','n','e','l','.',
        'e','l','f',0
    };

    debug_puts("[BOOT] volume OK\r\n");
    EFI_FILE* kernelFile = 0;

    debug_puts("[BOOT] Opening kernel.elf\r\n");

    EFI_STATUS openStatus = root->Open(
        root,
        &kernelFile,
        kernelName,
        EFI_FILE_MODE_READ,
        0
    );

    if (openStatus != EFI_SUCCESS) {
        debug_puts("[BOOT] root->Open FAILED\r\n");
        halt();
    }

    debug_puts("[BOOT] kernel opened OK\r\n");


    


 

    static uint8_t elfBuffer[4 * 1024 * 1024];

    uint64_t elfSize = sizeof(elfBuffer);

    debug_puts("[BOOT] Reading kernel.elf\r\n");
    if (kernelFile->Read(
            kernelFile,
            &elfSize,
            elfBuffer
        ) != EFI_SUCCESS)
        halt();

    debug_puts("[BOOT] kernel read OK\r\n");
    kernelFile->Close(kernelFile);
    root->Close(root);


     

    ELF64_EHDR* eh = (ELF64_EHDR*)elfBuffer;

    if (eh->e_ident[0] != 0x7f ||
        eh->e_ident[1] != 'E' ||
        eh->e_ident[2] != 'L' ||
        eh->e_ident[3] != 'F')
        halt();

    if (eh->e_machine != 0x3e)
        halt();


     

    ELF64_PHDR* ph =
        (ELF64_PHDR*)(elfBuffer + eh->e_phoff);

    debug_puts("[BOOT] ELF header OK\r\n");
    for (uint16_t i = 0; i < eh->e_phnum; i++) {

        debug_puts("[BOOT] program header\r\n");
        if (ph[i].p_type != PT_LOAD)
            continue;

        uint64_t start = ph[i].p_vaddr & ~0xFFFULL;
        uint64_t end =
            (ph[i].p_vaddr + ph[i].p_memsz + 0xFFFULL)
            & ~0xFFFULL;

        uint64_t pages = (end - start) / 4096;

        uint64_t address = start;

        debug_puts("[BOOT] AllocatePages\r\n");
        if (BS->AllocatePages(
                EFI_ALLOCATE_ADDRESS,
                EFI_LOADER_DATA,
                pages,
                &address
            ) != EFI_SUCCESS)
            halt();

        uint8_t* destination =
            (uint8_t*)(uintptr_t)ph[i].p_vaddr;

        uint8_t* source =
            elfBuffer + ph[i].p_offset;

        for (uint64_t j = 0; j < ph[i].p_filesz; j++)
            destination[j] = source[j];

        for (uint64_t j = ph[i].p_filesz;
             j < ph[i].p_memsz;
             j++)
            destination[j] = 0;
    }


    
 

debug_puts("[BOOT] Getting memory map\r\n");

uint64_t memory_map_size = 0;
uint64_t map_key = 0;
uint64_t descriptor_size = 0;
uint32_t descriptor_version = 0;

EFI_GET_MEMORY_MAP GetMemoryMap =
    *(EFI_GET_MEMORY_MAP *)((uint8_t *)BS + 0x38);

debug_puts("[BOOT] Before GetMemoryMap\r\n");

EFI_STATUS status = GetMemoryMap(
    &memory_map_size,
    0,
    &map_key,
    &descriptor_size,
    &descriptor_version
);

debug_puts("[BOOT] After GetMemoryMap\r\n");

debug_puts("[BOOT] Status: ");
debug_hex64(status);

debug_puts("[BOOT] Required size: ");
debug_hex64(memory_map_size);

debug_puts("[BOOT] Descriptor size: ");
debug_hex64(descriptor_size);

if (status != EFI_BUFFER_TOO_SMALL)
    halt();

memory_map_size += descriptor_size * 8 + 4096;

void* memory_map = 0;

debug_puts("[BOOT] Allocating memory map buffer\\r\\n");

EFI_STATUS alloc_status = BS->AllocatePool(
    EFI_LOADER_DATA,
    memory_map_size,
    &memory_map
);

debug_puts("[BOOT] AllocatePool returned\\r\\n");

if (alloc_status != EFI_SUCCESS)
    halt();

debug_puts("[BOOT] Getting memory map second time\\r\\n");

if (GetMemoryMap(
        &memory_map_size,
        memory_map,
        &map_key,
        &descriptor_size,
        &descriptor_version
    ) != EFI_SUCCESS)
    halt();

debug_puts("[BOOT] Memory map OK\r\n");




 

uint64_t rsdp = find_rsdp(ST);

if (!rsdp) {
    debug_puts("[BOOT] ACPI RSDP not found\\r\\n");
    halt();
}

debug_puts("[BOOT] ACPI RSDP found\\r\\n");


    BootInfo boot = {
        (uint64_t)(uintptr_t)framebuffer,
        width,
        height,
        pitch,
        pixel_format,
        rsdp,
        (uint64_t)(uintptr_t)memory_map,
        memory_map_size,
        descriptor_size,
        descriptor_version
    };


     

    EFI_EXIT_BOOT_SERVICES ExitBootServices =
        *(EFI_EXIT_BOOT_SERVICES *)((uint8_t *)BS + 0xE8);

    debug_puts("[BOOT] ExitBootServices ptr: ");
    debug_hex64((uint64_t)(uintptr_t)ExitBootServices);

    debug_puts("[BOOT] Exiting UEFI Boot Services\\r\\n");

    EFI_STATUS exit_status =
        ExitBootServices(ImageHandle, map_key);

    debug_puts("[BOOT] EBS_RETURNED\r\n");

    if (exit_status != EFI_SUCCESS)
        halt();

    debug_puts("[BOOT] EBS_OK\r\n");
    debug_puts("[BOOT] segments loaded\r\n");
    debug_puts("[BOOT] jumping to kernel\r\n");
     

    debug_puts("[BOOT] segments loaded\\r\\n");
    debug_puts("[BOOT] jumping to kernel\\r\\n");

    typedef void (*KERNEL_ENTRY)(BootInfo*)
        __attribute__((sysv_abi));

    KERNEL_ENTRY kernel_entry =
        (KERNEL_ENTRY)(uintptr_t)eh->e_entry;

    kernel_entry(&boot);

    halt();
    return 0;
}
