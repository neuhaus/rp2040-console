/*
 * Bare-metal termcap shim for the Pico port.
 *
 * A termcap-driven game drives the screen through five entry points:
 *
 *     tgetent()  parse an /etc/termcap entry
 *     tgetstr()  fetch a capability string
 *     tgetnum()  fetch a numeric capability
 *     tgetflag() test a boolean capability
 *     tputs()    emit a capability with %-padding
 *     tgoto()    emit a cursor-motion capability for (col,row)
 *
 * A hosted build gets these from a termcap library reading /etc/termcap.
 * There is nothing of the kind here, and no way to learn what terminal is
 * attached to the USB link, so instead of parsing an entry we answer with
 * a built-in VT100/xterm description.
 *
 * That is a deliberate restriction: this build assumes a VT100/xterm
 * compatible host on the other end of the USB CDC link, which covers
 * every terminal emulator anyone will actually attach to a Pico. It is
 * not a general termcap implementation and does not attempt to be.
 */

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <termcap.h>

/* Capabilities the game asks for, as they appear in a termcap entry.
 * tgetstr() hands back a pointer into this table; the game frees the
 * scratch buffer it passed in but never frees these. */
static char s_bc[] = "\b";
static char s_cl[] = "\033[H\033[2J";
static char s_ce[] = "\033[K";
static char s_cd[] = "\033[J";
static char s_ho[] = "\033[H";
static char s_cm[] = "\033[%i%d;%dH";
static char s_nd[] = "\033[C";
static char s_up[] = "\033[A";
static char s_so[] = "\033[7m";
static char s_se[] = "\033[m";

/*
 * Terminal metrics: what the game is told it has to draw into, and what
 * tgetnum("co") and tgetnum("li") answer. The manifest can set them; 80x24
 * is the assumption everywhere else in the engine, and the honest thing to
 * report over a serial link, where the real size of the emulator window is
 * not something the board can ask about.
 */
#ifndef RP2040_CONSOLE_COLS
#define RP2040_CONSOLE_COLS 80
#endif

#ifndef RP2040_CONSOLE_ROWS
#define RP2040_CONSOLE_ROWS 24
#endif

#define SHIM_COLS RP2040_CONSOLE_COLS
#define SHIM_ROWS RP2040_CONSOLE_ROWS

char PC = 0;
char *UP = s_up;
char *BC = s_bc;
/*
 * termcap's ospeed is not a baud rate: it is the small BSD code that indexes
 * the padding tables a termcap entry is scaled by. B9600 is 13 in that
 * numbering, which is what a program expects to read back, and a value like
 * 115200 would sail straight past the end of any such table.
 */
short ospeed = 13;

int tgetent(char *bp, const char *name)
{
    (void)bp; /* no entry is parsed; see header comment */

    /*
     * Accept only terminals we actually describe. Silently claiming to
     * be VT100 for anything else would produce a confusing mess of
     * escape sequences rather than a clear failure.
     */
    if (name == NULL)
        return 0;

    if (strcmp(name, "xterm") == 0 || strcmp(name, "vt100") == 0 ||
        strcmp(name, "ansi") == 0 || strcmp(name, "linux") == 0 ||
        strcmp(name, "screen") == 0 || strcmp(name, "dumb") == 0 ||
        strcmp(name, "xterm-256color") == 0 || strcmp(name, "vt220") == 0)
        return 1;

    return 0;
}

int tgetflag(const char *id)
{
    /*
     * The game probes "NP"/"nx" (no parity) and "os" (turn-on
     * strobe). Report no-parity, which is right for USB CDC, and no
     * strobing, which lets the game continue.
     */
    if (strcmp(id, "NP") == 0 || strcmp(id, "nx") == 0)
        return 1;
    return 0;
}

int tgetnum(const char *id)
{
    if (strcmp(id, "co") == 0)
        return SHIM_COLS;
    if (strcmp(id, "li") == 0)
        return SHIM_ROWS;
    if (strcmp(id, "sg") == 0)
        return 0; /* no standout glitch: so/se are safe to use */
    return -1;
}

char *tgetstr(const char *id, char **area)
{
    (void)area; /* static storage; nothing to allocate */

    if (strcmp(id, "bc") == 0)
        return s_bc;
    if (strcmp(id, "cl") == 0)
        return s_cl;
    if (strcmp(id, "ce") == 0)
        return s_ce;
    if (strcmp(id, "cd") == 0)
        return s_cd;
    if (strcmp(id, "ho") == 0)
        return s_ho;
    if (strcmp(id, "cm") == 0)
        return s_cm;
    if (strcmp(id, "nd") == 0)
        return s_nd;
    if (strcmp(id, "up") == 0)
        return s_up;
    if (strcmp(id, "so") == 0)
        return s_so;
    if (strcmp(id, "se") == 0)
        return s_se;

    /*
     * Absent on purpose: "ti"/"ve"/"vs"/"te"/"xd". The game treats
     * missing ti/te as fine, and xd is documented as unsupported in
     * the 1985 source itself.
     */
    return NULL;
}

/*
 * Emit a termcap string. The real tputs() understands padding such as
 * "$<50>"; we have none, so this is a plain copy to the caller's
 * putc.
 */
int tputs(const char *str, int affcnt, int (*putc_fn)(int))
{
    const char *p;

    (void)affcnt;

    if (str == NULL)
        return -1;

    for (p = str; *p != '\0'; p++)
        (void)putc_fn((unsigned char)*p);

    return 0;
}

/*
 * Expand a cursor-motion capability. The only form the game uses is
 * "%i%d;%dH" (the %i flags 1-based coordinates), so handle exactly that
 * and leave any other capability untouched.
 */
char *tgoto(const char *cap, int col, int row)
{
    static char out[32];

    if (cap == NULL)
        return NULL;

    if (strstr(cap, "%i%d;%dH") != NULL)
    {
        /*
         * termcap's cm takes (column, row) - %p1 is the column, %p2 the
         * row - but ANSI's CUP is ESC [ row ; col H. The two orders are
         * opposite, so the arguments must be swapped on the way out.
         *
         * Getting this backwards does not look like an obvious fault: the
         * cursor still moves, just to a transposed position, so the map
         * draws in the wrong place rather than not drawing at all.
         */
        snprintf(out, sizeof(out), "\033[%d;%dH", row + 1, col + 1);
        return out;
    }

    if (strstr(cap, "%d") != NULL)
    {
        /* Single-parameter capability, e.g. "%dC". */
        const char *pct = strchr(cap, '%');
        if (pct != NULL && pct[1] == 'd')
        {
            char tail = pct[2];
            snprintf(out, sizeof(out), "%.*s%d%c", (int)(pct - cap), cap, col, tail);
            return out;
        }
    }

    /* Nothing to expand; hand it back unchanged. */
    snprintf(out, sizeof(out), "%s", cap);
    return out;
}