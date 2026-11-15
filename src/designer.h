// Stilwell Farms -- design tools: paint strokes, undo, farm colours, blueprints
#ifndef DESIGNER_H
#define DESIGNER_H
#include "game.h"

// build catalogue kinds (the catalogue itself is in menus.c)
enum { BK_DECOR, BK_MACHINE, BK_TREE, BK_POND, BK_FILL, BK_BLDG, BK_MOVE, BK_REMOVE, BK_COPY, BK_STAMP };
#define CAT_MOVE 5              // MOVE category: 0 Move, 1 Remove (the eraser)
#define CAT_BLUEPRINT 6
#define CAT_COLOURS 7

// blueprint piece codes (G.bp_pal): one byte names what a cell holds
#define PC_MACHINE 0x40         // 0x01..0x3F decor kind, 0x40 | machine, 0x50 | tree, 0x60 | building type (top-left cell), 0x70 pond
#define PC_TREE 0x50
#define PC_BLDG 0x60
#define PC_POND 0x70

// menus.c: the catalogue rules, one cell at a time
void bk_cost(uint8_t kind, uint8_t arg, uint16_t *g, uint8_t *w, uint8_t *s) BANKED;
uint8_t bk_fits(uint8_t x, uint8_t y, uint8_t kind, uint8_t arg) BANKED;   // one cell of the item may go here
void bk_put(uint8_t x, uint8_t y, uint8_t kind, uint8_t arg) BANKED;       // place a one-cell item (no charge)
void build_select(void) BANKED;     // SELECT in build mode: eraser <-> the item in hand
uint8_t build_quick(void) BANKED;   // START in build mode: undo / items / ... ; 1 = go on to the main menu

// designer.c
extern uint8_t paint_on;            // 1 while A is held after a placement (a stroke), 2 = stroke stopped (out of money)
extern uint8_t ghost_x, ghost_y, ghost_w, ghost_h, ghost_slot;   // blueprint preview drawn by farmview.c
void paint_begin(uint8_t kind, uint8_t arg) BANKED;
uint8_t paint_cell(uint8_t x, uint8_t y) BANKED;   // place (or erase) one cell of the stroke; 1 = done
void paint_step(uint8_t lx, uint8_t ly) BANKED;    // a step with A held: lay the cell left behind / erase ahead
void paint_bump(void) BANKED;                      // walking into something with the eraser held
void paint_end(void) BANKED;
uint8_t erase_at(uint8_t x, uint8_t y) BANKED;     // remove decor/machine/tree (undoable)
void undo_bld(uint8_t slot) BANKED;                // a building was just placed
void undo_move(uint16_t from, uint16_t to) BANKED;
void undo_moveb(uint8_t slot, uint8_t ox, uint8_t oy) BANKED;
uint8_t undo_last(void) BANKED;
uint8_t undo_count(void) BANKED;
void undo_clear(void) BANKED;       // scene change / night: history no longer applies
void colour_menu(void) BANKED;
void accent_patch(void) BANKED;     // main.c's palette push: put the farm colours into pbuf (raw season palette)
extern uint16_t pbuf[32];
void pal_refresh(void);             // main.c
uint8_t bp_copy_cursor(uint8_t fx, uint8_t fy, uint8_t *x0, uint8_t *y0, uint8_t *w, uint8_t *h) BANKED;
void bp_copy_click(void) BANKED;
void bp_copy_reset(void) BANKED;
uint8_t bp_ghost(uint8_t slot, uint8_t x0, uint8_t y0) BANKED;   // move the preview; 1 = something fits
void ghost_hide(void) BANKED;
void bp_stamp(uint8_t slot, uint8_t x0, uint8_t y0) BANKED;
// farmview.c
mt_t bld_type_mt(uint8_t type, uint8_t part) BANKED;
#endif
