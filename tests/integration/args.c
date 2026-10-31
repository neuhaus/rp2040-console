/*
 * The manifest's ARGS arrive between argv[0] and the NULL that ends the list.
 * Nothing runs this on a board; building it is the check, because the generated
 * argument table has to be spliced into the engine's argv in the right place.
 */
#include <assert.h>
#include <string.h>

int rp2040_console_game_main(int argc, char **argv)
{
    assert(argc == 3);
    assert(strcmp(argv[0], "args_app") == 0);
    assert(strcmp(argv[1], "-x") == 0);
    assert(strcmp(argv[2], "story.z3") == 0);
    return 0;
}
