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
