#include <stdio.h>
int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    FILE *out = fopen(argv[1], "w");
    if (!out) return 1;
    fputs("#include \"termcap.h\"\n"
          "#include <time.h>\n"
          "#include <hardware/watchdog.h>\n"
          "_Static_assert(sizeof(time_t) == 4, \"legacy time ABI\");\n"
          "int main(int argc, char **argv) { long t; (void)argc; (void)argv; time(&t); localtime(&t); return panic(APPLICATION_HEADER) + (int)difftime(t, t); }\n", out);
    return fclose(out) != 0;
}
