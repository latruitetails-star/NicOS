#include "input.h"

#define INPUT_QUEUE_SIZE 64

static InputKey queue[INPUT_QUEUE_SIZE];
static uint32_t read_pos;
static uint32_t write_pos;

void input_init(void)
{
    read_pos = 0;
    write_pos = 0;
}

void input_push(
    KeyCode key,
    uint8_t pressed,
    uint8_t modifiers
)
{
    uint32_t next =
        (write_pos + 1) % INPUT_QUEUE_SIZE;

    if (next == read_pos)
        return;

    queue[write_pos].key = key;
    queue[write_pos].pressed = pressed;
    queue[write_pos].modifiers = modifiers;

    write_pos = next;
}

int input_poll(InputKey *event)
{
    if (!event)
        return 0;

    if (read_pos == write_pos)
        return 0;

    *event = queue[read_pos];

    read_pos =
        (read_pos + 1) % INPUT_QUEUE_SIZE;

    return 1;
}
