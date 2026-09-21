#ifndef NICOS_INPUT_H
#define NICOS_INPUT_H

#include <stdint.h>

typedef enum {
    KEY_NONE = 0,

    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G,
    KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N,
    KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U,
    KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,

    KEY_1, KEY_2, KEY_3, KEY_4, KEY_5,
    KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,

    KEY_ENTER,
    KEY_ESC,
    KEY_BACKSPACE,
    KEY_TAB,
    KEY_SPACE,

    KEY_MINUS,
    KEY_EQUAL,
    KEY_LBRACKET,
    KEY_RBRACKET,
    KEY_BACKSLASH,
    KEY_SEMICOLON,
    KEY_APOSTROPHE,
    KEY_GRAVE,
    KEY_COMMA,
    KEY_DOT,
    KEY_SLASH,

    KEY_CAPSLOCK,

    KEY_F1, KEY_F2, KEY_F3, KEY_F4,
    KEY_F5, KEY_F6, KEY_F7, KEY_F8,
    KEY_F9, KEY_F10, KEY_F11, KEY_F12,

    KEY_LEFT,
    KEY_RIGHT,
    KEY_UP,
    KEY_DOWN,

    KEY_INSERT,
    KEY_DELETE,
    KEY_HOME,
    KEY_END,
    KEY_PAGEUP,
    KEY_PAGEDOWN,

    KEY_LSHIFT,
    KEY_RSHIFT,
    KEY_LCTRL,
    KEY_RCTRL,
    KEY_LALT,
    KEY_RALT
} KeyCode;

typedef struct {
    KeyCode key;
    uint8_t pressed;
    uint8_t modifiers;
} InputKey;

#define INPUT_MOD_SHIFT 0x01
#define INPUT_MOD_CTRL  0x02
#define INPUT_MOD_ALT   0x04
#define INPUT_MOD_CAPS  0x08

void input_init(void);
void input_push(KeyCode key, uint8_t pressed, uint8_t modifiers);
int input_poll(InputKey *event);

#endif
