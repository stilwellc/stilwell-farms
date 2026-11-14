// Stilwell Farms -- farmhands: hiring at town hall, nightly payroll and work, the STAFF page (banked)
//
// Night order (world_overnight -> staff_overnight, after growth/weather/animals, before shipping pays):
//   1. wages are recalculated from the farm's size (land, soil, crops, animals)
//   2. payroll comes out of the gold you hold. Shipping money only arrives at dawn, so tonight's sales
//      (including what the staff ship tonight) can't cover tonight's wages: keep a cash cushion.
//      A worker you can't pay works one night unpaid; if you still can't pay (with back pay) the next
//      night, they quit and leave a polite note in the morning report.
//   3. everyone still employed does the night's work: harvest + ship, collect + feed animals, weeding,
//      then the morning watering (after weather, so it is the last word on wet soil).
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
#include "decor.h"

BANKREF(staff_overnight)

// Set to 1 once town businesses exist: the shopkeeper is defined (role, wage, flag) but not offered yet.
#define STAFF_SHOP_OPEN 0

#define NUM_NAMES 12
static const char *const names[NUM_NAMES] = {"Wren", "Tobi", "Mae", "Gus", "Ivy", "Otis",
                                             "Pearl", "Hank", "Nell", "Rudy", "Lou", "Dot"};
static const char *const role_name[NUM_ROLES] = {"Waterer", "Harvester", "Rancher", "Grounds", "Shopkeep"};
static const char *const role_short[NUM_ROLES] = {"Water", "Harvest", "Ranch", "Grounds", "Shop"};
static const char *const role_desc[NUM_ROLES][2] = {
    {"Waters all tilled", "soil each morning."},
    {"Picks ripe crops", "and ships them."},
    {"Feeds animals hay", "and ships produce."},
    {"Pulls weeds and", "clears dead crops."},
    {"Will run your town", "businesses."}};
static const uint8_t wage_base[NUM_ROLES] = {30, 40, 40, 25, 60};
static const uint8_t wage_plot[NUM_ROLES] = {8, 8, 8, 12, 15};

// last night's results (for the report and the farmhands' lines)
static uint16_t m_wages, m_watered, m_harvested, m_collected, m_fed, m_cleared;
static uint32_t m_shipped;
static uint8_t m_unpaid, m_quit, m_nohay;
static uint8_t m_quit_name[NUM_ROLES];

static const char *worker_name(uint8_t role) { return names[(G.staff[role] - 1) % NUM_NAMES]; }
static uint8_t candidate(uint8_t role) {  // today's applicant for a role (distinct across roles)
    return (uint8_t)((role * 5 + G.day + G.season * 7 + G.year) % NUM_NAMES);
}

uint8_t staff_hired(uint8_t role) BANKED { return role < NUM_ROLES && G.staff[role]; }

// ---------------------------------------------------------------- wages
static uint16_t wage_now(uint8_t role) {
    uint16_t i, units = 0, w;
    uint8_t k, plots = 0, crop;
    {   // owned plots: popcount by bytes (a variable 16-bit shift per plot was a loop)
        uint8_t b = (uint8_t)G.plots, h = (uint8_t)(G.plots >> 8);
        for (k = 8; k; k--) { plots += (b & 1) + (h & 1); b >>= 1; h >>= 1; }
    }
    w = wage_base[role] + (uint16_t)wage_plot[role] * plots;
    if (role == ROLE_WATER || role == ROLE_HARVEST) {
        const uint8_t *pc = G.cell, *p1 = G.a1;
        for (i = 0; i < 1024; i++, pc++, p1++) {
            if (*pc != C_SOIL) continue;
            crop = *p1 & SOIL_CROP;
            if (role == ROLE_WATER) units++;
            else if (crop && crop != CROP_WITHERED) units++;
        }
    }
    if (role == ROLE_WATER) w += units >> 1;              // +1 per 2 tilled tiles
    else if (role == ROLE_HARVEST) w += units << 1;       // +2 per growing crop
    else if (role == ROLE_RANCH) {
        for (k = 0; k < MAX_ANIMALS; k++) if (G.anim[k].kind != 0xFF) w += 12;   // +12 per animal
    }
    return w;
}

// ---------------------------------------------------------------- the night's work
static uint8_t roll_quality(uint8_t fert) {
    uint8_t r = rnd();
    if (r < (fert ? 30 : 6)) return 2;
    if (r < (fert ? 100 : 30)) return 1;
    return 0;
}

static void ship(uint8_t it, uint8_t q) {
    uint16_t v = item_value(it, q);
    G.ship_value += v;
    m_shipped += v;
    mark_shipped(it);
}

static void work(void) {
    uint16_t i;
    uint8_t k, c, crop, *pc = G.cell, *p1 = G.a1, *p2 = G.a2;
    uint8_t harv = G.staff[ROLE_HARVEST], grounds = G.staff[ROLE_GROUNDS], water = G.staff[ROLE_WATER];
    // rancher: collect what's waiting (quality from happiness), then feed today's hay
    if (G.staff[ROLE_RANCH])
        for (k = 0; k < MAX_ANIMALS; k++) {
            Animal *a = &G.anim[k];
            if (a->kind == 0xFF) continue;
            if (a->flags & AF_READY) {
                uint8_t it = a->kind == A_COW ? I_MILK : a->kind == A_HEN ? I_EGG : I_WOOL;
                uint8_t q = BREED(a) == R_LEGEND ? 2 : a->happy > 200 ? 2 : a->happy > 120 ? 1 : 0;
                ship(it, q);
                if (BREED(a)) ship(it, q);   // rare and golden breeds give two
                a->flags &= ~AF_READY;
                m_collected++;
            }
            if (!(a->flags & AF_FED)) {
                if (G.hay) { G.hay--; a->flags |= AF_FED; m_fed++; }
                else m_nohay = 1;
            }
        }
    if (!(harv | grounds | water)) return;
    // one pass over the farm for the field hands (each cell's jobs in the old order: harvest, clear, water)
    for (i = 0; i < 1024; i++, pc++, p1++, p2++) {
        c = *pc;
        if (c == C_SOIL) {
            crop = *p1 & SOIL_CROP;
            // harvester: every ripe crop (regrowers stay planted, keeping their seed rarity), quality rolled like a hand harvest
            if (harv && crop && crop != CROP_WITHERED && (*p2 & SOIL_DAYS) >= crops[crop].days) {
                ship(crop - 1, roll_quality(*p1 & SOIL_FERT));
                if (*p2 & SOIL_RAR) ship(crop - 1, roll_quality(*p1 & SOIL_FERT));            // rare/legendary: 2+
                if (((*p2 & SOIL_RAR) >> 6) == R_LEGEND) ship(crop - 1, roll_quality(1));     // legendary: 3
                if (crops[crop].regrow) *p2 = (*p2 & SOIL_RAR) | (crops[crop].days - crops[crop].regrow);
                else { *p1 &= SOIL_WET | SOIL_FERT; *p2 = 0; }
                G.stat_harvest++;
                m_harvested++;
            }
            // groundskeeper: withered crops
            if (grounds && (*p1 & SOIL_CROP) == CROP_WITHERED) {
                *p1 &= SOIL_WET | SOIL_FERT; *p2 = 0;
                m_cleared++;
            }
            // waterer: the morning round, every tilled tile
            if (water && !(*p1 & SOIL_WET)) { *p1 |= SOIL_WET; m_watered++; }
        } else if (c == C_WEEDS && grounds && *p1 && owned((uint8_t)i & 31, (uint8_t)(i >> 5))) {
            // groundskeeper: weeds that grew back on your land (clearing the original overgrowth is yours)
            *pc = C_GRASS; *p1 = 0; *p2 = 0;
            G.stat_weeds++;
            m_cleared++;
        }
    }
}

void staff_overnight(void) BANKED {
    uint8_t r, bit;
    m_wages = m_watered = m_harvested = m_collected = m_fed = m_cleared = 0;
    m_shipped = 0;
    m_unpaid = m_quit = m_nohay = 0;
    for (r = 0; r < NUM_ROLES; r++) {
        uint32_t owed;
        if (!G.staff[r]) continue;
        bit = 1 << r;
        G.staff_wage[r] = wage_now(r);
        owed = G.staff_wage[r];
        if (G.staff_unpaid & bit) owed <<= 1;   // back pay for the unpaid night
        if (G.gold >= owed) {
            G.gold -= owed;
            m_wages += (uint16_t)owed;
            G.staff_unpaid &= ~bit;
        } else if (G.staff_unpaid & bit) {
            m_quit |= bit;
            m_quit_name[r] = G.staff[r];
            G.staff[r] = 0;
            G.staff_unpaid &= ~bit;
        } else {
            G.staff_unpaid |= bit;
            m_unpaid |= bit;
        }
    }
    work();
}

// ---------------------------------------------------------------- morning report
static void add_count(uint16_t n, const char *what) {
    if (!n) return;
    if (s_len() > 7) s_add(", ");
    s_num(n, 0); s_add(what);
}

void staff_report(void) BANKED {
    uint8_t r;
    if (m_watered || m_harvested || m_collected || m_fed || m_cleared) {
        s_clr(); s_add("Staff: ");
        add_count(m_harvested, " harvested");
        add_count(m_collected, " collected");
        add_count(m_fed, " fed");
        add_count(m_cleared, " cleared");
        add_count(m_watered, " watered");
        s_ch('.');
        say(sbuf);
    }
    if (m_nohay) say("Your rancher ran out of hay! Cut grass into a silo or buy hay in town.");
    if (m_wages || m_shipped) {
        s_clr();
        if (m_wages) { s_add("Paid G"); s_num(m_wages, 0); s_add(" in wages. "); }
        if (m_shipped) { s_add("Staff shipped G"); s_num(m_shipped, 0); s_add(" of goods."); }
        say(sbuf);
    }
    if (m_unpaid) {
        uint8_t n = 0;
        s_clr(); s_add("Short on gold! ");
        for (r = 0; r < NUM_ROLES; r++)
            if (m_unpaid & (1 << r)) { if (n++) s_add(", "); s_add(worker_name(r)); }
        s_add(" worked unpaid. Pay up tomorrow or they'll quit.");
        say(sbuf);
    }
    for (r = 0; r < NUM_ROLES; r++) {
        uint8_t bit = 1 << r;
        if (m_quit & bit) {
            const char *n = names[(m_quit_name[r] - 1) % NUM_NAMES];
            s_clr(); s_add("A note from "); s_add(n);
            s_add(": Thanks for having me, boss, but I can't work without pay. Good luck!");
            say(sbuf);
        }
    }
}

// ---------------------------------------------------------------- talking to a farmhand
void staff_talk(uint8_t role) BANKED {
    uint16_t i, dry = 0;
    if (!G.staff[role]) return;
    set_speaker(worker_name(role));   // RAM copy: say() runs with other banks mapped
    s_clr();
    if (G.staff_unpaid & (1 << role)) s_add("Um, boss... I wasn't paid last night. One more and I have to go.");
    else switch (role) {
    case ROLE_WATER:
        for (i = 0; i < 1024; i++) if (G.cell[i] == C_SOIL && !(G.a1[i] & SOIL_WET)) dry++;
        if (dry) { s_add("New soil? I'll water those "); s_num(dry, 0); s_add(" tiles first thing tomorrow!"); }
        else s_add("All watered, boss!");
        break;
    case ROLE_HARVEST:
        if (m_harvested) { s_add("Shipped "); s_num(m_harvested, 0); s_add(" crops last night, boss!"); }
        else s_add("Nothing ripe yet. I'll check the fields again tonight!");
        break;
    case ROLE_RANCH:
        if (!G.hay) s_add("We're out of hay, boss! Fill the silo or buy some in town.");
        else { s_add("Everyone's fed. "); s_num(G.hay, 0); s_add(" hay left in the silo."); }
        break;
    case ROLE_GROUNDS:
        if (m_cleared) { s_add("Pulled "); s_num(m_cleared, 0); s_add(" weeds last night. Looking tidy, boss!"); }
        else s_add("Not a weed in sight, boss!");
        break;
    default: s_add("Minding the stand, boss!"); break;
    }
    say(sbuf);
    ui_speaker = 0;
}

// ---------------------------------------------------------------- where they stand during the day
static uint16_t first_cell(uint8_t crop_only) {  // first soil (or crop) tile
    uint16_t i;
    for (i = 0; i < 1024; i++)
        if (G.cell[i] == C_SOIL && (!crop_only || (G.a1[i] & SOIL_CROP)))
            return (i & 31) | ((i >> 5) << 8);
    return 0xFFFF;
}

uint16_t staff_spot(uint8_t role) BANKED {
    uint8_t k, h = bld_find(B_HOUSE);
    uint16_t s = 0xFFFF;
    switch (role) {
    case ROLE_WATER: s = first_cell(0); break;
    case ROLE_HARVEST: s = first_cell(1); if (s == 0xFFFF) s = first_cell(0); break;
    case ROLE_RANCH:
        for (k = 0; k < MAX_BLD; k++)
            if ((G.bld[k].type == B_BARN || G.bld[k].type == B_COOP) && !(G.bld[k].flags & BF_RUIN)) {
                s = (G.bld[k].x + 1) | ((uint16_t)(G.bld[k].y + bld_h(G.bld[k].type) + 1) << 8);
                break;
            }
        break;
    case ROLE_SHOP:
        k = bld_find(B_STAND);
        if (k == 0xFF) return 0xFFFF;   // off in town
        return G.bld[k].x | ((uint16_t)(G.bld[k].y + 2) << 8);
    }
    if (s == 0xFFFF && h != 0xFF) {  // near the house; groundskeeper works the yard
        s = (G.bld[h].x + (role == ROLE_GROUNDS ? 5 : 1 + role)) | ((uint16_t)(G.bld[h].y + 5) << 8);
    }
    if (s != 0xFFFF && role == ROLE_HARVEST) s += 0x0100;   // stand a step apart from the waterer
    return s;
}

uint8_t staff_pal(uint8_t role) BANKED {
    switch (role) {
    case ROLE_WATER: return SPR_PL_DOWN_0_PAL;            // blue
    case ROLE_HARVEST: return SPR_FARMHAND_DOWN_PAL + 1;  // STAFF2: wheat / mustard
    case ROLE_GROUNDS: return SPR_MERCHANT_PAL;           // green
    default: return SPR_FARMHAND_DOWN_PAL;                // STAFF: red
    }
}

// ---------------------------------------------------------------- hiring (town hall)
static void wait_button(void) {
    while (1) {
        read_input();
        if (joy_press & (J_A | J_B | J_START)) { sfx(SFX_CURSOR); return; }
        wait_vbl_done();
    }
}

static void role_panel(uint8_t role, uint8_t hired) {
    ui_panel(0, 9, 20, 8);
    s_clr();
    if (hired) { s_add(worker_name(role)); s_add(" works here"); }
    else { s_add(names[candidate(role)]); s_add(" asks G"); s_num(wage_now(role), 0); s_add("/day"); }
    ui_text(1, 10, sbuf);
    ui_text(1, 12, role_desc[role][0]);
    ui_text(1, 14, role_desc[role][1]);
}

#define NUM_OFFERED (STAFF_SHOP_OPEN ? NUM_ROLES : NUM_ROLES - 1)

static void hire_menu(void) {
    uint8_t r, k, hv = 0;
    while (1) {
        for (k = 0; k < NUM_OFFERED; k++) {
            s_clr(); s_add(role_name[k]); s_pad(10);
            s_add(G.staff[k] ? "HIRED" : names[candidate(k)]);
            lbl_set(k);
        }
        ui_begin();
        ui_hover = &hv;
        while (1) {
            role_panel(hv, G.staff[hv] != 0);
            r = ui_menu(0, 0, 18, lp, NUM_OFFERED, hv);
            if (r != 0xFE) break;
        }
        ui_hover = 0;
        ui_end();
        if (r == 0xFF) return;
        hv = r;
        if (G.staff[r]) {
            s_clr(); s_add(worker_name(r)); s_add(" already works for you. Manage staff from the STAFF page.");
            say(sbuf);
            continue;
        }
        s_clr(); s_add("Hire "); s_add(names[candidate(r)]); s_add(" as "); s_add(role_name[r]);
        s_add(" for G"); s_num(wage_now(r), 0); s_add(" a day?");
        if (!ui_yesno(sbuf)) continue;
        G.staff[r] = candidate(r) + 1;
        G.staff_wage[r] = wage_now(r);
        G.staff_unpaid &= ~(1 << r);
        sfx(SFX_COIN);
        s_clr(); s_add(worker_name(r)); s_add(" starts tonight! Wages grow with the farm and are paid nightly, before shipping.");
        say(sbuf);
    }
}

static const char *const hall_items[4] = {"TALK", "HIRE HELP", "STAFF", "BYE"};

uint8_t staff_hall(void) BANKED {
    uint8_t r;
    set_speaker("Hollis");
    say("Need a hand on the farm? Folks in town want work.");
    ui_speaker = 0;
    while (1) {
        ui_begin();
        r = ui_menu(0, 0, 12, hall_items, 4, 0);
        ui_end();
        if (r == 0) return 1;
        if (r == 1) hire_menu();
        else if (r == 2) { ui_begin(); staff_page(); ui_end(); }
        else return 0;
    }
}

// ---------------------------------------------------------------- STAFF page
void staff_page(void) BANKED {
    uint8_t k, n, r, map[NUM_ROLES];
    uint16_t total;
    while (1) {
        n = 0; total = 0;
        for (k = 0; k < NUM_ROLES; k++) {
            uint16_t w;
            if (!G.staff[k]) continue;
            w = wage_now(k);
            total += w;
            s_clr(); s_add(worker_name(k)); s_pad(6); s_add(role_short[k]); s_pad(13); s_num(w, 4);
            lbl_set(n); map[n++] = k;
        }
        ui_box(0, 0, 20, 17);
        s_clr(); s_add("STAFF");
        ui_title(2, 0, sbuf);
        if (!n) {
            ui_text(2, 2, "Nobody yet.");
            ui_text(2, 4, "Mayor Hollis at");
            ui_text(2, 5, "town hall can find");
            ui_text(2, 6, "you farmhands.");
            wait_button();
            return;
        }
        for (k = 0; k < n; k++) ui_text(2, 2 + k * 2, lbl[k]);
        ui_panel(0, 11, 20, 6);
        s_clr(); s_add("Wages/day  G"); s_num(total, 0); ui_text(1, 12, sbuf);
        ui_text(1, 14, "A: let someone go");
        r = 0;
        while (1) {
            for (k = 0; k < n; k++) ui_char(1, 2 + k * 2, k == r ? GL_CURSOR : GL_FILL);
            read_input();
            if (joy_press & (J_B | J_START)) { sfx(SFX_CANCEL); return; }
            if (joy_press & J_A) { sfx(SFX_CONFIRM); break; }
            if (joy_press & J_UP) { r = r ? r - 1 : n - 1; sfx(SFX_CURSOR); }
            if (joy_press & J_DOWN) { r = r + 1 < n ? r + 1 : 0; sfx(SFX_CURSOR); }
            wait_vbl_done();
        }
        k = map[r];
        s_clr(); s_add("Let "); s_add(worker_name(k)); s_add(" go? They'll leave on good terms.");
        if (ui_yesno(sbuf)) {
            s_clr(); s_add(worker_name(k)); s_add(": Thanks for everything, boss. Take care!");
            G.staff[k] = 0;
            G.staff_unpaid &= ~(1 << k);
            say(sbuf);
        }
    }
}
