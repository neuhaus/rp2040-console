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
    return 0;
}
