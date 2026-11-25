// Stilwell Farms -- the fifth heart (banked)
//
// Four heart scenes each end at four hearts (townlife.c). At five full hearts (friendship 250) there is one
// more, the first time you talk to them out of doors after that: what you've become to each other, and a
// parting gift worth having. Same little script letters as townlife.c: @ they speak, G Grandpa's voice,
// N narration, !x a bubble. Seen once: bit 7 of G.heart_seen[npc].
#pragma bank 255
#include <gb/gb.h>
#include "game.h"

BANKREF(heart5_try)

static const char *const names[NUM_NPC] = {"Alyssa", "Brick", "Andy", "Raza", "Joe", "Marcy"};
static const char *const ev[NUM_NPC][9] = {
    {"!h", "@There you are, dear. Come and look at the window.",
     "NA sunflower in a chipped blue pot, turned to the morning light.",
     "@I grew it from your seed. First one in that window since your grandpa's.",
     "@I've added a line to the ledger. Not an order this time.",
     "N'The farmer. Paid in full, every season. Family.'",
     "@Don't you dare make me cry in front of the customers.", "!h", 0},
    {"!.", "@Farmer. Wait.", "@Made something. Not for sale.",
     "NA small iron horseshoe, hammered smooth, your initials stamped on the inside.",
     "@Your grandpa had one off my father. Hung it over the barn door. Luck.",
     "@Never made one for anybody. Didn't think I would.",
     "@...Right. Good. Back to work. Both of us.", "!h", 0},
    {"!!", "@PAL! Close your eyes! Okay, don't, you'll walk into the bench.",
     "@I've been building something in secret. For WEEKS. It was very hard not to tell you.",
     "NA little wooden sign, carved all the way round with crops, cows and one lopsided dog.",
     "@It says STILWELL FARMS. For your gate. I spelled it right on the third try!",
     "@Everything I build is for somebody. This is the first one that was for a best friend.",
     "@Okay now I need a minute. Sawdust. In my eye.", "!h", 0},
    {"!n", "@Sit, hon. Sit! This one's on the house. They all are, for you.",
     "@I've a card for you. My grandmother's recipe. Nobody outside the family has it.",
     "NThe card is soft with age. 'For when someone becomes ours.'",
     "@Marcy asked if you could come to supper on Sundays. I said that was up to you.",
     "@It isn't, really. There's a chair with your name on it. Carved. Andy helped.",
     "@Welcome home, hon.", "!h", 0},
    {"!.", "@Walk with me a minute. Mayor's orders.",
     "@When your grandpa got sick he made me promise something.",
     "G'Joe, when the kid comes, don't help too much. Let the farm teach them. Then tell them I knew.'",
     "@So. He knew. Before you'd planted a single seed, he knew what you'd make of the place.",
     "@I've kept this town thirty years. It has never been as alive as it is now.",
     "@That's you. I wanted it said out loud. Here: the key to the town. It opens nothing.", "!h", 0},
    {"!!", "@WAIT WAIT WAIT. I have a IMPORTANT ANNOUNCEMENT.",
     "@I made a list of my best friends. It is: one, YOU. Two, the dog I will get. Three, Raza.",
     "@Don't tell Raza about three.",
     "NA crumpled drawing: a farm, a very tall you, a very small Marcy, a dog the size of a barn.",
     "@It's us when I'm BIG and I work on your farm. I'm in charge of the dog.",
     "@You have to keep it FOREVER. That's the rule.", "!h", 0}};

static void pause(uint8_t n) { while (n--) { update_sprites(); wait_vbl_done(); } }

// talking to someone in town with nothing else due: 1 if the fifth scene played
uint8_t heart5_try(uint8_t npc, uint8_t a) BANKED {
    const char *const *e = ev[npc];
    const char *s;
    static const uint8_t legend_crop[4] = {3, 7, 10, 14};
    uint8_t cr = legend_crop[G.season & 3];
    if (G.friend[npc] < 250 || (G.heart_seen[npc] & 0x8F) != 0x0F) return 0;   // all four seen, this one not
    if (G.heart_day == (G.day | (G.season << 5))) return 0;                    // one scene a day
    for (; (s = *e) != 0; e++) {
        if (s[0] == '!') {
            if (a != 0xFF) { emote(a, s[1] == 'h' ? EMO_HEART : s[1] == 'n' ? EMO_NOTE : s[1] == '.' ? EMO_DOTS : EMO_EXCLAIM, 0); pause(40); }
            continue;
        }
        if (s[0] == 'N') ui_speaker = 0;
        else set_speaker(s[0] == 'G' ? "Grandpa" : names[npc]);
        say(s + 1);
    }
    if (a != 0xFF) emote(a, EMO_NONE, 0);
    ui_speaker = 0;
    G.heart_seen[npc] |= 0x80;
    G.heart_day = G.day | (G.season << 5);
    G.seeds[cr][R_LEGEND] = G.seeds[cr][R_LEGEND] > 250 ? 255 : G.seeds[cr][R_LEGEND] + 5;
    G.gold += 5000;
    music_play(MUS_JINGLE_FANFARE);
    hud_draw();
    say("A friend for life. (And tucked in your pocket afterwards: 5 legendary seeds and G5000.)");
    music_for_scene();
    hud_flash("\xF1 A new memory \xF1");
    return 1;
}
