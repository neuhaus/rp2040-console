int nh_sram_trial(void) {
    d_level target;
    int i;
    for (i = 0; i < MAXDUNGEON; i++)
        if (!strcmp(dungeons[i].dname, "Fort Ludios")) break;
    if (i == MAXDUNGEON) { impossible("No Fort Ludios dungeon"); return 0; }
    target.dnum = i; target.dlevel = 1;
    init_random(rn2);
    nh_mem_phase("trial_before_ludios");
    goto_level(&target, FALSE, FALSE, FALSE);
    nh_mem_phase("trial_after_ludios");
    return 0;
}

/* A wish for "Amulet of Yendor" may grant the imitation, which shares its
 * description; carry the real one, as the hero would after the Sanctum. */
int nh_sram_amulet(void) {
    struct obj *amulet = 0;
    if (!u.uhave.amulet)
        amulet = addinv(mksobj(AMULET_OF_YENDOR, TRUE, FALSE));
    printf("\nAMULET carried=%d letter=%c\n", u.uhave.amulet,
           amulet ? amulet->invlet : '?');
    return 0;
}

int nh_sram_pack(void) {
    struct obj *bag = addinv(mksobj(BAG_OF_HOLDING, TRUE, FALSE));
    int i;
    bag->cursed = 0;
    for (i = 0; i < 150; ++i)
        (void) add_to_container(bag, mksobj(RIN_PROTECTION, TRUE, FALSE));
    bag->owt = weight(bag);
    nh_mem_phase("pack_150_objects");
    return 0;
}

/* Visit the actual branch roster, rather than assuming a displayed depth
 * identifies every dungeon level. Endgame is a separate irreversible phase. */
static int nh_sram_world_run(boolean revisit) {
    extern int n_dgns;
    d_level target;
    int d, l, expected = 0, cached = 0;
    quest_status.got_quest = TRUE;
    u.ualign.record = 20;
    for (d = 0; d < n_dgns; ++d) {
        target.dnum = d; target.dlevel = 1;
        printf("\nWORLD_BRANCH %d %s levels=%d endgame=%d\n", d,
               dungeons[d].dname, dungeons[d].num_dunlevs, In_endgame(&target));
        if (!In_endgame(&target)) expected += dungeons[d].num_dunlevs;
    }
    nh_mem_phase("world_before");
    for (d = 0; d < n_dgns; ++d) {
        target.dnum = d; target.dlevel = 1;
        if (In_endgame(&target)) continue;
        for (l = 1; l <= dungeons[d].num_dunlevs; ++l) {
            target.dlevel = l;
            if (on_level(&target, &u.uz)
                || (!revisit && (level_info[ledger_no(&target)].flags & LFILE_EXISTS)))
                continue;
            printf("\nWORLD_VISIT dungeon=%d level=%d ledger=%d\n",
                   d, l, ledger_no(&target));
            goto_level(&target, FALSE, FALSE, FALSE);
            if (!on_level(&target, &u.uz)) panic("world tour transition refused");
            if (revisit && level.flags.hero_memory) {
                struct obj *obj;
                /* Explore terrain/traps and remember real floor objects so
                 * storage measurements include populated remembered maps. */
                wiz_map();
                for (obj = fobj; obj; obj = obj->nobj) map_object(obj, FALSE);
            }
        }
    }
    nh_sram_trial();
    for (l = 1; l <= maxledgerno(); ++l)
        if (level_info[l].flags & LFILE_EXISTS) ++cached;
    printf("\nWORLD_DONE expected=%d cached=%d current=%d\n", expected,
           cached, ledger_no(&u.uz));
    nh_mem_phase(revisit ? "revisit_done" : "world_done");
    return 0;
}

int nh_sram_world(void) { return nh_sram_world_run(FALSE); }
int nh_sram_revisit(void) { return nh_sram_world_run(TRUE); }

/* Step onto this level's magic portal and let dotrap() take it, so the
 * transition uses domagicportal() and deferred_goto() as in play. */
int nh_sram_portal(void) {
    struct trap *t;
    for (t = ftrap; t; t = t->ntrap)
        if (t->ttyp == MAGIC_PORTAL) break;
    if (!t) { pline("No magic portal on this level."); return 0; }
    printf("\nPORTAL_TAKE from=%d,%d x=%d y=%d\n", u.uz.dnum, u.uz.dlevel,
           t->tx, t->ty);
    u_on_newpos(t->tx, t->ty);
    dotrap(t, 0);
    nh_mem_phase("portal_taken");
    return 1;
}

int nh_sram_altar(void) {
    int x, y;
    if (!Is_astralevel(&u.uz)) { impossible("altar trial requires Astral"); return 0; }
    for (x = 1; x < COLNO; ++x) for (y = 0; y < ROWNO; ++y)
        if (IS_ALTAR(levl[x][y].typ)
            && Amask2align(levl[x][y].altarmask & AM_MASK) == u.ualign.type) {
            u_on_newpos(x, y);
            /* Keep the hero on the altar; arrival collision handling may
             * instead move the hero to a neighbouring square. */
            if (m_at(x, y)) mnexto(m_at(x, y));
            if (m_at(x, y)) { pline("The altar is occupied."); return 0; }
            vision_full_recalc = 1;
            printf("\nASCENSION_ALTAR x=%d y=%d alignment=%d\n",x,y,u.ualign.type);
            nh_mem_phase("ascension_altar");
            return 0;
        }
    pline("No co-aligned Astral altar found.");
    return 0;
}
