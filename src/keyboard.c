/* keyboard.c - PS/2 keyboard driver
 *
 * The keyboard controller raises IRQ1 on every key press/release and
 * leaves a one-byte scancode waiting at port 0x60. We translate that
 * scancode (US QWERTY, "scancode set 1", the PS/2 default) into ASCII
 * and hand it to the terminal.
 */

#include <stdint.h>
#include <stddef.h>
#include <gdt.h>
#inlcude <tss.h>

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

extern void terminal_putchar(char c);
extern void irq_install_handler(uint8_t irq, void (*handler)(void*));

/* Scancode -> ASCII, unshifted. Index is the raw scancode byte.
 * 0 means "no ASCII equivalent" (function keys, ctrl, etc). This
 * covers the standard US QWERTY layout for set 1. */
static const char scancode_ascii[128] = {
    0,  27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0, /* left ctrl */
    'a','s','d','f','g','h','j','k','l',';','\'','`',
    0, /* left shift */
    '\\','z','x','c','v','b','n','m',',','.','/',
    0, /* right shift */
    '*',
    0, /* alt */
    ' ', /* space */
    0, /* caps lock */
    0,0,0,0,0,0,0,0,0,0, /* F1-F10 */
    0, /* num lock */
    0, /* scroll lock */
    0,0,0,0,0,0,0,0,0, /* keypad, home, etc */
    0,0,0,0,0,0,0,0,0,0,0,0,0
};

static const char scancode_ascii_shift[128] = {
    0,  27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,
    'A','S','D','F','G','H','J','K','L',':','"','~',
    0,
    '|','Z','X','C','V','B','N','M','<','>','?',
    0,
    '*', 0, ' ', 0,
    0,0,0,0,0,0,0,0,0,0,
    0,0,
    0,0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0,0,0,0,0,0
};

#define SC_LSHIFT_DOWN 0x2A
#define SC_RSHIFT_DOWN 0x36
#define SC_LSHIFT_UP   0xAA
#define SC_RSHIFT_UP   0xB6
#define SC_RELEASE_BIT 0x80

static int shift_held = 0;

static void keyboard_handler(void* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);

    if (scancode == SC_LSHIFT_DOWN || scancode == SC_RSHIFT_DOWN) {
        shift_held = 1;
        return;
    }
    if (scancode == SC_LSHIFT_UP || scancode == SC_RSHIFT_UP) {
        shift_held = 0;
        return;
    }

    if (scancode & SC_RELEASE_BIT) return; /* key release, ignore rest */

    char c = shift_held ? scancode_ascii_shift[scancode] : scancode_ascii[scancode];
    if (c != 0) terminal_putchar(c);
}

void keyboard_install(void) {
    irq_install_handler(1, keyboard_handler);
}
