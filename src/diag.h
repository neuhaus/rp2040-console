#ifndef RP2040_CONSOLE_DIAG_H
#define RP2040_CONSOLE_DIAG_H

/* Boot progress on the onboard LED; see diag.c. */

void console_diag_init(void);
void console_diag_stage(int stage);

/* How far this boot got, for the board that prints nothing at all. */
#define DIAG_STAGE_STARTED          1   /* first code after the C runtime  */
#define DIAG_STAGE_CONSOLE          2   /* stdio up, USB device initialised */
#define DIAG_STAGE_ENV              3   /* environment built               */
#define DIAG_STAGE_CLOCK            4   /* date and time settled           */
#define DIAG_STAGE_STORAGE          5   /* volume mounted or given up on   */
#define DIAG_STAGE_READY            6   /* set up; handing over to the game */

#endif
