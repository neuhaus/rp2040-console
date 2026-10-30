/* Included only in explicitly requested diagnostic builds. */
#include "tamp-codec.h"
extern int n_dgns;
extern boolean level_exists[];
static unsigned nh_objs(struct obj *o) {
    unsigned n = 0;
    for (; o; o = o->nobj) n += 1 + nh_objs(o->cobj);
    return n;
}
static int nh_stats(void) {
    struct monst *m;
    unsigned mons = 0, objs = nh_objs(invent) + nh_objs(fobj) + nh_objs(level.buriedobjlist) + nh_objs(migrating_objs);
    int l, cached = 0;
    for (m = fmon; m; m = m->nmon) { ++mons; objs += nh_objs(m->minvent); }
    for (m = migrating_mons; m; m = m->nmon) { ++mons; objs += nh_objs(m->minvent); }
    for (l = 1; l <= maxledgerno(); ++l) if (level_exists[l]) ++cached;
    printf("\nSTATE dungeon=%d level=%d ledger=%d objects=%u monsters=%u inventory=%u cached=%d\n", u.uz.dnum, u.uz.dlevel, ledger_no(&u.uz), objs, mons, nh_objs(invent), cached);
    nh_mem_phase("stats");
    return 0;
}
static int nh_pack(void) {
    struct obj *bag = addinv(mksobj(BAG_OF_HOLDING, TRUE, FALSE)), *o;
    int i;
    bag->cursed = 0;
    for (i = 0; i < 150; ++i) {
        o = mksobj(RIN_PROTECTION, TRUE, FALSE);
        o->nobj = bag->cobj;
        bag->cobj = o;
    }
    bag->owt = weight(bag);
    nh_stats();
    nh_mem_phase("pack_done");
    return 0;
}
static int nh_world_run(boolean revisit) {
    d_level target;
    int d, l, expected = 0;
    quest_status.got_quest = TRUE;
    u.ualign.record = 20;
    for (d = 0; d < n_dgns; ++d) {
        target.dnum = d; target.dlevel = 1;
        printf("\nWORLD_BRANCH %d %s levels=%d endgame=%d\n", d, dungeons[d].dname, dungeons[d].num_dunlevs, In_endgame(&target));
        if (In_endgame(&target)) continue;
        expected += dungeons[d].num_dunlevs;
        for (l = 1; l <= dungeons[d].num_dunlevs; ++l) {
            target.dlevel = l;
            if (!on_level(&target, &u.uz)) {
                printf("\nWORLD_VISIT dungeon=%d level=%d ledger=%d\n", d, l, ledger_no(&target));
                goto_level(&target, FALSE, FALSE, FALSE);
                if (!on_level(&target, &u.uz)) panic("world transition refused");
            }
            if (revisit) {
                struct obj *o;
                struct trap *t;
                int x, y;
                for (x = 1; x < COLNO; ++x) for (y = 0; y < ROWNO; ++y) {
                    levl[x][y].seen = 1;
                    map_background(x, y, FALSE);
                }
                for (t = ftrap; t; t = t->ntrap) {
                    t->tseen = 1;
                    map_trap(t, FALSE);
                }
                for (o = fobj; o; o = o->nobj) map_object(o, FALSE);
            }
        }
    }
    printf("\nWORLD_DONE expected=%d\n", expected);
    nh_stats();
    nh_mem_phase(revisit ? "revisit_done" : "world_done");
    return 0;
}
static int nh_world(void) { return nh_world_run(FALSE); }
static int nh_revisit(void) { return nh_world_run(TRUE); }
static int nh_nextplane(void) {
    d_level target;
    if (Is_astralevel(&u.uz)) return 0;
    if (!u.uhave.amulet) (void) addinv(mksobj(AMULET_OF_YENDOR, TRUE, FALSE));
    if (!u.uhave.amulet) panic("missing Amulet");
    target.dnum = earth_level.dnum;
    target.dlevel = In_endgame(&u.uz) ? u.uz.dlevel - 1 : earth_level.dlevel;
    printf("\nPLANE_VISIT level=%d ledger=%d\n", target.dlevel, ledger_no(&target));
    goto_level(&target, FALSE, FALSE, FALSE);
    if (!on_level(&target, &u.uz)) panic("plane transition refused");
    nh_stats();
    nh_mem_phase("plane_done");
    return 0;
}
static int nh_altar(void) {
    int x, y;
    if (!Is_astralevel(&u.uz)) panic("not Astral");
    for (x = 1; x < COLNO; ++x) for (y = 0; y < ROWNO; ++y)
        if (IS_ALTAR(levl[x][y].typ) && Amask2align(levl[x][y].altarmask & AM_MASK) == u.ualign.type) {
            if (m_at(x,y)) mnexto(m_at(x,y));
            if (m_at(x,y)) panic("occupied altar");
            teleds(x,y);
            printf("\nASCENSION_ALTAR x=%d y=%d alignment=%d\n", x,y,u.ualign.type);
            nh_mem_phase("altar_done");
            return 0;
        }
    panic("no aligned altar");
    return 0;
}
static int nh_stairs(void) {
    int x = xdnstair ? xdnstair : xupstair;
    int y = xdnstair ? ydnstair : yupstair;
    if (m_at(x,y)) mnexto(m_at(x,y));
    teleds(x,y);
    printf("\nSTAIRS_READY x=%d y=%d down=%d\n", x,y,!!xdnstair);
    return 0;
}

static int nh_verify(void) {
    int l, fd, owner, count = 0;
    xchar ledger;
    unsigned char byte;
    for (l = 1; l <= maxledgerno(); ++l) if (level_exists[l]) {
        fd = open_levelfile(l);
        if (fd < 0) panic("missing cached level %d", l);
        minit();
        if (mread(fd, &owner, sizeof owner) || mread(fd, &ledger, sizeof ledger)
            || owner != hackpid || ledger != l) panic("invalid cached level owner/ledger %d", l);
        while (nh_codec_read(fd, &byte, 1) == 0) { }
        close(fd);
        ++count;
    }
    printf("\nVERIFIED_LEVELS %d\n", count);
    nh_mem_phase("verify_done");
    return 0;
}
