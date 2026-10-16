#include <assert.h>
#include <string.h>
#include <termcap.h>

static char output[128];
static unsigned length;
static int emit(int c) { output[length++] = (char)c; return c; }

int main(void)
{
    assert(tgetent(NULL, "xterm") == 1);
    assert(strchr(tgetstr("as", NULL), 15) == NULL);
    assert(strchr(tgetstr("ae", NULL), 15) == NULL);
    /* Exercise the capability sequence used around map glyphs and messages.
     * It must restore ASCII before ordinary text without emitting Ctrl-O. */
    assert(tputs(tgetstr("as", NULL), 1, emit) == 0);
    emit('q');
    assert(tputs(tgetstr("ae", NULL), 1, emit) == 0);
    tputs("You miss the sewer rat.", 1, emit);
    assert(strcmp(output, "\033(0q\033(BYou miss the sewer rat.") == 0);
    assert(strcmp(tgetstr("ks", NULL), "\033>") == 0);
    assert(strcmp(tgetstr("ke", NULL), "\033>") == 0);
    assert(strcmp(tgoto(tgetstr("cm", NULL), 17, 3), "\033[4;18H") == 0);
    assert(strcmp(tgoto(tgetstr("cm", NULL), 17, 2), "\033[3;18H") == 0);
    assert(tgetnum("Co") == 8);
    assert(strcmp(tgetstr("le", NULL), "\b") == 0);
    assert(strcmp(tgetstr("md", NULL), "\033[1m") == 0);
    assert(strcmp(tgetstr("me", NULL), "\033[m") == 0);
    assert(strcmp(tgetstr("mr", NULL), "\033[7m") == 0);
    for (int color = 0; color < 8; color++) {
        char expected[] = "\033[30m";
        expected[3] += color;
        assert(strcmp(tparm(tgetstr("AF", NULL), color), expected) == 0);
        expected[2] = '4';
        assert(strcmp(tparm(tgetstr("AB", NULL), color), expected) == 0);
    }
    assert(strcmp(tgoto(tgetstr("AF", NULL), 4, 0), "\033[34m") == 0);
    assert(tparm(NULL, 0) == NULL);
    assert(tparm(tgetstr("cm", NULL), 0) == NULL);
    assert(tparm(tgetstr("AF", NULL), -1) == NULL);
    assert(tparm(tgetstr("AF", NULL), 8) == NULL);
    return 0;
}
