// Stilwell Farms -- town life: daily routines, opening hours, greetings, birthdays, heart events (banked)
#pragma bank 255
#include <gb/gb.h>
#include "game.h"
#include "routes.h"

BANKREF(tl_spot)

static const char *const npc_names[NUM_NPC] = {"Rosa", "Brick", "June", "Ada", "Hollis", "Milo"};

// ---------------------------------------------------------------- daily routines
// Each townsperson's day is a list of (time, spot) stops; they walk the flow field to each stop in turn.
// One list per kind of day: weekday, Saturday, Sunday, rainy weekday/Saturday, rainy Sunday.
// Shopkeepers' lists are the shops' opening hours: a counter is open exactly while its owner is behind it.
#define T(h, m) ((uint8_t)((h) * 6 + (m) / 10))   // time in ten-minute steps
#define HM 0x3F                                    // home: in at their front door
#define END {0xFF, 0}
#define HOME_BIT 0x80
typedef struct { uint8_t t, s; } Stop;

static const Stop rosa_wk[] = {{T(6, 0), SP_GARDEN}, {T(8, 0), SP_STORE}, {T(19, 0), SP_PARK_W}, {T(21, 0), HM}, END};
static const Stop rosa_sa[] = {{T(6, 0), SP_GARDEN}, {T(8, 0), SP_STORE}, {T(19, 0), SP_CAFE_T1}, {T(21, 30), HM}, END};
static const Stop rosa_su[] = {{T(6, 0), SP_MARKET}, {T(8, 0), SP_STORE}, {T(19, 0), SP_FOUNT_W}, {T(21, 0), HM}, END};
static const Stop rosa_rn[] = {{T(8, 0), SP_STORE}, {T(19, 0), SP_CAFE_T1}, {T(21, 0), HM}, END};

static const Stop brick_wk[] = {{T(6, 0), SP_ORCHARD}, {T(9, 0), SP_SMITH}, {T(18, 0), SP_CAFE_T3}, {T(19, 30), SP_FOUNT_E}, {T(22, 0), HM}, END};
static const Stop brick_su[] = {{T(7, 0), SP_PARK_E}, {T(12, 0), SP_CAFE_T3}, {T(14, 0), SP_ORCHARD}, {T(18, 0), SP_FOUNT_E}, {T(22, 0), HM}, END};
static const Stop brick_rn[] = {{T(9, 0), SP_SMITH}, {T(18, 0), SP_CAFE_T3}, {T(21, 0), HM}, END};
static const Stop brick_rs[] = {{T(10, 0), SP_CAFE_T3}, {T(15, 0), HM}, END};

static const Stop june_wk[] = {{T(6, 0), SP_ROAD}, {T(7, 0), SP_CAFE_T2}, {T(9, 0), SP_CARP}, {T(18, 0), SP_ORCHARD}, {T(20, 0), SP_PARK_S}, {T(21, 30), HM}, END};
static const Stop june_su[] = {{T(6, 0), SP_ROAD}, {T(8, 0), SP_PARK_S}, {T(12, 0), SP_CAFE_T2}, {T(13, 0), SP_BOARD}, {T(15, 0), SP_PARK_C}, {T(19, 0), SP_PLAZA_5}, {T(21, 0), HM}, END};
static const Stop june_rn[] = {{T(7, 0), SP_CAFE_T2}, {T(9, 0), SP_CARP}, {T(18, 0), SP_CAFE_T2}, {T(21, 0), HM}, END};
static const Stop june_rs[] = {{T(11, 0), SP_CAFE_T2}, {T(14, 0), HM}, END};

static const Stop ada_wk[] = {{T(6, 0), SP_MARKET}, {T(7, 0), SP_CAFE}, {T(21, 0), HM}, END};
static const Stop ada_su[] = {{T(6, 0), SP_GARDEN}, {T(7, 0), SP_CAFE}, {T(21, 0), HM}, END};
static const Stop ada_rn[] = {{T(7, 0), SP_CAFE}, {T(21, 0), HM}, END};

static const Stop hollis_wk[] = {{T(6, 0), SP_BOARD}, {T(7, 30), SP_CAFE_T1}, {T(9, 0), SP_HALL}, {T(12, 0), SP_CAFE_T1}, {T(13, 0), SP_HALL}, {T(17, 0), SP_FOUNT_W}, {T(19, 0), SP_PARK_C}, {T(21, 0), HM}, END};
static const Stop hollis_sa[] = {{T(6, 0), SP_PARK_C}, {T(7, 30), SP_CAFE_T1}, {T(9, 0), SP_HALL}, {T(12, 0), SP_CAFE_T1}, {T(13, 0), SP_HALL}, {T(17, 0), SP_FOUNT_W}, {T(21, 0), HM}, END};
static const Stop hollis_su[] = {{T(7, 30), SP_CAFE_T1}, {T(9, 0), SP_HALL}, {T(12, 0), SP_CAFE_T1}, {T(13, 0), SP_HALL}, {T(17, 0), SP_PARK_C}, {T(20, 0), HM}, END};
static const Stop hollis_rn[] = {{T(7, 30), SP_CAFE_T1}, {T(9, 0), SP_HALL}, {T(12, 0), SP_CAFE_T1}, {T(13, 0), SP_HALL}, {T(17, 0), SP_CAFE_T4}, {T(20, 0), HM}, END};

static const Stop milo_wk[] = {{T(6, 0), SP_FOUNT_E}, {T(12, 0), SP_CAFE_T2}, {T(13, 0), SP_PARK_S}, {T(17, 0), SP_FOUNT_E}, {T(19, 0), HM}, END};
static const Stop milo_sa[] = {{T(6, 0), SP_FOUNT_E}, {T(9, 0), SP_ROAD}, {T(12, 0), SP_CAFE_T2}, {T(13, 0), SP_PARK_E}, {T(17, 0), SP_FOUNT_E}, {T(19, 30), HM}, END};
static const Stop milo_su[] = {{T(8, 0), SP_GARDEN}, {T(12, 0), SP_CAFE_T4}, {T(13, 0), SP_PARK_W}, {T(17, 0), SP_PLAZA_5}, {T(19, 0), HM}, END};
static const Stop milo_rn[] = {{T(6, 0), SP_FOUNT_E}, {T(12, 0), SP_CAFE_T4}, {T(13, 0), HM}, END};

enum { V_WEEK, V_SAT, V_SUN, V_RAIN, V_RAINSUN };
static const Stop *const sched[NUM_NPC][5] = {
    {rosa_wk, rosa_sa, rosa_su, rosa_rn, rosa_rn},
    {brick_wk, brick_wk, brick_su, brick_rn, brick_rs},
    {june_wk, june_wk, june_su, june_rn, june_rs},
    {ada_wk, ada_wk, ada_su, ada_rn, ada_rn},
    {hollis_wk, hollis_sa, hollis_su, hollis_rn, hollis_rn},
    {milo_wk, milo_sa, milo_su, milo_rn, milo_rn}};
static const uint8_t home_spot[NUM_NPC] = {SP_STORE, SP_SMITH, SP_CARP, SP_HOME_A, SP_HOME_B, SP_HOME_A};
static const uint8_t fest_spot[NUM_NPC] = {SP_PLAZA_1, SP_PLAZA_2, SP_PLAZA_3, SP_PLAZA_4, SP_HALL, SP_PLAZA_5};

static uint8_t now_t(void) { return (uint8_t)(G.time / 10); }
static uint8_t weekday(void) { return (uint8_t)((G.day - 1) % 7); }   // 0 Monday .. 6 Sunday (day 1 is a Monday)
static uint8_t wet(void) { return G.weather == 1 || G.weather == 3; }

// Milo spends weekend days (8am-6pm) at the beach, by the pier (areas.c), unless it's a festival
uint8_t milo_beach(void) BANKED {
    uint8_t t = now_t();
    return weekday() >= 5 && t >= T(8, 0) && t < T(18, 0) && !FESTIVAL_DAY();
}

uint8_t tl_spot(uint8_t npc) BANKED {
    const Stop *p;
    uint8_t t = now_t(), s = HM, wd = weekday(), v;
    if (FESTIVAL_DAY() && t >= T(9, 0) && t < T(18, 0)) return fest_spot[npc];   // the whole town turns out
    if (npc == NPC_MILO && milo_beach()) return home_spot[npc] | HOME_BIT;      // off to the beach: not in town
    v = wet() ? (wd == 6 ? V_RAINSUN : V_RAIN) : wd == 6 ? V_SUN : wd == 5 ? V_SAT : V_WEEK;
    for (p = sched[npc][v]; p->t != 0xFF && p->t <= t; p++) s = p->s;
    if (s == HM) return home_spot[npc] | HOME_BIT;
    if (G.season == SEASON_WINTER) {   // too cold for the park and the garden: the plaza instead
        if (s == SP_PARK_W) s = SP_PLAZA_1;
        else if (s == SP_PARK_E) s = SP_PLAZA_2;
        else if (s == SP_PARK_S) s = SP_PLAZA_3;
        else if (s == SP_PARK_C) s = SP_PLAZA_4;
        else if (s == SP_GARDEN) s = SP_MARKET;
        else if (s == SP_ORCHARD) s = SP_PLAZA_5;
    }
    return G.mk_flags ? invest_spot(npc, s) : s;   // invest.c: evenings on Market Street once it is open
}

// ---------------------------------------------------------------- opening hours
static uint8_t owner_of(uint8_t act, uint8_t *spot) {
    switch (act) {
    case ACT_STORE: *spot = SP_STORE; return NPC_ROSA;
    case ACT_SMITH: *spot = SP_SMITH; return NPC_BRICK;
    case ACT_CARPENTER: *spot = SP_CARP; return NPC_JUNE;
    case ACT_CAFE: *spot = SP_CAFE; return NPC_ADA;
    case ACT_HALL: *spot = SP_HALL; return NPC_HOLLIS;
    default: return 0xFF;
    }
}

uint8_t tl_shop_open(uint8_t act) BANKED {
    uint8_t spot, n = owner_of(act, &spot);
    return n == 0xFF || tl_spot(n) == spot;
}

void tl_closed_sign(uint8_t act) BANKED {
    uint8_t spot, n = owner_of(act, &spot), t = now_t();
    ui_speaker = 0;
    if (FESTIVAL_DAY() && t >= T(9, 0) && t < T(18, 0)) {
        say("CLOSED for the festival! Come find us around the fountain.");
        return;
    }
    if (n == NPC_HOLLIS && t >= T(12, 0) && t < T(13, 0)) { say("Out to lunch at Ada's. Back at 1pm! -Mayor Hollis"); return; }
    switch (act) {
    case ACT_STORE: say("CLOSED.\nGeneral Store\nOpen every day, 8am to 7pm. -Rosa"); break;
    case ACT_SMITH: say("CLOSED.\nBrick's Forge\n9am to 6pm. Closed Sundays."); break;
    case ACT_CARPENTER: say("CLOSED.\nJune's Carpentry\n9am to 6pm, Monday to Saturday."); break;
    case ACT_CAFE: say("CLOSED.\nAda's Cafe\nOpen every day, 7am to 9pm. Come back hungry!"); break;
    default: say("Town Hall\nOpen every day, 9am to 5pm. Lunch 12 to 1."); break;
    }
    (void)spot;
}

// ---------------------------------------------------------------- greetings + birthdays
static const char *const greets[NUM_NPC][4] = {   // morning, afternoon, evening, festival (name + ": " + this <= 18)
    {"Morning, dear!", "Hello, dear!", "Evening, dear!", "Happy festival"},
    {"Hmph. Morning.", "...Hey.", "Evening.", "Hmph. Fun."},
    {"Morning! Hi!", "Hi hi hi!", "Evening!", "Festival day!"},
    {"Morning, hon!", "Hiya, hon!", "Evening, hon!", "Have a pie!"},
    {"Good day!", "Hello!", "Evening!", "Welcome!"},
    {"Hi! HI!", "Hey! Hey!", "Hi again!", "FESTIVAL!!"}};
static const uint8_t bday_season[NUM_NPC] = {SEASON_SPRING, SEASON_FALL, SEASON_SUMMER, SEASON_WINTER, SEASON_FALL, SEASON_SUMMER};
static const uint8_t bday_day[NUM_NPC] = {9, 21, 3, 12, 5, 22};

uint8_t tl_birthday(uint8_t npc) BANKED { return G.season == bday_season[npc] && G.day == bday_day[npc]; }

void tl_greet(uint8_t npc) BANKED {
    uint8_t t = now_t();
    s_clr(); s_add(npc_names[npc]); s_add(": ");
    if (tl_birthday(npc)) s_add("My birthday!");
    else s_add(greets[npc][FESTIVAL_DAY() ? 3 : t < T(12, 0) ? 0 : t < T(18, 0) ? 1 : 2]);
    hud_flash(sbuf);
}

// A on a townhouse door: Ada and Milo live on the west lane, the mayor on the east
void tl_house(void) BANKED {
    uint8_t west = G.px < 16, in;
    in = west ? (tl_spot(NPC_ADA) & HOME_BIT) || (tl_spot(NPC_MILO) & HOME_BIT) : (tl_spot(NPC_HOLLIS) & HOME_BIT);
    ui_speaker = 0;
    if (now_t() >= T(22, 0)) say("The lights are out. Better not wake anyone.");
    else if (!in) say(west ? "Nobody's home. Ada and Milo must be out and about." : "Nobody's home. The mayor must be out and about.");
    else if (west) {
        if (tl_spot(NPC_ADA) & HOME_BIT) { set_speaker("Ada"); say("(through the door) Cafe's closed, hon! Breakfast is at seven."); }
        else { set_speaker("Milo"); say("(through the door) Mom says I can't come out! It's RAINING. I LOVE rain!"); }
    } else { set_speaker("Hollis"); say("(through the door) The mayor is off duty! Come see me at the hall."); }
    ui_speaker = 0;
}

// ---------------------------------------------------------------- friends page
static const char *const season_short[4] = {"Sp", "Su", "Fa", "Wi"};   // "Bday Fa 21" ends left of the pips

void friends_rows(void) BANKED {
    uint8_t k, j, h, y;
    for (k = 0; k < NUM_NPC; k++) {
        y = 2 + k * 2;
        ui_text(2, y, npc_names[k]);
        h = G.friend[k] / 50;
        for (j = 0; j < 5; j++) ui_char(14 + j, y, j < h ? GL_HEART : GL_HEART_EMPTY);
        // under hearts 1-4: the heart event at that heart (gold star seen, silver star ready, dot locked)
        for (j = 0; j < 4; j++)
            ui_char(14 + j, y + 1, (G.heart_seen[k] & (1 << j)) ? GL_IT_STAR2 : j < h ? GL_IT_STAR : font_map['.' - 32]);
        s_clr();
        if (tl_birthday(k)) s_add("Bday today!");
        else { s_add("Bday "); s_add(season_short[bday_season[k]]); s_ch(' '); s_num(bday_day[k], 0); }
        ui_text(3, y + 1, sbuf);
    }
    ui_text(2, 15, "\xF1 seen  \xF0 ready");
}

// ---------------------------------------------------------------- heart events
// One short scene per townsperson at 1, 2, 3 and 4 hearts. Scripts are lists of lines; the first character
// says what a line is:
//   @text  they say it          Gtext  Grandpa (a memory)        Ntext  narration
//   !k     emote over them (! h=heart n=note .=dots)             fk    they face: u d l r, a = away from you
//   ?Nq    they ask q; the next two lines are the choices, the two after are their replies; choice N is the kind one
//   +k     a gift: S legendary seeds, g gold ore, w wood, E a full belly, j jam, $ gold, m a minnow
static const char *const ev_rosa[4][16] = {
    {"!!", "@Oh! There you are, dear. I was sorting the old ledgers and look what fell out.",
     "@One of your grandpa's order cards. 'Forty radish seeds. And one sunflower, for Rosa's window.'",
     "@Every spring, one sunflower. He never said why, and I never asked.", "!.",
     "?1What do you say?", "It was his way", "What a chore",
     "@...I suppose he did. I'll grow one this year. Maybe you'll bring me a sunflower someday, hm?",
     "@Ha! You sound just like him on a windy day. Well, I'll think about it.", "!h", 0},
    {"!n", "@Evening, dear. I come out here to read the ledger.", "@Every name in it planted something in this valley.",
     "@Look, I started a new page. 'Six radish seeds. The new farmer.' That's you.",
     "?1What do you say?", "Show me his page", "Nice bookkeeping",
     "@Here. His handwriting was terrible. Look at this one...",
     "@Somebody has to! Here, his first page...",
     "GRosa, write it down: one sack of seed potatoes and a promise. I'll pay you in the spring.",
     "@He paid in summer. With interest. In pie.", "!h", 0},
    {"!.", "@Rain day, slow day. Can I tell you something, dear?",
     "@Years ago the store nearly closed. One long winter, nobody was buying.",
     "@Your grandpa came in every week and bought seeds. Bags and bags. He said he was 'stocking up'.",
     "@Nobody stocks up on that many turnips. He kept my lights on, that stubborn old man.", "fa",
     "?1What do you say?", "I'll be loyal too", "Turnips are tasty",
     "@...Oh, you. Come here. You're just like him, you know that?",
     "@Ha! Not THAT many turnips, they're not.", "!h", 0},
    {"!h", "@I've been keeping something for you, dear. Your grandpa left it with me.",
     "@He said, 'When the kid's ready.' I've watched you work that land. I think you're ready.",
     "+S", "NRosa hands you a jar of heirloom seeds, labelled in your grandpa's careful, crooked writing.",
     "@Legendary seeds. The best he ever grew. Now go plant something wonderful...",
     "@...and come tell me about it. I'll put a sunflower in the window.", "!h", 0}};

static const char *const ev_brick[4][16] = {
    {"!.", "@...You. Hold out your hands.", "NBrick turns your hands over and studies them with a squint.",
     "@Hmph. Calluses. Blisters. Dirt under the nails.",
     "?1What do you do?", "Hold them up", "Hide them",
     "@Good. Farmer's hands now. Your grandpa had hands like old saddle leather.",
     "@Don't. Hands like that mean you're doing it right.", "@...Keep it up.", 0},
    {"!!", "@Gah! Don't sneak up on a man.", "NBrick hides something behind his back. It's a little wooden bird.",
     "@It's for June. Her birthday's coming. She talks about birds. A lot.", "fa", "@...Don't tell anyone.",
     "?1What do you say?", "Secret's safe", "June'll LOVE it!",
     "@Hmph. Good. ...You think the wings are alright? Never mind. Forget I asked.",
     "@Shh! Keep your voice down! ...You really think so?", "!h", 0},
    {"!.", "@That hoe you started with. The old one of your grandpa's. I made it.",
     "@I was sixteen. Worst weld I ever did. The blade's crooked as a creek.",
     "@He used it forty years. Wouldn't let me fix it.",
     "GCrooked or not, it turns the soil, son. So do you. Everybody starts somewhere.",
     "?1What do you say?", "Teach me to forge", "It's a fine hoe",
     "@You? Ha! ...Maybe. Come by on a slow day. Wear boots.",
     "@It's a terrible hoe. ...But thanks.", "!h", 0},
    {"!h", "@She hung the bird in her window. June. She smiled for a whole day.",
     "@Thanks for keeping your mouth shut. Not many folks can.", "@Here. Made you something.",
     "+g", "NBrick drops five lumps of gold ore into your hands. They're still a little warm.",
     "@Enough for a gold tool. Bring it by and I'll make it sing.", "@...Friend.", 0}};

static const char *const ev_june[4][16] = {
    {"!!", "@Morning! I measure the whole town on my run. Your road is exactly 412 paces!",
     "@Oh! And I sketched your farmhouse from the road. See?", "@I gave it a weathervane. It NEEDS a weathervane.",
     "?1What do you say?", "Can I get a copy?", "412? Really?",
     "@Really? Yes! I'll ink it properly. Nobody ever asks for a copy!",
     "@413 if you step around the puddle. I'm VERY thorough.", "!n", 0},
    {"!n", "@Sit with me? I'm drawing the bandstand that used to stand in the park.",
     "@A storm knocked it down before you came. Your grandpa built the first one, you know.",
     "@Brick's dad did the hinges. The whole town used to dance there on summer nights.",
     "?1What do you say?", "Let's rebuild it", "Storms are scary",
     "@You mean it? I've had the plans for years! I just needed someone to say it out loud.",
     "@They are. But a bandstand's just wood and nails. Wood and nails can go back up.", "!h", 0},
    {"!.", "@The town fund doesn't cover lumber. Hollis says maybe next year. It's always next year.", "fa",
     "?1What do you say?", "I'll help you", "Next year's close",
     "@...You would? Okay. Okay! Then I'm not giving up. Not this time.",
     "@Easy to say when you're not the one holding the plans.", "!h", 0},
    {"!h", "@Guess what! Brick gave every hinge. Rosa sold seed packets for the fund.", "@Ada baked for the whole crew!",
     "@It goes up this week. And the plaque says: 'Built by neighbours. Begun by old Stilwell...'",
     "@'...and finished by the new one.' That's you, by the way!",
     "+w", "NJune presses a bundle of leftover lumber into your arms.",
     "@For your farm. Build something that makes people dance.", "!h", 0}};

static const char *const ev_ada[4][16] = {
    {"!n", "@Here, hon. On the house. You look like you skipped breakfast again.",
     "@Cherry pie. Your grandpa ordered it every Friday...", "@...and every Friday he said it wasn't sweet enough.",
     "?2How is it?", "It's perfect", "Could be sweeter",
     "@Liar. A sweet liar. Eat up.",
     "@HA! That's exactly what he said! Same face and everything. Oh, I needed that laugh.", "!h", 0},
    {"!.", "@I used to cook in the city, hon. Forty tables a night. Fancy plates.", "@I stopped tasting any of it.",
     "@Then I came here. Milo grew up on tomatoes off the vine, and I remembered what food is for.",
     "?1Ask her?", "What is it for?", "Sounds stressful",
     "@For people, hon. For sitting down together. Your vegetables taste like that.",
     "@It was. This is better. Busy, but better.", "!h", 0},
    {"!.", "@Milo wants to be a farmer like you. He planted a bean in a teacup on my counter.",
     "@It's... not doing great. And he checks it every hour. I can't bear to tell him.",
     "?1What do you say?", "I'll help him", "Beans are tough",
     "@Would you? Oh, hon. A little sun and less water, I bet. He'll listen to you.",
     "@I know. I just hate watching him worry.", "!h", 0},
    {"!h", "@New on the menu: the Stilwell Plate. Everything on it came from your fields.",
     "@People come in from the next valley for it now. Sit. First one's yours, forever.",
     "+E", "NAda brings out a steaming plate. You eat every bite and feel brand new.",
     "+j", "@And take some jam for the road. Don't argue with me, hon.", 0}};

static const char *const ev_hollis[4][16] = {
    {"!!", "@Ah, come, come! I found something in the town archive. Look here.",
     "@Your grandpa and me at the very first Flower Show. Nineteen years old. He won, of course.",
     "@I've been mayor fifty years, and I still haven't beaten him.",
     "?1What do you say?", "Did you ever win?", "You haven't aged!",
     "@Once. He was sick in bed. He sent me a card that said 'It doesn't count.' It counts.",
     "@Ha! Flatterer. You'd make a fine politician.", "!n", 0},
    {"!n", "@We dug this fountain by hand, he and I. The summer the well ran dry.",
     "@I said it was too fancy for a little town. He said a town needs somewhere to make wishes.",
     "GToss a coin, Hollis. Worst case, the fountain gets richer.",
     "?1What do you do?", "Toss a coin too", "Just listen",
     "NTwo coins plink into the water. Hollis closes his eyes and smiles.",
     "@...Thank you for letting an old man ramble.", "!h", 0},
    {"!.", "@I'm getting old, you know. Some days I wonder who'll keep the calendar when I'm gone.", "fa",
     "?1What do you say?", "The town will", "You'll outlive us",
     "@...Yes. Yes, I think they will. You've made this place feel young again.",
     "@Ha! From your lips to the weather's ears.", "!h", 0},
    {"!h", "@Your grandpa and I buried a tin under the town tree. 'Open when the farm is back on its feet.'",
     "@I'd say it is. I dug it up this morning. There's a letter. It's for you.",
     "GIf you're reading this, the old place is green again. I knew it would be.",
     "GBe good to the town. It was good to me.", "GAnd let Hollis win the Flower Show once. Love, Grandpa.",
     "+$", "NTucked in the letter is a bundle of old banknotes. G2000!", "@...He never could keep a secret.", 0}};

static const char *const ev_milo[4][16] = {
    {"!!", "@Hey! HEY! Is that dirt on your boots from your farm? Real farm dirt?",
     "?1What do you do?", "Let him touch it", "It's just dirt",
     "@Whoa. It's so... dirty. Best day ever!", "@It's NOT just dirt. It's FARM dirt. Geez.", "!n", 0},
    {"!n", "@I made a kite! It's got a dog on it 'cause Mom says no real dog till I'm ten.",
     "@Will you hold the string while I run? Please please please?",
     "?1What do you do?", "Hold the string", "Maybe later",
     "NYou hold the string. Milo runs. The dog kite climbs into the sky, and he whoops.",
     "@...Okay. Later. You promise?", "!h", 0},
    {"!!", "@My bean SPROUTED! In the teacup! Mom says you helped.",
     "@I'm gonna name it Grandpa. 'Cause your grandpa was the best farmer. And so is my bean.",
     "?1What do you say?", "He'd love that", "Weird bean name",
     "@Yeah! Grandpa the bean. He's gonna be so tall.", "@It's a GREAT name. You're weird.", "!h", 0},
    {"!h", "@I drew your farm! That's you. That's the barn. That's me visiting. That's your dog.",
     "@When I'm bigger can I help on your farm? I'll work for cookies. Two cookies.",
     "+m", "NMilo gives you a small fish wrapped in his drawing.",
     "@That's my best fish ever. It's a minnow. It's still the best.", "!h", 0}};

static const char *const *const events[NUM_NPC] = {
    (const char *const *)ev_rosa, (const char *const *)ev_brick, (const char *const *)ev_june,
    (const char *const *)ev_ada, (const char *const *)ev_hollis, (const char *const *)ev_milo};

// when each scene can happen: a window (ten-minute steps) and the weather (0 any, 1 dry, 2 rain)
typedef struct { uint8_t t0, t1, wx; } When;
static const When when[NUM_NPC][4] = {
    {{T(8, 0), T(19, 0), 0}, {T(18, 0), T(21, 0), 1}, {T(8, 0), T(21, 0), 2}, {T(8, 0), T(12, 0), 0}},
    {{T(9, 0), T(18, 0), 0}, {T(18, 0), T(22, 0), 0}, {T(6, 0), T(22, 0), 0}, {T(9, 0), T(18, 0), 0}},
    {{T(6, 0), T(12, 0), 1}, {T(17, 0), T(21, 0), 1}, {T(6, 0), T(21, 0), 0}, {T(9, 0), T(18, 0), 1}},
    {{T(10, 0), T(15, 0), 0}, {T(17, 0), T(21, 0), 0}, {T(7, 0), T(21, 0), 2}, {T(7, 0), T(21, 0), 0}},
    {{T(9, 0), T(17, 0), 0}, {T(16, 0), T(20, 0), 1}, {T(6, 0), T(21, 0), 0}, {T(9, 0), T(17, 0), 0}},
    {{T(6, 0), T(18, 0), 1}, {T(9, 0), T(17, 0), 1}, {T(6, 0), T(19, 0), 0}, {T(6, 0), T(19, 0), 0}}};

static void pause(uint8_t n) { while (n--) { update_sprites(); wait_vbl_done(); } }

static uint8_t emo_of(char c) { return c == 'h' ? EMO_HEART : c == 'n' ? EMO_NOTE : c == '.' ? EMO_DOTS : EMO_EXCLAIM; }

static void gift(char c) {
    static const uint8_t legend_crop[4] = {3, 7, 10, 14};   // strawberry, melon, pumpkin, snow pea
    uint8_t cr = legend_crop[G.season & 3];
    switch (c) {
    case 'S': G.seeds[cr][R_LEGEND] = G.seeds[cr][R_LEGEND] > 253 ? 255 : G.seeds[cr][R_LEGEND] + 2; break;
    case 'g': inv_add(I_GOLD, 0, 5); break;
    case 'w': G.wood += 40; break;
    case 'E': G.energy = G.energy_max; break;
    case 'j': inv_add(I_JAM, 2, 3); break;
    case '$': G.gold += 2000; break;
    default: inv_add(I_MINNOW, 2, 1); break;
    }
    music_play(MUS_JINGLE_FANFARE);
    hud_draw();
}

static void line(uint8_t npc, const char *s) {
    if (s[0] == 'N') ui_speaker = 0;
    else set_speaker(s[0] == 'G' ? "Grandpa" : npc_names[npc]);
    say(s + 1);
}

static void play_scene(uint8_t npc, uint8_t lvl, uint8_t a) {
    const char *const *ev = events[npc] + lvl * 16;
    const char *s;
    uint8_t opp = G.facing ^ 1, good = 0, k, r;
    for (; (s = *ev) != 0; ev++) {
        switch (s[0]) {
        case '@': case 'G': case 'N': line(npc, s); break;
        case '!': if (a != 0xFF) { emote(a, emo_of(s[1]), 0); pause(40); } break;
        case 'f':
            if (a != 0xFF) { actor_face(a, s[1] == 'a' ? G.facing : opp); pause(24); actor_face(a, opp); }
            break;
        case '+': gift(s[1]); break;
        case '?':
            set_speaker(npc_names[npc]);
            for (k = 0; k < 2; k++) { s_clr(); s_add(ev[1 + k]); lbl_set(k); }
            ui_begin();
            ui_dialog(s + 2);
            r = ui_menu(0, 5, 20, lp, 2, 0);
            ui_end();
            if (r == 0xFF) r = 1;
            good = r == (uint8_t)(s[1] - '1');
            line(npc, ev[3 + r]);
            ev += 4;
            break;
        }
    }
    if (a != 0xFF) emote(a, EMO_NONE, 0);
    ui_speaker = 0;
    // a shared moment brings you closer; the kind answer more so
    k = good ? 12 : 4;
    G.friend[npc] = (uint16_t)G.friend[npc] + k > 250 ? 250 : G.friend[npc] + k;
    G.heart_seen[npc] |= 1 << lvl;
    G.heart_day = G.day | (G.season << 5);
    music_for_scene();
    hud_flash("\xF1 A new memory \xF1");
    goals_check();
}

static uint8_t due(uint8_t npc) {   // the next unseen heart event, if it can happen right now; else 0xFF
    uint8_t l, t = now_t(), seen = G.heart_seen[npc];
    const When *w;
    for (l = 0; l < 4 && (seen & (1 << l)); l++) ;
    if (l == 4 || G.friend[npc] < (uint8_t)((l + 1) * 50)) return 0xFF;
    if (G.heart_day == (G.day | (G.season << 5)) || FESTIVAL_DAY()) return 0xFF;   // one a day; none at festivals
    w = &when[npc][l];
    if (t < w->t0 || t >= w->t1 || (w->wx == 1 && wet()) || (w->wx == 2 && !wet())) return 0xFF;
    if (tl_spot(npc) & HOME_BIT) return 0xFF;   // they're indoors
    return l;
}

uint8_t tl_heart_try(uint8_t npc, uint8_t walk_in) BANKED {
    uint8_t l = due(npc), a, px = G.px;
    if (l == 0xFF) return 0;
    if (walk_in) {   // they spot you on the farm road and hurry over, to stand at your side
        a = actor_bring(npc, px + 6, 5);
        if (a != 0xFF) { actor_walk(a, px + 1, 5); actor_walk(a, px + 1, G.py); actor_face(a, D_LEFT); }
        G.facing = D_RIGHT;
    } else a = actor_slot_npc(npc);
    play_scene(npc, l, a);
    return 1;
}

void tl_arrive(void) BANKED {
    uint8_t k;
    if (G.py != 1) return;   // only when you come down the farm road
    if (FESTIVAL_DAY() && now_t() >= T(9, 0) && now_t() < T(18, 0)) { hud_flash("Festival at the hall!"); return; }
    for (k = 0; k < NUM_NPC; k++)
        if (tl_heart_try(k, 1)) return;
}
