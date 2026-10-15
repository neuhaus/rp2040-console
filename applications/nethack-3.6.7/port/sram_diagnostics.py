"""Opt-in deterministic wizard probes; never enabled for ordinary play."""
from pathlib import Path
import shutil

def prepare(src, port):
    def replace(name, old, new):
        p = src / name
        s = p.read_text()
        if old not in s: raise RuntimeError("Missing diagnostic hook: " + name)
        p.write_text(s.replace(old, new, 1))
    replace('sys/unix/unixmain.c', 'authorize_wizard_mode()\n{',
            'authorize_wizard_mode()\n{\n    return TRUE; /* diagnostic image only */')
    replace('sys/unix/unixmain.c', 'sys_random_seed()\n{',
            'sys_random_seed()\n{\n    has_strong_rngseed = FALSE;\n    return 0x5eed1234UL;')
    replace('src/allmain.c', '    load_qtlist();',
            '    nh_mem_phase("before_quest");\n    load_qtlist();\n    nh_mem_phase("after_quest");')
    replace('src/sp_lev.c', '    result = sp_level_loader(fd, lvl, &operand_arena);',
            '    nh_mem_phase("before_special_load");\n    result = sp_level_loader(fd, lvl, &operand_arena);\n    nh_mem_phase("after_special_load");')
    replace('src/sp_lev.c', '    sp_level_free(lvl, operand_arena);',
            '    nh_mem_phase("after_special_build");\n    sp_level_free(lvl, operand_arena);\n    nh_mem_phase("after_special_free");')
    replace('src/restore.c', '    /* Success! */',
            '    nh_mem_phase("restore_done");\n    /* Success! */')
    replace('src/cmd.c', 'wiz_show_stats()\n{', 'wiz_show_stats()\n{\n    nh_mem_phase("stats");')
    replace('src/cmd.c', '    { \'\\0\', "stats",',
            '    { \'\\0\', "sramtrial", "fixed-seed Ludios trial", nh_sram_trial, WIZMODECMD | AUTOCOMPLETE },\n'
            '    { \'\\0\', "sramtour", "visit 48 depths", nh_sram_tour, WIZMODECMD | AUTOCOMPLETE },\n'
            '    { \'\\0\', "srampack", "carry 150 test objects", nh_sram_pack, WIZMODECMD | AUTOCOMPLETE },\n'
            '    { \'\\0\', "sramstack", "travel stack probes", nh_sram_stack, WIZMODECMD | AUTOCOMPLETE },\n'
            '    { \'\\0\', "stats",')
    p = src / 'src/cmd.c'; p.write_text(p.read_text() + (port / 'sram-commands.c').read_text())
    p = src / 'compat.h'; p.write_text(p.read_text() + '\nvoid nh_mem_phase(const char *);\nint nh_sram_trial(void);\nint nh_sram_stack(void);\nint nh_sram_pack(void);\nint nh_sram_tour(void);\n')
    p = src / 'CMakeLists.txt'; s = p.read_text().replace('set(_game_env TERM=xterm HOME=/)', 'set(_game_env TERM=xterm HOME=/ USER=wizard LOGNAME=wizard)')
    p.write_text(s + '\ntarget_compile_definitions(nethack PRIVATE RP2040_CONSOLE_CLOCK_NO_PROMPT=1)\ntarget_compile_options(nethack PRIVATE -fstack-usage)\n')
    shutil.copy(port / 'sram-diagnostics.c', src / 'memlog.c')
