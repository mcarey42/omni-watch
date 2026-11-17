#pragma once
/* The rim keyboard's pure core (spec section 6; Task 5): the layout asset's loader, the ring's hit-testing, the editing
 * buffer with its shift and caps, the masking, Delete's repeat and the idle stages. No LVGL, no clocks: every time is an
 * argument. The buffer holds passwords: never log it; omni_kb_wipe clears it non-elidably (wipe.h). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "omni/netlist.h"   /* omni_auth_class_t */

/* kb_lay.bin: "OKL1" | u8 version 1 | u8 nsets 4 | u8 n[4] | the keys, n[0]+..+n[3] bytes, each 0x21..0x7E. */
#define OMNI_KB_SETS 4                 /* abc, ABC, 123, #+= */
#define OMNI_KB_SET_MAX 32
typedef struct { uint8_t n[OMNI_KB_SETS]; char keys[OMNI_KB_SETS][OMNI_KB_SET_MAX]; } omni_kb_layout_t;
/* false: refused (the magic, version 1, nsets 4, 1..32 keys a set, the exact size, every key 0x21..0x7E) */
bool omni_kb_layout_load(omni_kb_layout_t *L, const void *data, size_t size);

/* The ring (the screen is 466 px, centre 233,233; one geometry for the hit test and the drawing). */
#define OMNI_KB_RING_R 196
#define OMNI_KB_BAND_IN 171
#define OMNI_KB_BAND_OUT 222
#define OMNI_KB_CANCEL_R 131
#define OMNI_KB_LOUPE_IN 85
#define OMNI_KB_CENTRE_R 150
#define OMNI_KB_CANCEL (-2)
float omni_kb_key_angle(int i, int n);               /* key 0 centred at 12 o'clock, clockwise, radians */
int omni_kb_index_at(int16_t x, int16_t y, int n);   /* r < 131: OMNI_KB_CANCEL; else the key under the angle */
bool omni_kb_in_band(int16_t x, int16_t y);          /* 171 <= r <= 233 (a ring start) */
void omni_kb_loupe_at(int i, int n, int16_t *x, int16_t *y);   /* r 196 - 85 along key i's radius */
typedef enum { KB_STROKE_NONE, KB_STROKE_RING, KB_STROKE_KEY, KB_STROKE_RESERVED } omni_kb_stroke_t;
/* ring start: RING; a centre key: KEY; r0 < 150 and not on a key, reaching the band: RESERVED; else NONE */
omni_kb_stroke_t omni_kb_stroke(int16_t x0, int16_t y0, int16_t x1, int16_t y1, bool start_on_key);
/* The firmware calls neither omni_kb_loupe_at nor omni_kb_stroke; both stay for spec 6.6 and the host tests (test_kb).
 * The keyboard places its loupe from its cached Q-fixed-point unit vectors (also the finger's direction while
 * cancelling, which is not a key), without sinf/cosf per frame; that is up to 1 px off this float position, so the
 * screen does not call it. The stroke classifier is spec 6.6's reservation: the screen does nothing on a centre stroke. */

typedef enum { KB_MODE_PASSWORD, KB_MODE_SSID, KB_MODE_CITY } omni_kb_mode_t;
typedef enum { KB_SHIFT_OFF, KB_SHIFT_ONCE, KB_SHIFT_LOCK } omni_kb_shift_t;
/* set: 0 letters, 2 digits (123), 3 symbols (#+=); the set shown is omni_kb_set() */
typedef struct { char buf[65]; uint8_t len; uint8_t mode, set, shift; bool reveal;
                 uint32_t t_type, t_shift; } omni_kb_edit_t;
void omni_kb_open(omni_kb_edit_t *e, omni_kb_mode_t m);   /* wiped, set abc, shift off, reveal off */
typedef enum { KB_TYPED, KB_AT_CAP } omni_kb_rc_t;
omni_kb_rc_t omni_kb_append(omni_kb_edit_t *e, char c, uint32_t t_ms);  /* ONCE drops to OFF (and ABC to abc) */
bool omni_kb_delete(omni_kb_edit_t *e);                                  /* false when empty */
uint8_t omni_kb_cap(const omni_kb_edit_t *e);   /* PASSWORD 64 when the buffer so far is all hex, else 63; SSID 32;
                                                   CITY 15 */
void omni_kb_shift_tap(omni_kb_edit_t *e, uint32_t t_ms);  /* abc/ABC: OFF->ONCE, ONCE within 400 ms->LOCK, ONCE->OFF,
                                                              LOCK->OFF; 123 <-> #+= in the symbol sets */
void omni_kb_123_tap(omni_kb_edit_t *e);                   /* letters -> 123; symbols -> abc */
uint8_t omni_kb_set(const omni_kb_edit_t *e);              /* the set shown: 0 abc, 1 ABC, 2 123, 3 #+= */
bool omni_kb_pass_ok(const char *p, uint8_t len, uint8_t auth_class);  /* open: any; PSK/SAE: 8..63, or 64 hex */
bool omni_kb_shows(const omni_kb_edit_t *e, uint8_t i, uint32_t now);  /* reveal, or the last char < 900 ms */
uint16_t omni_kb_repeats(uint16_t hold_ms);   /* Delete's repeats due by a hold's age: 0 < 450, then 1 + (age-450)/85 */
typedef enum { KB_IDLE_AWAKE, KB_IDLE_DIM, KB_IDLE_CLOSE } omni_kb_idle_t;
omni_kb_idle_t omni_kb_idle(uint32_t idle_ms);   /* DIM >= 45 000; CLOSE >= 60 000 */
void omni_kb_wipe(omni_kb_edit_t *e);            /* omni_wipe of the whole struct, then mode kept */
