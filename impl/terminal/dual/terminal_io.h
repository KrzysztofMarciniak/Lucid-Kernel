#ifndef LUCID_IMPL_TERMINAL_DUAL_IO_H
#define LUCID_IMPL_TERMINAL_DUAL_IO_H

void terminal_io_init(void);
int terminal_io_available(void);
void terminal_io_putc(char c);
void terminal_io_clear(void);
void terminal_io_flush(void);
int terminal_input_poll_key(void);

#endif
