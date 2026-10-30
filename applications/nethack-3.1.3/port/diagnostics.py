"""Opt-in maximum-branch wizard workload. Release source is unaffected."""
def prepare(src, port):
    def replace(name, old, new):
        p = src / name
        s = p.read_text()
        if s.count(old) != 1:
            raise RuntimeError('Diagnostic hook missing/ambiguous: ' + name + ': ' + repr(old))
        p.write_text(s.replace(old, new))
    replace('sys/unix/unixmain.c', '\tprocess_options(argc, argv);', '\twizard = TRUE;\n\tStrcpy(pl_character, "V");\n\tflags.legacy = FALSE;\n\tprocess_options(argc, argv);')
    replace('platform.c', 'srandom(seed ^ entropy);', 'srandom(0x5eed1234UL);')
    replace('src/dungeon.c', 'rn1(pd.tmpdungeon[i].lev.rand,\n\t\t\t\t\t      pd.tmpdungeon[i].lev.base)', '(pd.tmpdungeon[i].lev.base + pd.tmpdungeon[i].lev.rand - 1)')
    replace('src/cmd.c', 'const struct ext_func_tab extcmdlist[] = {', '''#include "../diagnostic-commands.c"
const struct ext_func_tab extcmdlist[] = {
    {"sramworld", "visit every branch", nh_world},
    {"sramrevisit", "map and revisit every branch", nh_revisit},
    {"srampack", "carry 150 objects", nh_pack},
    {"sramnextplane", "visit next plane", nh_nextplane},
    {"sramaltar", "stand on aligned Astral altar", nh_altar},
    {"sramstats", "count live state", nh_stats},
    {"sramverify", "verify every cached level", nh_verify},
    {"sramstairs", "stand on down/up stairs", nh_stairs},
''')
    replace('src/restore.c', '\trestoring = FALSE;\n\tclear_nhwindow(WIN_MESSAGE);', '\trestoring = FALSE;\n\tnh_mem_phase("restore_done");\n\tclear_nhwindow(WIN_MESSAGE);')
    replace('src/do.c', '\tregister int fd;\n\tregister boolean up', '\tregister int fd;\n\tnh_mem_phase("before_level");\n\tregister boolean up')
    p = src / 'compat.h'
    p.write_text(p.read_text() + '\nvoid nh_mem_phase(const char *);\n')
    p = src / 'CMakeLists.txt'
    p.write_text(p.read_text() + '''
target_sources(nethack PRIVATE diagnostic-memory.c)
target_compile_definitions(nethack PRIVATE RP2040_CONSOLE_CLOCK_NO_PROMPT=1)
target_link_options(nethack PRIVATE -Wl,--wrap=_malloc_r -Wl,--wrap=_calloc_r -Wl,--wrap=_realloc_r)
''')
