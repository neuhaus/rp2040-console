#ifndef RP2040_CONSOLE_CONSOLE_H
#define RP2040_CONSOLE_CONSOLE_H

#include <termios.h>

/*
 * The console: what the program sees when it reads a character, and the
 * shortest path to the screen.
 */

/*
 * Input with the tty's input processing applied - what getchar() reaches
 * through the prefix header. Blocks until a character arrives.
 */
int console_getchar(void);

/* Write exactly these bytes. No newline appended, nothing interpreted. */
void console_rawout(const char *s);

/* Write exactly count bytes. This is what stdout and stderr write to. */
void console_write(const void *buf, size_t count);

/*
 * Read from the console as a terminal does: blocks for the first byte, then
 * returns what is waiting, at most a line. What read() does for fd 0.
 */
int console_read(void *buf, size_t count);

/*
 * The terminal state the engine claims to enforce. The input path reads it to
 * decide what to do with each character, which is what makes
 * tcsetattr() more than a call that returns zero.
 */
struct termios *console_termios(void);

/* Print the memory map over the console. */
void report_memory(void);

#endif /* RP2040_CONSOLE_CONSOLE_H */
