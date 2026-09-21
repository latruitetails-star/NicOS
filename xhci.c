#include <stdint.h>
#include "input.h"

static void xhci_debug(const char *s);

#define TRB_CYCLE       (1U << 0)
#define TRB_IOC         (1U << 5)
#define TRB_CHAIN       (1U << 4)
#define TRB_IDT         (1U << 6)
#define TRB_TYPE(t)     ((uint32_t)(t) << 10)
#define TRB_SLOT(s)     ((uint32_t)(s) << 24)
#define TRB_EP(e)       ((uint32_t)(e) << 16)

#define TRB_NORMAL       1
#define TRB_SETUP        2
#define TRB_DATA         3
#define TRB_STATUS       4
#define TRB_LINK         6

#define TRB_ENABLE_SLOT  9
#define TRB_ADDRESS_DEV 11
#define TRB_CONFIG_EP   12

#define TRB_TRANSFER    32
#define TRB_COMPLETION  33
#define TRB_PORT_STATUS 34

#define CC_SUCCESS       1
#define CC_SHORT_PACKET 13

#define CMD_RUN          (1U << 0)
#define CMD_RESET        (1U << 1)

#define STS_HALT         (1U << 0)
#define STS_CNR          (1U << 11)

#define PORT_CCS         (1U << 0)
#define PORT_PED         (1U << 1)
#define PORT_PR          (1U << 4)
#define PORT_SPEED_MASK  (0xFU << 10)
#define PORT_CSC         (1U << 17)
#define PORT_PRC         (1U << 21)

#define EP_TYPE_CONTROL  4
#define EP_TYPE_INT_IN   7

#define USB_REQ_GET_DESCRIPTOR  6
#define USB_REQ_SET_CONFIGURATION 9
#define HID_REQ_SET_PROTOCOL    11
#define HID_REQ_SET_IDLE        10

#define USB_DESC_DEVICE         1
#define USB_DESC_CONFIGURATION  2
#define USB_DESC_INTERFACE      4
#define USB_DESC_ENDPOINT       5
#define USB_DESC_HID            0x21

#define HID_CLASS                3
#define HID_SUBCLASS_BOOT        1
#define HID_PROTOCOL_KEYBOARD    1

#define ALIGN64 __attribute__((aligned(64)))

typedef struct {
    uint32_t d[4];
} TRB;

typedef struct {
    uint32_t d[8];
} XHCIContext32;

#include "pci.h"

static volatile uint8_t *xhci;
static uint32_t caplen;
static uint32_t op;
static uint32_t dboff;
static uint32_t rtsoff;

static uint8_t slot_id;
static uint8_t port_id;

static uint8_t ep0_mps = 8;
static uint8_t hid_interface;
static uint8_t hid_ep;
static uint16_t hid_mps = 8;
static uint8_t hid_interval;

static uint8_t keyboard_ready;

static uint8_t last_report[8];
static uint8_t current_report[8];

static ALIGN64 TRB command_ring[256];
static ALIGN64 TRB event_ring[256];
static ALIGN64 TRB control_ring[256];
static ALIGN64 TRB interrupt_ring[256];

static ALIGN64 uint64_t dcbaa[256];

static ALIGN64 uint8_t device_context[4096];
static ALIGN64 uint8_t input_context[4096];

static ALIGN64 uint64_t erst[2];

static ALIGN64 uint8_t descriptor_buffer[1024];

static uint32_t cmd_index;
static uint32_t event_index;
static uint8_t cmd_cycle = 1;
static uint8_t event_cycle = 1;

static uint32_t control_index;
static uint8_t control_cycle = 1;

static uint32_t interrupt_index;
static uint8_t interrupt_cycle = 1;

static inline uint32_t mmio32(uint32_t off)
{
    return *(volatile uint32_t *)(xhci + off);
}


static inline void mmio_write32(uint32_t off, uint32_t v)
{
    *(volatile uint32_t *)(xhci + off) = v;
}

static inline void mmio_write64(uint32_t off, uint64_t v)
{
    *(volatile uint64_t *)(xhci + off) = v;
}

static inline void zero_mem(void *ptr, uint64_t n)
{
    uint8_t *p = ptr;

    while (n--)
        *p++ = 0;
}

static inline uint64_t phys(void *p)
{
    


 
    return (uint64_t)(uintptr_t)p;
}

static void trb_clear(TRB *t)
{
    t->d[0] = 0;
    t->d[1] = 0;
    t->d[2] = 0;
    t->d[3] = 0;
}

static void ring_init(TRB *ring, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++)
        trb_clear(&ring[i]);

    ring[count - 1].d[0] = phys(ring);
    ring[count - 1].d[1] = phys(ring) >> 32;
    ring[count - 1].d[3] =
        TRB_TYPE(TRB_LINK) |
        TRB_CYCLE |
        (1U << 1);
}

static void command_ring_init(void)
{
    ring_init(command_ring, 256);
    cmd_index = 0;
    cmd_cycle = 1;
}

static void event_ring_init(void)
{
    for (uint32_t i = 0; i < 256; i++)
        trb_clear(&event_ring[i]);

    event_index = 0;
    event_cycle = 1;

    erst[0] = phys(event_ring);
    erst[1] = 256;
}

static void transfer_ring_init(TRB *ring, uint32_t *index, uint8_t *cycle)
{
    ring_init(ring, 256);
    *index = 0;
    *cycle = 1;
}

static void ring_advance(
    TRB *ring,
    uint32_t *index,
    uint8_t *cycle
)
{
    (void)ring;

    (*index)++;

    if (*index == 255) {
        *index = 0;
        *cycle ^= 1;
    }
}

static void ring_put(
    TRB *ring,
    uint32_t *index,
    uint8_t *cycle,
    uint32_t d0,
    uint32_t d1,
    uint32_t d2,
    uint32_t d3
)
{
    TRB *t = &ring[*index];

    t->d[0] = d0;
    t->d[1] = d1;
    t->d[2] = d2;
    t->d[3] = d3 | (*cycle ? TRB_CYCLE : 0);

    ring_advance(ring, index, cycle);
}

static int wait_event(
    uint8_t wanted_type,
    uint8_t wanted_slot,
    uint8_t *completion
)
{
    for (uint64_t timeout = 0; timeout < 100000000ULL; timeout++) {

        TRB *t = &event_ring[event_index];

        if ((t->d[3] & TRB_CYCLE) != event_cycle)
            continue;

        uint8_t type =
            (uint8_t)((t->d[3] >> 10) & 0x3F);

        uint8_t slot =
            (uint8_t)(t->d[3] >> 24);

        uint8_t cc =
            (uint8_t)(t->d[2] >> 24);

        event_index++;

        if (event_index == 256) {
            event_index = 0;
            event_cycle ^= 1;
        }

        

 
        uint32_t erdp =
            rtsoff + 0x20 + 0x18;

        mmio_write64(
            erdp,
            phys(&event_ring[event_index])
        );

        if (type == wanted_type &&
            (wanted_slot == 0 || slot == wanted_slot)) {

            if (completion)
                *completion = cc;

            return 1;
        }
    }

    return 0;
}

static int command(uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3,
                   uint8_t wanted_slot, uint8_t *cc)
{
    uint32_t before = cmd_index;

    ring_put(
        command_ring,
        &cmd_index,
        &cmd_cycle,
        d0, d1, d2, d3
    );

    mmio_write32(
        dboff + 0,
        0
    );

    (void)before;

    return wait_event(
        TRB_COMPLETION,
        wanted_slot,
        cc
    );
}

static int control_transfer(
    uint8_t bmRequestType,
    uint8_t bRequest,
    uint16_t wValue,
    uint16_t wIndex,
    uint16_t wLength,
    void *data,
    uint8_t direction_in
)
{


    uint32_t setup0 =
        ((uint32_t)wValue << 16) |
        ((uint32_t)bRequest << 8) |
        bmRequestType;

    uint32_t setup1 =
        ((uint32_t)wLength << 16) |
        wIndex;

    uint32_t trt;

    if (wLength == 0)
        trt = 0;
    else if (direction_in)
        trt = 3U << 16;
    else
        trt = 2U << 16;

    ring_put(
        control_ring,
        &control_index,
        &control_cycle,
        setup0,
        setup1,
        8,
        TRB_TYPE(TRB_SETUP) |
        TRB_IDT |
        TRB_CHAIN |
        trt
    );

    if (wLength != 0) {

        uint64_t p = phys(data);

        uint32_t flags =
            TRB_TYPE(TRB_DATA) |
            TRB_CHAIN;

        if (direction_in)
            flags |= (1U << 16);

        ring_put(
            control_ring,
            &control_index,
            &control_cycle,
            (uint32_t)p,
            (uint32_t)(p >> 32),
            wLength,
            flags
        );
    }

    ring_put(
        control_ring,
        &control_index,
        &control_cycle,
        0,
        0,
        0,
        TRB_TYPE(TRB_STATUS) |
        TRB_IOC |
        (direction_in ? 0 : (1U << 16))
    );

    {
        uint32_t base = control_index - 3;

        xhci_debug("[USB] CTRL IDX ");

        {
            char c[4];
            c[0] = (char)('0' + ((control_index / 100) % 10));
            c[1] = (char)('0' + ((control_index / 10) % 10));
            c[2] = (char)('0' + (control_index % 10));
            c[3] = 0;
            xhci_debug(c);
        }

        xhci_debug("\r\n");

        xhci_debug("[USB] SETUP ");

        {
            TRB *t = &control_ring[base];

            char c[9];
            uint32_t v = t->d[0];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> ((7 - i) * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug(" ");

        {
            TRB *t = &control_ring[base];

            char c[9];
            uint32_t v = t->d[1];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> ((7 - i) * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug(" ");

        {
            TRB *t = &control_ring[base];

            char c[9];
            uint32_t v = t->d[2];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> ((7 - i) * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug(" ");
        
        {
            TRB *t = &control_ring[base];

            char c[9];
            uint32_t v = t->d[3];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> ((7 - i) * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug("\r\n");

        xhci_debug("[USB] DATA D2 ");

        {
            TRB *t = &control_ring[base + 1];

            char c[9];
            uint32_t v = t->d[2];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> ((7 - i) * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug(" D3 ");

        {
            TRB *t = &control_ring[base + 1];

            char c[9];
            uint32_t v = t->d[3];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> ((7 - i) * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug("\r\n");

        xhci_debug("[USB] STATUS D3 ");

        {
            TRB *t = &control_ring[base + 2];

            char c[9];
            uint32_t v = t->d[3];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> ((7 - i) * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug("\r\n");
    }

    mmio_write32(
        dboff + slot_id * 4,
        1
    );

    uint8_t cc = 0;

    if (!wait_event(
            TRB_TRANSFER,
            slot_id,
            &cc))
    {
        xhci_debug("[USB] CTRL TIMEOUT\\r\\n");
        return 0;
    }

    xhci_debug("[USB] CTRL CC = ");

    {
        char c1 = (char)('0' + ((cc / 10) % 10));
        char c2 = (char)('0' + (cc % 10));

        xhci_debug(&c1);
        xhci_debug(&c2);
        xhci_debug("\\r\\n");
    }

    return cc == CC_SUCCESS ||
           cc == CC_SHORT_PACKET;
}

static void reset_controller(void)
{
    uint32_t cmd = mmio32(op + 0x00);

    cmd &= ~CMD_RUN;

    mmio_write32(op + 0x00, cmd);

    for (uint64_t i = 0; i < 10000000ULL; i++) {
        if (mmio32(op + 0x04) & STS_HALT)
            break;
    }

    mmio_write32(op + 0x00, CMD_RESET);

    for (uint64_t i = 0; i < 10000000ULL; i++) {
        if (!(mmio32(op + 0x00) & CMD_RESET))
            break;
    }

    for (uint64_t i = 0; i < 10000000ULL; i++) {
        if (!(mmio32(op + 0x04) & STS_CNR))
            break;
    }
}

static int setup_controller(void)
{
    reset_controller();

    zero_mem(dcbaa, sizeof(dcbaa));
    zero_mem(device_context, sizeof(device_context));
    zero_mem(input_context, sizeof(input_context));

    command_ring_init();
    event_ring_init();

    





 
    erst[0] = phys(event_ring);
    erst[1] = 256;

    dcbaa[0] = 0;

    mmio_write64(
        op + 0x30,
        phys(dcbaa)
    );

    mmio_write64(
        op + 0x18,
        phys(command_ring) |
        1
    );

    uint32_t runtime = rtsoff + 0x20;

    mmio_write32(runtime + 0x08, 1);

    mmio_write64(
        runtime + 0x10,
        phys(erst)
    );

    mmio_write64(
        runtime + 0x18,
        phys(event_ring)
    );

    mmio_write32(
        runtime + 0x04,
        0
    );

    mmio_write32(
        runtime + 0x00,
        2
    );

    uint32_t hcs1 = mmio32(0x04);

    uint8_t max_slots =
        (uint8_t)(hcs1 & 0xFF);

    if (max_slots == 0)
        return 0;

    mmio_write32(
        op + 0x38,
        max_slots
    );

    mmio_write32(
        op + 0x00,
        CMD_RUN
    );

    for (uint64_t i = 0; i < 10000000ULL; i++) {
        if (!(mmio32(op + 0x04) & STS_HALT))
            return 1;
    }

    return 0;
}

static int find_port(void)
{
    uint32_t hcs1 = mmio32(0x04);

    uint8_t ports =
        (uint8_t)((hcs1 >> 24) & 0xFF);

    if (ports == 0)
        return 0;

    for (uint8_t p = 1; p <= ports; p++) {

        uint32_t v =
            mmio32(op + 0x400 + ((p - 1) * 0x10));

        if (!(v & PORT_CCS))
            continue;

        if (v & PORT_PED) {
            port_id = p;
            return 1;
        }

        

 
        uint32_t clear =
            v &
            ~(PORT_CSC |
              PORT_PRC |
              PORT_PR);

        mmio_write32(
            op + 0x400 + ((p - 1) * 0x10),
            clear | PORT_PR
        );

        for (uint64_t i = 0; i < 10000000ULL; i++) {

            v =
                mmio32(
                    op + 0x400 +
                    ((p - 1) * 0x10)
                );

            if (!(v & PORT_PR) &&
                (v & PORT_PED))
                break;
        }

        v =
            mmio32(
                op + 0x400 +
                ((p - 1) * 0x10)
            );

        if (v & PORT_PED) {
            port_id = p;
            return 1;
        }
    }

    return 0;
}

static uint8_t port_speed(void)
{
    uint32_t v =
        mmio32(
            op + 0x400 +
            ((port_id - 1) * 0x10)
        );

    return (uint8_t)((v >> 10) & 0xF);
}

static int enable_slot(void)
{
    uint8_t cc = 0;

    if (!command(
            0,
            0,
            0,
            TRB_TYPE(TRB_ENABLE_SLOT),
            0,
            &cc))
        return 0;

    


 
    TRB *e =
        &event_ring[
            event_index == 0
                ? 255
                : event_index - 1
        ];

    slot_id =
        (uint8_t)(e->d[3] >> 24);

    return slot_id != 0;
}

static void setup_ep0_context(uint8_t speed)
{
    uint32_t *ic = (uint32_t *)input_context;

    zero_mem(input_context, sizeof(input_context));

    transfer_ring_init(control_ring, &control_index, &control_cycle);

    

 
    ic[1] = 0x3;

    uint32_t *slot = ic + 8;
    uint32_t *ep0  = ic + 16;

    




 
    slot[0] =
        ((uint32_t)speed << 20) |
        (1U << 27);

    slot[1] =
        ((uint32_t)port_id << 16);

    





 
    ep0[0] =
        (3U << 1);

    ep0[1] =
        (EP_TYPE_CONTROL << 3) |
        (3U << 1) |
        ((uint32_t)ep0_mps << 16);

    ep0[2] =
        (uint32_t)phys(control_ring) | 1U;

    ep0[3] =
        (uint32_t)(phys(control_ring) >> 32);
}

static int address_device(void)
{
    uint8_t speed = port_speed();

    ep0_mps = (speed >= 3) ? 64 : 8;

    setup_ep0_context(speed);

    uint8_t cc = 0;

    if (!command(
            (uint32_t)phys(input_context),
            (uint32_t)(phys(input_context) >> 32),
            0,
            TRB_TYPE(TRB_ADDRESS_DEV) |
            TRB_SLOT(slot_id),
            slot_id,
            &cc))
        return 0;

    return cc == CC_SUCCESS;
}

static int get_device_descriptor(void)
{
    zero_mem(descriptor_buffer, sizeof(descriptor_buffer));

    return control_transfer(
        0x80,
        USB_REQ_GET_DESCRIPTOR,
        (USB_DESC_DEVICE << 8),
        0,
        18,
        descriptor_buffer,
        1
    );
}

static int get_configuration_descriptor(void)
{
    xhci_debug("[USB] CFG 1 GET 9\r\n");

    zero_mem(descriptor_buffer, sizeof(descriptor_buffer));

    if (!control_transfer(
            0x80,
            USB_REQ_GET_DESCRIPTOR,
            (USB_DESC_CONFIGURATION << 8),
            0,
            9,
            descriptor_buffer,
            1))
    {
        xhci_debug("[USB] CFG 1 FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] CFG 1 OK\r\n");

    uint16_t total =
        (uint16_t)descriptor_buffer[2] |
        ((uint16_t)descriptor_buffer[3] << 8);

    xhci_debug("[USB] CFG LEN = ");

    {
        char c1 = (char)('0' + ((total / 100) % 10));
        char c2 = (char)('0' + ((total / 10) % 10));
        char c3 = (char)('0' + (total % 10));

        xhci_debug(&c1);
        xhci_debug(&c2);
        xhci_debug(&c3);
        xhci_debug("\r\n");
    }

    if (total < 9 || total > sizeof(descriptor_buffer))
        return 0;

    xhci_debug("[USB] CFG 2 GET FULL\r\n");

    zero_mem(descriptor_buffer, sizeof(descriptor_buffer));

    if (!control_transfer(
            0x80,
            USB_REQ_GET_DESCRIPTOR,
            (USB_DESC_CONFIGURATION << 8),
            0,
            total,
            descriptor_buffer,
            1))
    {
        xhci_debug("[USB] CFG 2 FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] CFG 2 OK\r\n");

    return 1;
}
static int parse_hid_configuration(
    uint8_t *configuration,
    uint16_t length
)
{
    uint16_t off = 0;

    uint8_t interface_found = 0;

    while (off + 2 <= length) {

        uint8_t len =
            configuration[off];

        uint8_t type =
            configuration[off + 1];

        if (len < 2 ||
            off + len > length)
            break;

        if (type == USB_DESC_INTERFACE &&
            len >= 9) {

            uint8_t cls =
                configuration[off + 5];

            uint8_t sub =
                configuration[off + 6];

            uint8_t proto =
                configuration[off + 7];

            if (cls == HID_CLASS &&
                sub == HID_SUBCLASS_BOOT &&
                proto == HID_PROTOCOL_KEYBOARD) {

                interface_found = 1;
            }
        }

        if (type == USB_DESC_ENDPOINT &&
            len >= 7 &&
            interface_found) {

            uint8_t addr =
                configuration[off + 2];

            uint8_t attr =
                configuration[off + 3];

            uint16_t mps =
                configuration[off + 4] |
                ((uint16_t)configuration[off + 5] << 8);

            uint8_t interval =
                configuration[off + 6];

            if ((addr & 0x80) &&
                ((attr & 3) == 3)) {

                hid_ep = addr & 0x0F;
                hid_mps = mps & 0x7FF;
                hid_interval = interval;

                xhci_debug("[USB] HID EP = ");
                {
                    char c[3];
                    c[0] = '0' + ((hid_ep / 10) % 10);
                    c[1] = '0' + (hid_ep % 10);
                    c[2] = 0;
                    xhci_debug(c);
                }
                xhci_debug(" MPS = ");
                {
                    char c[6];
                    c[0] = '0' + ((hid_mps / 10000) % 10);
                    c[1] = '0' + ((hid_mps / 1000) % 10);
                    c[2] = '0' + ((hid_mps / 100) % 10);
                    c[3] = '0' + ((hid_mps / 10) % 10);
                    c[4] = '0' + (hid_mps % 10);
                    c[5] = 0;
                    xhci_debug(c);
                }
                xhci_debug(" INT = ");
                {
                    char c[4];
                    c[0] = '0' + ((hid_interval / 100) % 10);
                    c[1] = '0' + ((hid_interval / 10) % 10);
                    c[2] = '0' + (hid_interval % 10);
                    c[3] = 0;
                    xhci_debug(c);
                }
                xhci_debug("\r\n");

                return 1;
            }
        }

        off += len;
    }

    return 0;
}

static void setup_hid_endpoint_context(void)
{
    zero_mem(input_context, sizeof(input_context));

    uint32_t *ic =
        (uint32_t *)input_context;

    

 
    uint8_t dci =
        (uint8_t)((hid_ep * 2) + 1);

    




 
    ic[1] |= (1U << 0);
    ic[1] |= (1U << dci);

    



 
    uint32_t *slot =
        ic + 8;

    slot[0] =
        (slot[0] & ~(0x1FU << 27)) |
        ((uint32_t)dci << 27);

    transfer_ring_init(
        interrupt_ring,
        &interrupt_index,
        &interrupt_cycle
    );

    






 
    uint32_t *ep =
        ic + (8 * (dci + 1));

    ep[0] =
        ((uint32_t)hid_interval << 16);

    ep[1] =
        (EP_TYPE_INT_IN << 3) |
        (3U << 1) |
        ((uint32_t)hid_mps << 16);

    ep[2] =
        (uint32_t)phys(interrupt_ring) |
        1;

    ep[3] = 0;
}

static int configure_endpoint(void)
{
    setup_hid_endpoint_context();

    uint32_t *ic = (uint32_t *)input_context;

    xhci_debug("[USB] INPUT CTX:\r\n");

    for (uint32_t i = 0; i < 32; i++) {
        uint32_t v = ic[i];
        char b[9];
        char n[3];

        for (int j = 0; j < 8; j++)
            b[j] = "0123456789ABCDEF"[(v >> (28 - j * 4)) & 0xF];

        b[8] = 0;

        n[0] = "0123456789ABCDEF"[(i >> 4) & 0xF];
        n[1] = "0123456789ABCDEF"[i & 0xF];
        n[2] = 0;

        xhci_debug("DW");
        xhci_debug(n);
        xhci_debug(" = ");
        xhci_debug(b);
        xhci_debug("\r\n");
    }
    uint8_t cc = 0;

    int ok = command(
        (uint32_t)phys(input_context),
        (uint32_t)(phys(input_context) >> 32),
        0,
        TRB_TYPE(TRB_CONFIG_EP) |
        TRB_SLOT(slot_id),
        slot_id,
        &cc
    );

    xhci_debug("[USB] CONFIG EP CC = ");
    {
        char c[3];
        c[0] = "0123456789ABCDEF"[(cc >> 4) & 0xF];
        c[1] = "0123456789ABCDEF"[cc & 0xF];
        c[2] = 0;
        xhci_debug(c);
    }
    xhci_debug("\r\n");

    return ok && cc == CC_SUCCESS;
}

static int set_hid_protocol(void)
{
    

 
    if (!control_transfer(
            0x21,
            HID_REQ_SET_PROTOCOL,
            0,
            hid_interface,
            0,
            0,
            0))
        return 0;

    

 
    return control_transfer(
        0x21,
        HID_REQ_SET_IDLE,
        0,
        hid_interface,
        0,
        0,
        0
    );
}

static int set_configuration(void)
{
    uint8_t configuration =
        descriptor_buffer[5];

    return control_transfer(
        0x00,
        USB_REQ_SET_CONFIGURATION,
        configuration,
        0,
        0,
        0,
        0
    );
}

static void debug_event_ring(void)
{
    TRB *t = &event_ring[event_index];

    if ((t->d[3] & TRB_CYCLE) != event_cycle)
        return;

    uint8_t type =
        (uint8_t)((t->d[3] >> 10) & 0x3F);

    uint8_t cc =
        (uint8_t)((t->d[2] >> 24) & 0xFF);

    uint8_t slot =
        (uint8_t)(t->d[3] >> 24);

    xhci_debug("[USB] EVENT type=");

    {
        char c[3];

        c[0] = "0123456789ABCDEF"[(type >> 4) & 0xF];
        c[1] = "0123456789ABCDEF"[type & 0xF];
        c[2] = 0;

        xhci_debug(c);
    }

    xhci_debug(" CC=");

    {
        char c[3];

        c[0] = "0123456789ABCDEF"[(cc >> 4) & 0xF];
        c[1] = "0123456789ABCDEF"[cc & 0xF];
        c[2] = 0;

        xhci_debug(c);
    }

    xhci_debug(" SLOT=");

    {
        char c[3];

        c[0] = "0123456789ABCDEF"[(slot >> 4) & 0xF];
        c[1] = "0123456789ABCDEF"[slot & 0xF];
        c[2] = 0;

        xhci_debug(c);
    }

    xhci_debug("\r\n");
}

static int queue_keyboard_transfer(void)
{
    uint64_t p = phys(current_report);

    uint16_t length = hid_mps;

    if (length > sizeof(current_report))
        length = sizeof(current_report);

    if (length == 0)
        length = 8;

    uint32_t before = interrupt_index;

    xhci_debug("[USB] QUEUE KEYBOARD\r\n");

    ring_put(
        interrupt_ring,
        &interrupt_index,
        &interrupt_cycle,
        (uint32_t)p,
        (uint32_t)(p >> 32),
        length,
        TRB_TYPE(TRB_NORMAL) |
        TRB_IOC
    );

    xhci_debug("[USB] INT IDX = ");

    {
        char c[9];
        uint32_t v = interrupt_index;

        for (int i = 7; i >= 0; i--) {
            c[i] = "0123456789ABCDEF"[v & 0xF];
            v >>= 4;
        }

        c[8] = 0;
        xhci_debug(c);
    }

    xhci_debug("\r\n");

    uint8_t dci =
        (uint8_t)((hid_ep * 2) + 1);

    xhci_debug("[USB] DCI = ");

    {
        char c[3];

        c[0] = "0123456789ABCDEF"[(dci >> 4) & 0xF];
        c[1] = "0123456789ABCDEF"[dci & 0xF];
        c[2] = 0;

        xhci_debug(c);
    }

    xhci_debug("\r\n");

    mmio_write32(
        dboff + slot_id * 4,
        dci
    );

    (void)before;

    return 1;
}

static uint8_t hid_to_set1(uint8_t k)
{
    switch (k) {
        case 0x04: return 0x1E;
        case 0x05: return 0x30;
        case 0x06: return 0x2E;
        case 0x07: return 0x20;
        case 0x08: return 0x12;
        case 0x09: return 0x21;
        case 0x0A: return 0x22;
        case 0x0B: return 0x23;
        case 0x0C: return 0x17;
        case 0x0D: return 0x24;
        case 0x0E: return 0x25;
        case 0x0F: return 0x26;
        case 0x10: return 0x32;
        case 0x11: return 0x31;
        case 0x12: return 0x18;
        case 0x13: return 0x19;
        case 0x14: return 0x10;
        case 0x15: return 0x13;
        case 0x16: return 0x1F;
        case 0x17: return 0x14;
        case 0x18: return 0x16;
        case 0x19: return 0x2F;
        case 0x1A: return 0x11;
        case 0x1B: return 0x2D;
        case 0x1C: return 0x15;

        case 0x1D: return 0x02;
        case 0x1E: return 0x03;
        case 0x1F: return 0x04;
        case 0x20: return 0x05;
        case 0x21: return 0x06;
        case 0x22: return 0x07;
        case 0x23: return 0x08;
        case 0x24: return 0x09;
        case 0x25: return 0x0A;
        case 0x26: return 0x0B;

        case 0x28: return 0x1C;
        case 0x29: return 0x01;
        case 0x2A: return 0x0E;
        case 0x2B: return 0x0F;
        case 0x2C: return 0x39;

        case 0x2D: return 0x0C;
        case 0x2E: return 0x0D;
        case 0x2F: return 0x1A;
        case 0x30: return 0x1B;
        case 0x31: return 0x2B;
        case 0x33: return 0x27;
        case 0x34: return 0x28;
        case 0x35: return 0x29;
        case 0x36: return 0x33;
        case 0x37: return 0x34;
        case 0x38: return 0x35;

        case 0x39: return 0x3A;

        case 0x4F: return 0x4D;
        case 0x50: return 0x4B;
        case 0x51: return 0x50;
        case 0x52: return 0x48;

        case 0x53: return 0x45;

        case 0x54: return 0x35;
        case 0x55: return 0x37;
        case 0x56: return 0x4A;
        case 0x57: return 0x4E;

        case 0x58: return 0x1C;

        default:
            return 0;
    }
}

static void xhci_debug(const char *s)
{
    while (*s) {
        __asm__ volatile (
            "outb %0, %1"
            :
            : "a"(*s), "Nd"((uint16_t)0x402)
        );
        s++;
    }
}

int xhci_keyboard_init(void)
{
    xhci_debug("[USB] 1 PCI\r\n");

    PCI_Device pci;

    if (!pci_find_xhci(&pci)) {
        xhci_debug("[USB] PCI xHCI NOT FOUND\r\n");
        return 0;
    }

    xhci_debug("[USB] 2 PCI OK\r\n");

    if (!pci.bar[0]) {
        xhci_debug("[USB] BAR0 INVALID\r\n");
        return 0;
    }

    xhci = (volatile uint8_t *)(uintptr_t)pci.bar[0];

    xhci_debug("[USB] 3 MMIO\r\n");

    

 
    caplen = *(volatile uint8_t *)(xhci + 0x00);
    op = caplen;

    dboff = mmio32(0x14) & ~3U;
    rtsoff = mmio32(0x18) & ~31U;

    xhci_debug("[USB] 4 CAPS OK\r\n");

    

 
    if (!setup_controller()) {
        xhci_debug("[USB] 5 CONTROLLER FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] 5 CONTROLLER OK\r\n");

    

 
    if (!find_port()) {
        xhci_debug("[USB] 6 NO USB PORT\r\n");
        return 0;
    }

    xhci_debug("[USB] 6 PORT OK\r\n");

    uint8_t speed = port_speed();

    xhci_debug("[USB] SPEED = ");

    {
        char c = (char)('0' + speed);
        xhci_debug(&c);
        xhci_debug("\r\n");
    }

    

 
    if (!enable_slot()) {
        xhci_debug("[USB] 7 ENABLE SLOT FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] 7 SLOT OK\r\n");

    

 
    dcbaa[slot_id] = phys(device_context);

    


 
    if (speed >= 3)
        ep0_mps = 64;
    else
        ep0_mps = 8;

    

 
    if (!address_device()) {
        xhci_debug("[USB] 8 ADDRESS DEVICE FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] 8 ADDRESS OK\r\n");

    

 
    if (!get_device_descriptor()) {
        xhci_debug("[USB] 9 DEVICE DESC FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] 9 DEVICE DESC OK\r\n");

{
    uint32_t *dc = (uint32_t *)device_context;

    xhci_debug("[USB] EP0 DEQ LO = ");

    {
        uint32_t v = dc[10];
        char c[9];

        for (int i = 0; i < 8; i++) {
            uint8_t n = (uint8_t)((v >> (28 - i * 4)) & 0xF);
            c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
        }

        c[8] = 0;
        xhci_debug(c);
    }

    xhci_debug("\r\n");

    xhci_debug("[USB] EP0 DEQ HI = ");

    {
        uint32_t v = dc[11];
        char c[9];

        for (int i = 0; i < 8; i++) {
            uint8_t n = (uint8_t)((v >> (28 - i * 4)) & 0xF);
            c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
        }

        c[8] = 0;
        xhci_debug(c);
    }

    xhci_debug("\r\n");
}

    




 
    if (descriptor_buffer[7] != 0) {
        if (speed <= 2) {
            ep0_mps = descriptor_buffer[7];
        } else {
            ep0_mps = 64;
        }
    }

    

 

    {
        uint32_t *dc = (uint32_t *)device_context;

        xhci_debug("[USB] EP0 CTX DW0 = ");

        {
            uint32_t v = dc[8];
            char c[9];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> (28 - i * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug("\r\n");

        xhci_debug("[USB] EP0 CTX DW1 = ");

        {
            uint32_t v = dc[9];
            char c[9];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> (28 - i * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug("\r\n");

        xhci_debug("[USB] EP0 DEQ LO = ");

        {
            uint32_t v = dc[10];
            char c[9];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> (28 - i * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug("\r\n");

        xhci_debug("[USB] EP0 DEQ HI = ");

        {
            uint32_t v = dc[11];
            char c[9];

            for (int i = 0; i < 8; i++) {
                uint8_t n = (uint8_t)((v >> (28 - i * 4)) & 0xF);
                c[i] = (char)(n < 10 ? '0' + n : 'A' + n - 10);
            }

            c[8] = 0;
            xhci_debug(c);
        }

        xhci_debug("\r\n");
    }

    if (!get_configuration_descriptor()) {
        xhci_debug("[USB] 10 CONFIG DESC FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] 10 CONFIG DESC OK\r\n");

    

 
    uint16_t total =
        (uint16_t)descriptor_buffer[2] |
        ((uint16_t)descriptor_buffer[3] << 8);

    if (total > sizeof(descriptor_buffer))
        total = sizeof(descriptor_buffer);

    if (!parse_hid_configuration(descriptor_buffer, total)) {
        xhci_debug("[USB] 11 HID KEYBOARD NOT FOUND\r\n");
        return 0;
    }

    xhci_debug("[USB] 11 HID KEYBOARD FOUND\r\n");

    

 
    if (!configure_endpoint()) {
        xhci_debug("[USB] 12 CONFIG EP FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] 12 CONFIG EP OK\r\n");

    

 
    if (!set_configuration()) {
        xhci_debug("[USB] 13 SET CONFIG FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] 13 SET CONFIG OK\r\n");

    

 
    if (!set_hid_protocol()) {
        xhci_debug("[USB] 14 HID SETUP FAIL\r\n");
        return 0;
    }

    xhci_debug("[USB] 14 HID SETUP OK\r\n");

    zero_mem(last_report, sizeof(last_report));
    zero_mem(current_report, sizeof(current_report));

    

 
    if (!queue_keyboard_transfer()) {
        xhci_debug("[USB] 15 QUEUE FAIL\r\n");
        return 0;
    }

    keyboard_ready = 1;

    xhci_debug("[USB] 15 KEYBOARD READY\r\n");



    return 1;
}


static KeyCode hid_to_keycode(uint8_t hid)
{
    switch (hid) {

        case 0x04: return KEY_A;
        case 0x05: return KEY_B;
        case 0x06: return KEY_C;
        case 0x07: return KEY_D;
        case 0x08: return KEY_E;
        case 0x09: return KEY_F;
        case 0x0A: return KEY_G;
        case 0x0B: return KEY_H;
        case 0x0C: return KEY_I;
        case 0x0D: return KEY_J;
        case 0x0E: return KEY_K;
        case 0x0F: return KEY_L;
        case 0x10: return KEY_M;
        case 0x11: return KEY_N;
        case 0x12: return KEY_O;
        case 0x13: return KEY_P;
        case 0x14: return KEY_Q;
        case 0x15: return KEY_R;
        case 0x16: return KEY_S;
        case 0x17: return KEY_T;
        case 0x18: return KEY_U;
        case 0x19: return KEY_V;
        case 0x1A: return KEY_W;
        case 0x1B: return KEY_X;
        case 0x1C: return KEY_Y;
        case 0x1D: return KEY_Z;

        case 0x1E: return KEY_1;
        case 0x1F: return KEY_2;
        case 0x20: return KEY_3;
        case 0x21: return KEY_4;
        case 0x22: return KEY_5;
        case 0x23: return KEY_6;
        case 0x24: return KEY_7;
        case 0x25: return KEY_8;
        case 0x26: return KEY_9;
        case 0x27: return KEY_0;

        case 0x28: return KEY_ENTER;
        case 0x29: return KEY_ESC;
        case 0x2A: return KEY_BACKSPACE;
        case 0x2B: return KEY_TAB;
        case 0x2C: return KEY_SPACE;

        case 0x2D: return KEY_MINUS;
        case 0x2E: return KEY_EQUAL;
        case 0x2F: return KEY_LBRACKET;
        case 0x30: return KEY_RBRACKET;
        case 0x31: return KEY_BACKSLASH;
        case 0x33: return KEY_SEMICOLON;
        case 0x34: return KEY_APOSTROPHE;
        case 0x35: return KEY_GRAVE;
        case 0x36: return KEY_COMMA;
        case 0x37: return KEY_DOT;
        case 0x38: return KEY_SLASH;

        case 0x39: return KEY_CAPSLOCK;

        case 0x3A: return KEY_F1;
        case 0x3B: return KEY_F2;
        case 0x3C: return KEY_F3;
        case 0x3D: return KEY_F4;
        case 0x3E: return KEY_F5;
        case 0x3F: return KEY_F6;
        case 0x40: return KEY_F7;
        case 0x41: return KEY_F8;
        case 0x42: return KEY_F9;
        case 0x43: return KEY_F10;
        case 0x44: return KEY_F11;
        case 0x45: return KEY_F12;

        case 0x49: return KEY_INSERT;
        case 0x4A: return KEY_HOME;
        case 0x4B: return KEY_PAGEUP;
        case 0x4C: return KEY_DELETE;
        case 0x4D: return KEY_END;
        case 0x4E: return KEY_PAGEDOWN;

        case 0x4F: return KEY_RIGHT;
        case 0x50: return KEY_LEFT;
        case 0x51: return KEY_DOWN;
        case 0x52: return KEY_UP;

        case 0xE0: return KEY_LCTRL;
        case 0xE1: return KEY_LSHIFT;
        case 0xE2: return KEY_LALT;
        case 0xE4: return KEY_RCTRL;
        case 0xE5: return KEY_RSHIFT;
        case 0xE6: return KEY_RALT;

        default:
            return KEY_NONE;
    }
}

int xhci_keyboard_read_scancode(void)
{
    if (!keyboard_ready)
        return 0;

    uint8_t cc = 0;

    if (!wait_event(
            TRB_TRANSFER,
            slot_id,
            &cc))
        return 0;

    

 
    if (cc != CC_SUCCESS &&
        cc != CC_SHORT_PACKET) {
        queue_keyboard_transfer();
        return 0;
    }

    xhci_debug("[USB] HID REPORT = ");

    for (int i = 0; i < 8; i++) {
        char c[3];

        c[0] = "0123456789ABCDEF"[(current_report[i] >> 4) & 0xF];
        c[1] = "0123456789ABCDEF"[current_report[i] & 0xF];
        c[2] = 0;

        xhci_debug(c);
        xhci_debug(" ");
    }

    xhci_debug("\r\n");

    int result = 0;

    








 
    uint8_t modifiers = 0;

    if (current_report[0] & 0x22)
        modifiers |= INPUT_MOD_SHIFT;

    if (current_report[0] & 0x11)
        modifiers |= INPUT_MOD_CTRL;

    if (current_report[0] & 0x44)
        modifiers |= INPUT_MOD_ALT;

    

 
    for (int i = 2; i < 8; i++) {
        uint8_t old = last_report[i];
        uint8_t now = current_report[i];

        if (old == now)
            continue;

        

 
        if (old != 0) {
            KeyCode key = hid_to_keycode(old);

            if (key != KEY_NONE)
                input_push(key, 0, modifiers);
        }

        

 
        if (now != 0) {
            KeyCode key = hid_to_keycode(now);

            if (key != KEY_NONE) {
                input_push(key, 1, modifiers);

                

 
                uint8_t sc = hid_to_set1(now);

                if (sc && result == 0)
                    result = sc;
            }
        }

        last_report[i] = now;
    }

    

 
    queue_keyboard_transfer();

    return result;
}
