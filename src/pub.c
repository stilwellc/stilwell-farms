// Stilwell Farms -- the Pub: Corey behind the bar, west of the park, open from 4pm till the day ends (banked)
// Supper costs a little more than Raza's for the energy; what the Pub has is the hours. Meals count toward
// the same one-bar-a-day cap as the cafe's and your own cooking (G.ate).
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(pub_act)

#define PUB_OPENS 960   // 4pm (G.time is minutes)

static const char *const pub_talk[4] = {
    "Why the Scarecrow? Stand in a field all day, you'd want a root beer too.",
    "Your grandpa had the corner stool. Nobody sits there. Well... you could.",
    "Brick comes in, says four words all night. Tips well, though.",
    "Heard the fish are biting after dark. Heard it from a fish, mind you."};

void pub_act(void) BANKED {
    static const char *const fm[5] = {"Root beer 160", "Pub pie   340", "Stew      520", "Talk", "Bye"};
    static const uint16_t pub_cost[3] = {160, 340, 520};
    static const uint8_t pub_energy[3] = {20, 45, 75};
    uint8_t r;
    if (G.time < PUB_OPENS) {
        ui_speaker = 0;
        say("CLOSED.\nThe Thirsty Scarecrow\nOpens at 4pm. Scarecrows sleep in. -Corey");
        return;
    }
    set_speaker("Corey");
    say("Welcome to the Thirsty Scarecrow! Pull up a stool. Long day in the fields?");
    while (1) {
        ui_begin(); ui_money();
        r = ui_menu(0, 3, 16, fm, 5, 0);   // 13-column labels + cursor + borders
        ui_end(); ui_begin(); ui_money();  // (the name tab would cover the last row during the answer)
        if (r < 3) {
            if (G.gold < pub_cost[r]) ui_dialog("No rush. The stool's free, anyway.");
            else if (G.ate >= G.energy_max) ui_dialog("You've put away plenty today. Go get some sleep!");
            else {
                uint16_t e = (uint16_t)G.energy + pub_energy[r];
                G.gold -= pub_cost[r];
                if (e > G.energy_max) e = G.energy_max;
                G.ate = (uint16_t)G.ate + (e - G.energy) > 255 ? 255 : G.ate + (uint8_t)(e - G.energy);
                G.energy = (uint8_t)e;
                sfx(SFX_COIN);
                hud_draw();
                ui_dialog(r == 0 ? "Cold and fizzy. That hits the spot!" : r == 1 ? "Hot pie, flaky crust. You feel better already." :
                          "A deep bowl of stew. You could work another shift!");
            }
        } else if (r == 3) {
            ui_dialog(pub_talk[rnd() & 3]);
        } else { ui_dialog("Mind the step on your way out!"); ui_end(); ui_speaker = 0; return; }
        ui_end();
    }
}
