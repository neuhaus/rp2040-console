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

int nh_sram_stack(void) {
    int x, y, probes = 0;
    wiz_map();
    nh_mem_phase("stack_before_travel");
    for (x = 1; x < COLNO && probes < 16; ++x)
        for (y = 0; y < ROWNO && probes < 16; ++y)
            if (ACCESSIBLE(levl[x][y].typ)
                && distmin(u.ux, u.uy, x, y) > 2) {
                (void)is_valid_travelpt(x,y);
                ++probes;
            }
    printf("\nSTACK_PROBES %d\n",probes);
    nh_mem_phase("stack_after_travel");
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

int nh_sram_tour(void) {
    d_level target;
    int i, hell;
    for (hell = 0; hell < MAXDUNGEON; ++hell)
        if (!strcmp(dungeons[hell].dname, "Gehennom")) break;
    if (hell == MAXDUNGEON) { impossible("No Gehennom"); return 0; }
    for (i = 1; i <= 48; ++i) {
        target.dnum = i < dungeons[hell].depth_start ? 0 : hell;
        target.dlevel = i - dungeons[target.dnum].depth_start + 1;
        printf("\nTOUR_DEPTH %d\n", i);
        goto_level(&target, FALSE, FALSE, FALSE);
    }
    nh_sram_trial();
    nh_mem_phase("tour_done");
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

int nh_sram_nextplane(void) {
    d_level target;
    if (Is_astralevel(&u.uz)) return 0;
    if (!u.uhave.amulet) (void)addinv(mksobj(AMULET_OF_YENDOR, TRUE, FALSE));
    if (!u.uhave.amulet) panic("plane trial missing Amulet");
    target.dnum = earth_level.dnum;
    target.dlevel = In_endgame(&u.uz) ? u.uz.dlevel - 1 : earth_level.dlevel;
    printf("\nPLANES_VISIT level=%d ledger=%d\n", target.dlevel, ledger_no(&target));
    goto_level(&target, FALSE, FALSE, FALSE);
    if (!on_level(&target, &u.uz)) panic("plane tour transition refused");
    nh_mem_phase("plane_done");
    return 0;
}

int nh_sram_planes(void) {
    int n = 0;
    do {
        nh_sram_nextplane();
        if (++n > 5) panic("plane tour did not reach Astral");
    } while (!Is_astralevel(&u.uz));
    nh_mem_phase("planes_done");
    return 0;
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
