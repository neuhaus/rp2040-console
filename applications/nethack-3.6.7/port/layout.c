/*
 * Data-layout probes.
 *
 * The data files (dungeon, quest.dat, *.lev) are produced on the 32-bit host
 * by makedefs/lev_comp/dgn_comp and consumed on the 32-bit ARM target.  A
 * struct whose padding differs between the two compilers would be read at the
 * wrong offsets, so build.py compiles this file for both and tools/check-
 * layout.py compares the resulting symbol addresses (a probe's address is the
 * size/offset it encodes).  Only structures that actually appear in a
 * generated file are probed.
 */
#include "hack.h"
#include "sp_lev.h"
#include "dgn_file.h"
#include "qtext.h"
#include <stddef.h>

/* version_info heads every generated binary file (.lev and dungeon). */
char layout_version_info[sizeof(struct version_info)];

/* dungeon file: n_dgns then arrays of these */
char layout_tmpdungeon[sizeof(struct tmpdungeon)];
char layout_tmplevel[sizeof(struct tmplevel)];
char layout_tmpbranch[sizeof(struct tmpbranch)];

/* quest.dat: header, then arrays of these */
char layout_qtmsg[sizeof(struct qtmsg)];

/* .lev files: a version_info then a serialized sp_lev program */
char layout_sp_lev[sizeof(sp_lev)];
char layout_opcode[sizeof(_opcode)];
char layout_opvar[sizeof(struct opvar)];

/* format structures carried inside the sp_lev program */
char layout_lev_init[sizeof(lev_init)];
char layout_room_door[sizeof(room_door)];
char layout_monster[sizeof(monster)];
char layout_object[sizeof(object)];
char layout_altar[sizeof(altar)];
char layout_region[sizeof(region)];
char layout_lev_region[sizeof(lev_region)];
char layout_corridor[sizeof(corridor)];
char layout_room[sizeof(room)];

char offset_version_info_feature_set[offsetof(struct version_info, feature_set) + 1];
char offset_version_info_entity_count[offsetof(struct version_info, entity_count) + 1];
char offset_version_info_struct_sizes1[offsetof(struct version_info, struct_sizes1) + 1];
char offset_version_info_struct_sizes2[offsetof(struct version_info, struct_sizes2) + 1];

char offset_qtmsg_msgnum[offsetof(struct qtmsg, msgnum) + 1];
char offset_qtmsg_delivery[offsetof(struct qtmsg, delivery) + 1];
char offset_qtmsg_offset[offsetof(struct qtmsg, offset) + 1];
char offset_qtmsg_size[offsetof(struct qtmsg, size) + 1];
char offset_qtmsg_summary_size[offsetof(struct qtmsg, summary_size) + 1];

char offset_tmpdungeon_name[offsetof(struct tmpdungeon, name) + 1];
char offset_tmpdungeon_lev[offsetof(struct tmpdungeon, lev) + 1];
char offset_tmpdungeon_flags[offsetof(struct tmpdungeon, flags) + 1];
char offset_tmpdungeon_chance[offsetof(struct tmpdungeon, chance) + 1];
char offset_tmpdungeon_levels[offsetof(struct tmpdungeon, levels) + 1];
char offset_tmpdungeon_branches[offsetof(struct tmpdungeon, branches) + 1];
char offset_tmpdungeon_entry_lev[offsetof(struct tmpdungeon, entry_lev) + 1];
char offset_tmpdungeon_boneschar[offsetof(struct tmpdungeon, boneschar) + 1];
char offset_tmplevel_name[offsetof(struct tmplevel, name) + 1];
char offset_tmplevel_lev[offsetof(struct tmplevel, lev) + 1];
char offset_tmplevel_chance[offsetof(struct tmplevel, chance) + 1];
char offset_tmplevel_rndlevs[offsetof(struct tmplevel, rndlevs) + 1];
char offset_tmplevel_chain[offsetof(struct tmplevel, chain) + 1];
char offset_tmplevel_flags[offsetof(struct tmplevel, flags) + 1];
char offset_tmplevel_boneschar[offsetof(struct tmplevel, boneschar) + 1];
char offset_tmpbranch_name[offsetof(struct tmpbranch, name) + 1];
char offset_tmpbranch_lev[offsetof(struct tmpbranch, lev) + 1];
char offset_tmpbranch_chain[offsetof(struct tmpbranch, chain) + 1];
char offset_tmpbranch_type[offsetof(struct tmpbranch, type) + 1];
char offset_tmpbranch_up[offsetof(struct tmpbranch, up) + 1];

char offset_sp_lev_opcodes[offsetof(sp_lev, opcodes) + 1];
char offset_sp_lev_n_opcodes[offsetof(sp_lev, n_opcodes) + 1];
char offset_opcode_opcode[offsetof(_opcode, opcode) + 1];
char offset_opcode_opdat[offsetof(_opcode, opdat) + 1];
char offset_opvar_spovartyp[offsetof(struct opvar, spovartyp) + 1];
char offset_opvar_vardata[offsetof(struct opvar, vardata) + 1];

char offset_lev_init_init_style[offsetof(lev_init, init_style) + 1];
char offset_lev_init_flags[offsetof(lev_init, flags) + 1];
char offset_lev_init_filling[offsetof(lev_init, filling) + 1];
char offset_lev_init_init_present[offsetof(lev_init, init_present) + 1];
char offset_lev_init_fg[offsetof(lev_init, fg) + 1];
char offset_lev_init_bg[offsetof(lev_init, bg) + 1];
char offset_lev_init_smoothed[offsetof(lev_init, smoothed) + 1];
char offset_lev_init_joined[offsetof(lev_init, joined) + 1];
char offset_lev_init_lit[offsetof(lev_init, lit) + 1];
char offset_lev_init_walled[offsetof(lev_init, walled) + 1];
char offset_lev_init_icedpools[offsetof(lev_init, icedpools) + 1];

char offset_monster_coord[offsetof(monster, coord) + 1];
char offset_monster_x[offsetof(monster, x) + 1];
char offset_monster_y[offsetof(monster, y) + 1];
char offset_monster_class[offsetof(monster, class) + 1];
char offset_monster_appear[offsetof(monster, appear) + 1];
char offset_monster_peaceful[offsetof(monster, peaceful) + 1];
char offset_monster_asleep[offsetof(monster, asleep) + 1];
char offset_monster_align[offsetof(monster, align) + 1];
char offset_monster_id[offsetof(monster, id) + 1];
char offset_monster_name[offsetof(monster, name) + 1];
char offset_monster_appear_as[offsetof(monster, appear_as) + 1];

char offset_object_coord[offsetof(object, coord) + 1];
char offset_object_x[offsetof(object, x) + 1];
char offset_object_y[offsetof(object, y) + 1];
char offset_object_class[offsetof(object, class) + 1];
char offset_object_containment[offsetof(object, containment) + 1];
char offset_object_curse_state[offsetof(object, curse_state) + 1];
char offset_object_id[offsetof(object, id) + 1];
char offset_object_spe[offsetof(object, spe) + 1];
char offset_object_corpsenm[offsetof(object, corpsenm) + 1];
char offset_object_name[offsetof(object, name) + 1];
char offset_object_quan[offsetof(object, quan) + 1];

char offset_lev_region_inarea[offsetof(lev_region, inarea) + 1];
char offset_lev_region_delarea[offsetof(lev_region, delarea) + 1];
char offset_lev_region_in_islev[offsetof(lev_region, in_islev) + 1];
char offset_lev_region_del_islev[offsetof(lev_region, del_islev) + 1];
char offset_lev_region_rtype[offsetof(lev_region, rtype) + 1];
char offset_lev_region_rname[offsetof(lev_region, rname) + 1];

char offset_room_name[offsetof(room, name) + 1];
char offset_room_parent[offsetof(room, parent) + 1];
char offset_room_x[offsetof(room, x) + 1];
char offset_room_y[offsetof(room, y) + 1];
char offset_room_w[offsetof(room, w) + 1];
char offset_room_h[offsetof(room, h) + 1];
char offset_room_rtype[offsetof(room, rtype) + 1];
char offset_room_chance[offsetof(room, chance) + 1];
char offset_room_rlit[offsetof(room, rlit) + 1];
char offset_room_filled[offsetof(room, filled) + 1];
char offset_room_joined[offsetof(room, joined) + 1];
