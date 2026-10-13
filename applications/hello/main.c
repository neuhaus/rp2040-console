#include <stdio.h>

int main(int argc, char **argv)
{
    (void)argc;
    printf("Hello from %s on RP2040!\nPress Enter to exit.\n", argv[0]);
    while (getchar() != '\n') { }
    return 0;
}
