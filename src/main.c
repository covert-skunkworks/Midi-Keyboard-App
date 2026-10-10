//  Description:
//
//		main.c -- PC MIDI Keyboard: an on-screen piano keyboard (25 to 128
//		keys, 88 by default) that sends MIDI note events to an output port.
//
//		Input sources, all of which may play at the same time:
//
//		  mouse      click / drag across keys (glissando); velocity from
//		             where the key is struck, or a fixed value
//		  touch      one note per finger
//		  keyboard   two rows, tracker style:  Z S X D C ... -> octave n
//		                                       Q 2 W 3 E ... -> octave n+1
//
//		Right-click latches a key or a chord button: the notes keep sounding
//		until the next click (either button) on the same key or button.
//		Latched notes are drawn amber.
//
//		The chord row plays preset chords on the ROOT note; left click plays
//		while held, right click latches.
//
//		Each note keeps a reference count, so a key held by the mouse and a
//		finger only sends Note Off when the last one lets go. Each note also
//		remembers the channel it was started on, so changing channel while
//		holding a note cannot leave it hanging.
//
//		Shortcuts: Left/Right octave, Up/Down velocity, PgUp/PgDn channel,
//		Space sustain (CC64), Esc panic, F5 rescan ports, F2/F3 fewer/more keys.
//

//------------------------------------------------------------------------------
//------------------------------- Includes -------------------------------------
//------------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>

#include <SDL.h>

#include "font.h"
#include "icon_data.h"
#include "midi_out.h"
#include "piano.h"

//------------------------------------------------------------------------------
//--------------------- Local macros and constants -----------------------------
//------------------------------------------------------------------------------

#define APP_TITLE      "PC MIDI Keyboard"
#define BAR_H          64
#define MARGIN         14
#define MAX_FINGERS    10
#define TXT            2  // font scale used in the top bar
#define ROW_H          34
#define ROOT_Y         ( BAR_H + 10 )         // row of root note buttons
#define CHORD_Y        ( ROOT_Y + ROW_H + 6 )  // row of chord type buttons
#define CHORD_H        ROW_H
#define CHORD_MAX      5     // most notes in one chord
#define KEYS_Y         ( CHORD_Y + CHORD_H + MARGIN + 6 )
#define WIN_MIN_W      1000
#define WIN_MIN_H      280
#define BOTTOM_H       44    // cabinet edge and status line below the keys

// W_ROOT and W_UNLATCH start the two chord rows; the rest is the top bar.
enum
{
	W_PORT, W_RESCAN, W_KEYS, W_CHANNEL, W_VELOCITY, W_OCTAVE, W_SUSTAIN, W_PANIC,
	W_ROOT, W_UNLATCH, W_COUNT
};

//------------------------------------------------------------------------------
//---------------------- Local types and structures ----------------------------
//------------------------------------------------------------------------------

typedef struct
{
	SDL_Rect r;
	int      spinner;  // 1 if clicking the left / right third steps a value
} app_widget;

typedef struct
{
	const char *name;
	int         n;
	int         iv[CHORD_MAX];  // semitones above the root
} app_chord;

typedef struct
{
	int n;                 // 0 when nothing is held
	int note[CHORD_MAX];   // notes actually pressed, within the key range
} app_hold;

typedef struct
{
	SDL_Window   *win;
	SDL_Renderer *ren;
	pk_keyboard   kb;
	app_widget    w[W_COUNT];
	int           hover;           // widget under the mouse, -1 for none
	int           port;            // selected port index, -1 for none
	int           range;           // index into the piano size table
	int           channel;         // 0..15
	int           velocity;        // 1..127, 0 = from strike position
	int           octave;          // octave of the Z row, 1..6
	int           sustain;
	uint8_t       refs[128];       // number of sources holding each note
	uint8_t       down[128];       // velocity of each sounding note
	uint8_t       note_ch[128];    // channel each note was started on
	int           mouse_note;      // -1 when the mouse plays nothing
	int           mouse_drag;      // 1 while the button is held on the keys
	SDL_FingerID  finger_id[MAX_FINGERS];
	int           finger_note[MAX_FINGERS];  // -1 for a free slot
	int           scan_note[SDL_NUM_SCANCODES];  // -1 if the key plays nothing
	uint8_t       key_latch[128];  // 1 if right-click latched, holds one ref
	int           root;            // chord root note
	SDL_Rect      root_r[12];      // root buttons, C to B
	int           hover_root;      // root button under the mouse, -1 for none
	int           scr_w;           // largest window that fits the screen
	int           scr_h;
	SDL_Rect      chord_r[32];     // chord button rectangles
	app_hold      chord_latch[32]; // notes held by each latched chord button
	app_hold      mouse_chord;     // chord held by the left button
	int           mouse_chord_id;  // its button, -1 for none
	int           hover_chord;     // chord button under the mouse, -1 for none
	char          status[96];
} app_state;

//------------------------------------------------------------------------------
//---------------------------- Local variables ---------------------------------
//------------------------------------------------------------------------------

// Tracker layout by scancode, so it follows key position, not the letters
// printed on a national layout. Offsets in semitones from the row's C.
static const struct { SDL_Scancode sc; int off; } app_keymap[] =
{
	{ SDL_SCANCODE_Z, 0 },  { SDL_SCANCODE_S, 1 },  { SDL_SCANCODE_X, 2 },
	{ SDL_SCANCODE_D, 3 },  { SDL_SCANCODE_C, 4 },  { SDL_SCANCODE_V, 5 },
	{ SDL_SCANCODE_G, 6 },  { SDL_SCANCODE_B, 7 },  { SDL_SCANCODE_H, 8 },
	{ SDL_SCANCODE_N, 9 },  { SDL_SCANCODE_J, 10 }, { SDL_SCANCODE_M, 11 },
	{ SDL_SCANCODE_COMMA, 12 }, { SDL_SCANCODE_L, 13 },
	{ SDL_SCANCODE_PERIOD, 14 }, { SDL_SCANCODE_SEMICOLON, 15 },
	{ SDL_SCANCODE_SLASH, 16 },
	{ SDL_SCANCODE_Q, 12 }, { SDL_SCANCODE_2, 13 }, { SDL_SCANCODE_W, 14 },
	{ SDL_SCANCODE_3, 15 }, { SDL_SCANCODE_E, 16 }, { SDL_SCANCODE_R, 17 },
	{ SDL_SCANCODE_5, 18 }, { SDL_SCANCODE_T, 19 }, { SDL_SCANCODE_6, 20 },
	{ SDL_SCANCODE_Y, 21 }, { SDL_SCANCODE_7, 22 }, { SDL_SCANCODE_U, 23 },
	{ SDL_SCANCODE_I, 24 }, { SDL_SCANCODE_9, 25 }, { SDL_SCANCODE_O, 26 },
	{ SDL_SCANCODE_0, 27 }, { SDL_SCANCODE_P, 28 },
	{ SDL_SCANCODE_LEFTBRACKET, 29 }, { SDL_SCANCODE_EQUALS, 30 },
	{ SDL_SCANCODE_RIGHTBRACKET, 31 },
};

// HDIM is half-diminished (m7b5); 5 is a power chord with the octave.
static const app_chord app_chords[] =
{
	{ "MAJ",  3, { 0, 4, 7 } },
	{ "MIN",  3, { 0, 3, 7 } },
	{ "DIM",  3, { 0, 3, 6 } },
	{ "AUG",  3, { 0, 4, 8 } },
	{ "SUS2", 3, { 0, 2, 7 } },
	{ "SUS4", 3, { 0, 5, 7 } },
	{ "6",    4, { 0, 4, 7, 9 } },
	{ "MIN6", 4, { 0, 3, 7, 9 } },
	{ "7",    4, { 0, 4, 7, 10 } },
	{ "MAJ7", 4, { 0, 4, 7, 11 } },
	{ "MIN7", 4, { 0, 3, 7, 10 } },
	{ "HDIM", 4, { 0, 3, 6, 10 } },
	{ "DIM7", 4, { 0, 3, 6, 9 } },
	{ "ADD9", 4, { 0, 4, 7, 14 } },
	{ "9",    5, { 0, 4, 7, 10, 14 } },
	{ "MAJ9", 5, { 0, 4, 7, 11, 14 } },
	{ "MIN9", 5, { 0, 3, 7, 10, 14 } },
	{ "5",    3, { 0, 7, 12 } },
};
#define CHORD_COUNT ( (int)SDL_arraysize( app_chords ) )
SDL_COMPILE_TIME_ASSERT( chord_slots, SDL_arraysize( app_chords ) <= 32 );

static const SDL_Color COL_TEXT   = { 222, 218, 210, 255 };
static const SDL_Color COL_AMBER  = { 255, 190, 90, 255 };
static const SDL_Color COL_DIM    = { 140, 136, 128, 255 };
static const SDL_Color COL_ACCENT = { 110, 170, 255, 255 };
static const SDL_Color COL_RED    = { 255, 110, 100, 255 };

//------------------------------------------------------------------------------
//----------------------- Local function prototypes ----------------------------
//------------------------------------------------------------------------------

static void send3( app_state *app, uint8_t a, uint8_t b, uint8_t c );
static void note_press( app_state *app, int note, int vel );
static void note_release( app_state *app, int note );
static void release_all( app_state *app );
static void hold_chord( app_state *app, int id, app_hold *out );
static void hold_release( app_state *app, app_hold *h );
static void key_latch_toggle( app_state *app, int note, int vel );
static int  chord_at( const app_state *app, int x, int y );
static int  root_at( const app_state *app, int x, int y );
static void unlatch_all( app_state *app );
static void draw_root_button( app_state *app, int pc );
static void chord_click( app_state *app, int id, int right );
static void panic( app_state *app );
static void set_sustain( app_state *app, int on );
static void select_port( app_state *app, int index );
static void rescan_ports( app_state *app );
static int  natural_height( const app_state *app, int w );
static void layout( app_state *app );
static int  widget_at( const app_state *app, int x, int y );
static void widget_click( app_state *app, int id, int x, int dir );
static void draw_widget( app_state *app, int id, const char *label,
                         const char *value, int active );
static void draw_chord_button( app_state *app, int id );
static void draw( app_state *app );
static int  save_screenshot( app_state *app, const char *path );
static void handle_key( app_state *app, const SDL_KeyboardEvent *e );
static void handle_event( app_state *app, const SDL_Event *e, int *quit );

//------------------------------------------------------------------------------
//---------------------------- Local functions ---------------------------------
//------------------------------------------------------------------------------

/**
  * @brief  Send a three byte message, noting failures in the status line.
  *
  * @param  app Application state.
  * @param  a Status byte.
  * @param  b First data byte.
  * @param  c Second data byte.
  *
  * @retval None.
  */
static void send3( app_state *app, uint8_t a, uint8_t b, uint8_t c )
{
	uint8_t m[3];

	m[0] = a; m[1] = b; m[2] = c;
	if ( mo_send( m, 3 ) == -3 )
	{
		snprintf( app->status, sizeof app->status, "SEND FAILED" );
	}
}

/**
  * @brief  Add a holder to a note, sending Note On for the first one.
  *
  * @param  app Application state.
  * @param  note MIDI note.
  * @param  vel Velocity 1..127, overridden by a fixed velocity setting.
  *
  * @retval None.
  */
static void note_press( app_state *app, int note, int vel )
{
	char name[8];

	if ( note < app->kb.first || note > app->kb.last )
	{
		return;
	}
	if ( app->velocity )
	{
		vel = app->velocity;
	}
	if ( app->refs[note]++ == 0 )
	{
		app->down[note] = (uint8_t)vel;
		app->note_ch[note] = (uint8_t)app->channel;
		send3( app, (uint8_t)( 0x90 | app->channel ), (uint8_t)note,
		       (uint8_t)vel );
		pk_note_name( note, name, sizeof name );
		snprintf( app->status, sizeof app->status, "NOTE ON  %-4s %3d  VEL %3d",
		          name, note, vel );
	}
}

/**
  * @brief  Remove a holder from a note, sending Note Off for the last one.
  *
  * @param  app Application state.
  * @param  note MIDI note.
  *
  * @retval None.
  */
static void note_release( app_state *app, int note )
{
	char name[8];

	if ( note < 0 || note > 127 || app->refs[note] == 0 )
	{
		return;
	}
	if ( --app->refs[note] == 0 )
	{
		app->down[note] = 0;
		send3( app, (uint8_t)( 0x80 | app->note_ch[note] ), (uint8_t)note, 64 );
		pk_note_name( note, name, sizeof name );
		snprintf( app->status, sizeof app->status, "NOTE OFF %-4s %3d",
		          name, note );
	}
}

/**
  * @brief  Release every note held by any source.
  *
  * @param  app Application state.
  *
  * @retval None.
  */
static void release_all( app_state *app )
{
	int i;

	for ( i = 0; i < 128; i++ )
	{
		if ( app->refs[i] )
		{
			app->refs[i] = 1;
			note_release( app, i );
		}
	}
	app->mouse_note = -1;
	app->mouse_drag = 0;
	for ( i = 0; i < MAX_FINGERS; i++ )
	{
		app->finger_note[i] = -1;
	}
	for ( i = 0; i < SDL_NUM_SCANCODES; i++ )
	{
		app->scan_note[i] = -1;
	}
	SDL_memset( app->key_latch, 0, sizeof app->key_latch );
	SDL_memset( app->chord_latch, 0, sizeof app->chord_latch );
	SDL_memset( &app->mouse_chord, 0, sizeof app->mouse_chord );
	app->mouse_chord_id = -1;
}

/**
  * @brief  Press the notes of a chord preset on the current root.
  *
  * Notes outside the keyboard range are skipped and not recorded, so the
  * release later touches only notes this hold really owns.
  *
  * @param  app Application state.
  * @param  id Chord preset index.
  * @param  out Receives the notes pressed.
  *
  * @retval None.
  */
static void hold_chord( app_state *app, int id, app_hold *out )
{
	const app_chord *c = &app_chords[id];
	char name[8];
	int i, note;

	out->n = 0;
	for ( i = 0; i < c->n; i++ )
	{
		note = app->root + c->iv[i];
		if ( note >= app->kb.first && note <= app->kb.last )
		{
			note_press( app, note, 100 );
			out->note[out->n++] = note;
		}
	}
	pk_note_name( app->root, name, sizeof name );
	snprintf( app->status, sizeof app->status, "CHORD %s %s - %d NOTES",
	          name, c->name, out->n );
}

/**
  * @brief  Release every note of a hold and empty it.
  *
  * @param  app Application state.
  * @param  h The hold.
  *
  * @retval None.
  */
static void hold_release( app_state *app, app_hold *h )
{
	int i;

	for ( i = 0; i < h->n; i++ )
	{
		note_release( app, h->note[i] );
	}
	h->n = 0;
}

/**
  * @brief  Latch a key, or release it if it is already latched.
  *
  * @param  app Application state.
  * @param  note MIDI note.
  * @param  vel Velocity for a new latch.
  *
  * @retval None.
  */
static void key_latch_toggle( app_state *app, int note, int vel )
{
	if ( app->key_latch[note] )
	{
		app->key_latch[note] = 0;
		note_release( app, note );
	}
	else
	{
		app->key_latch[note] = 1;
		note_press( app, note, vel );
	}
}

/**
  * @brief  Find the chord button under a point.
  *
  * @param  app Application state.
  * @param  x Point x.
  * @param  y Point y.
  *
  * @retval Chord index, or -1.
  */
static int chord_at( const app_state *app, int x, int y )
{
	SDL_Point p;
	int i;

	p.x = x;
	p.y = y;
	for ( i = 0; i < CHORD_COUNT; i++ )
	{
		if ( SDL_PointInRect( &p, &app->chord_r[i] ) )
		{
			return i;
		}
	}
	return -1;
}

/**
  * @brief  Find the root note button under a point.
  *
  * @param  app Application state.
  * @param  x Point x.
  * @param  y Point y.
  *
  * @retval Pitch class 0 (C) .. 11 (B), or -1.
  */
static int root_at( const app_state *app, int x, int y )
{
	SDL_Point p;
	int i;

	p.x = x;
	p.y = y;
	for ( i = 0; i < 12; i++ )
	{
		if ( SDL_PointInRect( &p, &app->root_r[i] ) )
		{
			return i;
		}
	}
	return -1;
}

/**
  * @brief  Release every latched key and chord, leaving played notes and
  *         the sustain pedal alone (unlike panic).
  *
  * @param  app Application state.
  *
  * @retval None.
  */
static void unlatch_all( app_state *app )
{
	int i;

	for ( i = 0; i < 128; i++ )
	{
		if ( app->key_latch[i] )
		{
			key_latch_toggle( app, i, 0 );
		}
	}
	for ( i = 0; i < CHORD_COUNT; i++ )
	{
		hold_release( app, &app->chord_latch[i] );
	}
	snprintf( app->status, sizeof app->status, "ALL LATCHES RELEASED" );
}

/**
  * @brief  Act on a mouse press on a chord button.
  *
  * Any click on a latched button releases it. Otherwise a right click
  * latches the chord and a left click holds it until the button comes up.
  *
  * @param  app Application state.
  * @param  id Chord index.
  * @param  right 1 for the right mouse button.
  *
  * @retval None.
  */
static void chord_click( app_state *app, int id, int right )
{
	if ( app->chord_latch[id].n )
	{
		hold_release( app, &app->chord_latch[id] );
		snprintf( app->status, sizeof app->status, "CHORD %s RELEASED",
		          app_chords[id].name );
	}
	else if ( right )
	{
		hold_chord( app, id, &app->chord_latch[id] );
	}
	else
	{
		hold_release( app, &app->mouse_chord );
		hold_chord( app, id, &app->mouse_chord );
		app->mouse_chord_id = id;
	}
}

/**
  * @brief  Release everything and silence every channel.
  *
  * @param  app Application state.
  *
  * @retval None.
  */
static void panic( app_state *app )
{
	int ch;

	release_all( app );
	app->sustain = 0;
	for ( ch = 0; ch < 16; ch++ )
	{
		send3( app, (uint8_t)( 0xB0 | ch ), 64, 0 );   // sustain off
		send3( app, (uint8_t)( 0xB0 | ch ), 123, 0 );  // all notes off
		send3( app, (uint8_t)( 0xB0 | ch ), 120, 0 );  // all sound off
	}
	snprintf( app->status, sizeof app->status, "PANIC - ALL NOTES OFF" );
}

/**
  * @brief  Switch the sustain pedal (CC64).
  *
  * @param  app Application state.
  * @param  on 1 for pressed.
  *
  * @retval None.
  */
static void set_sustain( app_state *app, int on )
{
	if ( on == app->sustain )
	{
		return;
	}
	app->sustain = on;
	send3( app, (uint8_t)( 0xB0 | app->channel ), 64, on ? 127 : 0 );
	snprintf( app->status, sizeof app->status, "SUSTAIN %s", on ? "ON" : "OFF" );
}

/**
  * @brief  Open a port, silencing the previous one first.
  *
  * @param  app Application state.
  * @param  index Port index, -1 to close.
  *
  * @retval None.
  */
static void select_port( app_state *app, int index )
{
	int rc;

	if ( mo_current() >= 0 )
	{
		panic( app );
	}
	mo_close();
	app->port = -1;
	if ( index < 0 || index >= mo_port_count() )
	{
		snprintf( app->status, sizeof app->status, "NO PORT OPEN" );
		return;
	}
	rc = mo_open( index );
	if ( rc == 0 )
	{
		app->port = index;
		snprintf( app->status, sizeof app->status, "OPENED PORT %d", index + 1 );
	}
	else
	{
		app->port = index;  // keep it selected so the user sees which failed
		snprintf( app->status, sizeof app->status,
		          "CANNOT OPEN PORT (IN USE?)" );
	}
}

/**
  * @brief  Rescan ports and reopen the previous one by name if it is still
  *         there, since indices may have shifted.
  *
  * @param  app Application state.
  *
  * @retval None.
  */
static void rescan_ports( app_state *app )
{
	char prev[MO_MAX_NAME];
	int i, n, pick = -1;

	snprintf( prev, sizeof prev, "%s", mo_port_name( app->port ) );
	if ( mo_current() >= 0 )
	{
		panic( app );
	}
	n = mo_refresh();
	for ( i = 0; i < n && prev[0]; i++ )
	{
		if ( strcmp( prev, mo_port_name( i ) ) == 0 )
		{
			pick = i;
			break;
		}
	}
	if ( pick < 0 && n > 0 )
	{
		pick = 0;
	}
	select_port( app, pick );
	if ( n == 0 )
	{
		snprintf( app->status, sizeof app->status, "NO MIDI OUTPUT PORTS FOUND" );
	}
}

/**
  * @brief  Window height at which the keys have piano proportions, so
  *         nothing is left empty below them.
  *
  * @param  app Application state.
  * @param  w Window width.
  *
  * @retval Height in pixels.
  */
static int natural_height( const app_state *app, int w )
{
	float kh = pk_natural_height( pk_range_get( app->range ),
	                              (float)( w - 2 * MARGIN ) );

	return KEYS_Y + (int)SDL_ceilf( kh ) + BOTTOM_H;
}

/**
  * @brief  Place the widgets and the keyboard for the current window size.
  *
  * Also sets the window size limits: the maximum height follows the
  * width and key count, so the window can never grow past the point where
  * the keys stop getting longer.
  *
  * @param  app Application state.
  *
  * @retval None.
  */
static void layout( app_state *app )
{
	static const int widths[W_COUNT] = { 0, 92, 96, 96, 120, 96, 108, 84,
	                                     120, 120 };
	int ww, wh, x, i, fixed = 0, h = 36, y, bw, avail, max_h, min_h;

	SDL_GetRendererOutputSize( app->ren, &ww, &wh );
	y = ( BAR_H - h ) / 2;

	for ( i = 1; i < W_ROOT; i++ )
	{
		fixed += widths[i] + 8;
	}
	x = MARGIN;
	for ( i = 0; i < W_ROOT; i++ )
	{
		app->w[i].r.x = x;
		app->w[i].r.y = y;
		app->w[i].r.h = h;
		app->w[i].r.w = i == W_PORT ? SDL_max( 160, ww - 2 * MARGIN - fixed - 8 )
		                            : widths[i];
		app->w[i].spinner = ( i == W_PORT || i == W_KEYS || i == W_CHANNEL ||
		                      i == W_VELOCITY || i == W_OCTAVE );
		x += app->w[i].r.w + 8;
	}

	// Root row: octave spinner, then the 12 root notes sharing the rest.
	app->w[W_ROOT].r.x = MARGIN;
	app->w[W_ROOT].r.y = ROOT_Y;
	app->w[W_ROOT].r.w = widths[W_ROOT];
	app->w[W_ROOT].r.h = ROW_H;
	app->w[W_ROOT].spinner = 1;
	x = MARGIN + widths[W_ROOT] + 8;
	avail = ww - MARGIN - x;
	bw = ( avail - 11 * 4 ) / 12;
	for ( i = 0; i < 12; i++ )
	{
		app->root_r[i].x = x + i * ( bw + 4 );
		app->root_r[i].y = ROOT_Y;
		app->root_r[i].w = bw;
		app->root_r[i].h = ROW_H;
	}

	// Chord row: unlatch button, then the chord types under the roots.
	app->w[W_UNLATCH].r.x = MARGIN;
	app->w[W_UNLATCH].r.y = CHORD_Y;
	app->w[W_UNLATCH].r.w = widths[W_UNLATCH];
	app->w[W_UNLATCH].r.h = CHORD_H;
	app->w[W_UNLATCH].spinner = 0;
	bw = ( avail - ( CHORD_COUNT - 1 ) * 4 ) / CHORD_COUNT;
	for ( i = 0; i < CHORD_COUNT; i++ )
	{
		app->chord_r[i].x = x + i * ( bw + 4 );
		app->chord_r[i].y = CHORD_Y;
		app->chord_r[i].w = bw;
		app->chord_r[i].h = CHORD_H;
	}

	pk_layout( &app->kb, pk_range_get( app->range ), (float)MARGIN,
	           (float)KEYS_Y, (float)( ww - 2 * MARGIN ),
	           (float)( wh - KEYS_Y - BOTTOM_H ) );

	// SDL refuses a minimum above the current maximum and vice versa, so
	// set the maximum, then the minimum, then the maximum again.
	max_h = SDL_min( natural_height( app, ww ), app->scr_h );
	min_h = SDL_min( WIN_MIN_H, max_h );
	SDL_SetWindowMaximumSize( app->win, app->scr_w, max_h );
	SDL_SetWindowMinimumSize( app->win, WIN_MIN_W, min_h );
	SDL_SetWindowMaximumSize( app->win, app->scr_w, max_h );
}

/**
  * @brief  Find the widget under a point.
  *
  * @param  app Application state.
  * @param  x Point x.
  * @param  y Point y.
  *
  * @retval Widget id, or -1.
  */
static int widget_at( const app_state *app, int x, int y )
{
	SDL_Point p;
	int i;

	p.x = x;
	p.y = y;
	for ( i = 0; i < W_COUNT; i++ )
	{
		if ( SDL_PointInRect( &p, &app->w[i].r ) )
		{
			return i;
		}
	}
	return -1;
}

/**
  * @brief  Act on a click or wheel step on a widget.
  *
  * @param  app Application state.
  * @param  id Widget id.
  * @param  x Click x, used to pick the left or right third of a spinner.
  * @param  dir Step direction for spinners, 0 to derive it from x.
  *
  * @retval None.
  */
static void widget_click( app_state *app, int id, int x, int dir )
{
	const SDL_Rect *r = &app->w[id].r;
	int n;

	if ( app->w[id].spinner && dir == 0 )
	{
		if ( x < r->x + r->w / 3 )
		{
			dir = -1;
		}
		else if ( x >= r->x + r->w - r->w / 3 )
		{
			dir = 1;
		}
	}

	switch ( id )
	{
		case W_PORT:
			n = mo_port_count();
			if ( n > 0 )
			{
				select_port( app, ( app->port + ( dir ? dir : 1 ) + n ) % n );
			}
			break;
		case W_RESCAN:
			rescan_ports( app );
			break;
		case W_KEYS:
			n = pk_range_count();
			release_all( app );  // held notes may fall outside the new range
			app->range = ( app->range + ( dir ? dir : 1 ) + n ) % n;
			layout( app );
			snprintf( app->status, sizeof app->status, "%d KEYS - %s",
			          pk_range_get( app->range )->keys,
			          pk_range_get( app->range )->what );
			break;
		case W_CHANNEL:
			if ( app->sustain )
			{
				set_sustain( app, 0 );  // do not leave the old channel pedalled
			}
			app->channel = ( app->channel + ( dir ? dir : 1 ) + 16 ) % 16;
			break;
		case W_VELOCITY:
			if ( dir < 0 )
			{
				app->velocity = app->velocity <= 8 ? 0 : app->velocity - 8;
			}
			else if ( dir > 0 )
			{
				app->velocity = app->velocity == 0 ? 8
				                : SDL_min( 127, app->velocity + 8 );
			}
			break;
		case W_OCTAVE:
			app->octave = SDL_clamp( app->octave + dir, 1, 6 );
			break;
		case W_SUSTAIN:
			set_sustain( app, !app->sustain );
			break;
		case W_PANIC:
			panic( app );
			break;
		case W_ROOT:  // octave of the root, keeping its note name
			if ( app->root + dir * 12 >= 0 && app->root + dir * 12 <= 127 )
			{
				app->root += dir * 12;
			}
			break;
		case W_UNLATCH:
			unlatch_all( app );
			break;
		default:
			break;
	}
}

/**
  * @brief  Draw one top-bar widget: a rounded panel, a small caption and a
  *         value, with arrows on spinners.
  *
  * @param  app Application state.
  * @param  id Widget id.
  * @param  label Caption, drawn small at the top left.
  * @param  value Main text.
  * @param  active 1 to draw it lit (e.g. sustain on).
  *
  * @retval None.
  */
static void draw_widget( app_state *app, int id, const char *label,
                         const char *value, int active )
{
	const SDL_Rect *r = &app->w[id].r;
	char buf[MO_MAX_NAME];
	SDL_FRect f;
	int maxc, inner, tx;

	f.x = (float)r->x; f.y = (float)r->y;
	f.w = (float)r->w; f.h = (float)r->h;
	SDL_SetRenderDrawColor( app->ren, active ? 40 : 38, active ? 78 : 37,
	                        active ? 140 : 40, 255 );
	SDL_RenderFillRectF( app->ren, &f );
	if ( app->hover == id )
	{
		SDL_SetRenderDrawColor( app->ren, 255, 255, 255, 18 );
		SDL_RenderFillRectF( app->ren, &f );
	}
	SDL_SetRenderDrawColor( app->ren, 70, 68, 72, 255 );
	SDL_RenderDrawRectF( app->ren, &f );

	fnt_draw( app->ren, f.x + 8.0f, f.y + 4.0f, 1, label, COL_DIM );

	inner = r->w - 16;
	if ( app->w[id].spinner )
	{
		fnt_draw( app->ren, f.x + 6.0f, f.y + 16.0f, TXT, "<", COL_ACCENT );
		fnt_draw( app->ren, f.x + f.w - 6.0f - (float)fnt_width( TXT, ">" ),
		          f.y + 16.0f, TXT, ">", COL_ACCENT );
		inner -= 2 * ( FNT_CELL_W * TXT + 6 );
	}

	// Shorten long port names with ".." rather than spilling over.
	maxc = inner / ( FNT_CELL_W * TXT );
	snprintf( buf, sizeof buf, "%s", value );
	if ( maxc > 2 && (int)strlen( buf ) > maxc )
	{
		buf[maxc - 2] = '.';
		buf[maxc - 1] = '.';
		buf[maxc] = '\0';
	}
	tx = r->x + ( r->w - fnt_width( TXT, buf ) ) / 2;
	fnt_draw( app->ren, (float)tx, f.y + 16.0f, TXT, buf,
	          active ? COL_ACCENT : COL_TEXT );
}

/**
  * @brief  Draw one root note button, coloured like the piano key it names
  *         (ivory for naturals, black for sharps), blue when selected.
  *
  * @param  app Application state.
  * @param  pc Pitch class 0 (C) .. 11 (B).
  *
  * @retval None.
  */
static void draw_root_button( app_state *app, int pc )
{
	static const char *const names[12] =
	{
		"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
	};
	const SDL_Rect *r = &app->root_r[pc];
	int sel = app->root % 12 == pc;
	int sharp = pk_is_black( pc );
	SDL_FRect f;
	SDL_Color c;

	f.x = (float)r->x; f.y = (float)r->y;
	f.w = (float)r->w; f.h = (float)r->h;
	if ( sel )
	{
		SDL_SetRenderDrawColor( app->ren, 52, 104, 190, 255 );
	}
	else if ( sharp )
	{
		SDL_SetRenderDrawColor( app->ren, 14, 14, 16, 255 );
	}
	else
	{
		SDL_SetRenderDrawColor( app->ren, 226, 222, 212, 255 );
	}
	SDL_RenderFillRectF( app->ren, &f );
	if ( app->hover_root == pc )
	{
		SDL_SetRenderDrawColor( app->ren, sharp || sel ? 255 : 0,
		                        sharp || sel ? 255 : 0, sharp || sel ? 255 : 0,
		                        28 );
		SDL_RenderFillRectF( app->ren, &f );
	}
	SDL_SetRenderDrawColor( app->ren, 70, 68, 72, 255 );
	SDL_RenderDrawRectF( app->ren, &f );

	c.r = c.g = c.b = ( sharp || sel ) ? 236 : 34;
	c.a = 255;
	fnt_draw( app->ren,
	          (float)( r->x + ( r->w - fnt_width( TXT, names[pc] ) ) / 2 ),
	          (float)( r->y + ( r->h - 7 * TXT ) / 2 ), TXT, names[pc], c );
}

/**
  * @brief  Draw one chord preset button: amber when latched, blue while
  *         held by the left button.
  *
  * @param  app Application state.
  * @param  id Chord index.
  *
  * @retval None.
  */
static void draw_chord_button( app_state *app, int id )
{
	const SDL_Rect *r = &app->chord_r[id];
	const char *name = app_chords[id].name;
	int latched = app->chord_latch[id].n > 0;
	int held = app->mouse_chord_id == id;
	SDL_FRect f;
	SDL_Color c;
	int s;

	f.x = (float)r->x; f.y = (float)r->y;
	f.w = (float)r->w; f.h = (float)r->h;
	if ( latched )
	{
		SDL_SetRenderDrawColor( app->ren, 96, 62, 14, 255 );
	}
	else if ( held )
	{
		SDL_SetRenderDrawColor( app->ren, 40, 78, 140, 255 );
	}
	else
	{
		SDL_SetRenderDrawColor( app->ren, 38, 37, 40, 255 );
	}
	SDL_RenderFillRectF( app->ren, &f );
	if ( app->hover_chord == id )
	{
		SDL_SetRenderDrawColor( app->ren, 255, 255, 255, 18 );
		SDL_RenderFillRectF( app->ren, &f );
	}
	SDL_SetRenderDrawColor( app->ren, 70, 68, 72, 255 );
	SDL_RenderDrawRectF( app->ren, &f );

	// Drop to the small font when the window is too narrow for the label.
	s = fnt_width( TXT, name ) <= r->w - 6 ? TXT : 1;
	c = latched ? COL_AMBER : ( held ? COL_ACCENT : COL_TEXT );
	fnt_draw( app->ren, (float)( r->x + ( r->w - fnt_width( s, name ) ) / 2 ),
	          (float)( r->y + ( r->h - 7 * s ) / 2 ), s, name, c );
}

/**
  * @brief  Draw the whole window.
  *
  * @param  app Application state.
  *
  * @retval None.
  */
static void draw( app_state *app )
{
	char v[MO_MAX_NAME];
	uint8_t latch[128];
	SDL_FRect f;
	int ww, wh, y, i, j;

	SDL_GetRendererOutputSize( app->ren, &ww, &wh );
	SDL_SetRenderDrawColor( app->ren, 24, 23, 26, 255 );
	SDL_RenderClear( app->ren );

	// Top bar, slightly lighter, with a hairline below.
	f.x = 0; f.y = 0; f.w = (float)ww; f.h = BAR_H;
	SDL_SetRenderDrawColor( app->ren, 32, 31, 35, 255 );
	SDL_RenderFillRectF( app->ren, &f );
	f.y = BAR_H; f.h = 1;
	SDL_SetRenderDrawColor( app->ren, 58, 56, 60, 255 );
	SDL_RenderFillRectF( app->ren, &f );

	if ( mo_port_count() == 0 )
	{
		snprintf( v, sizeof v, "NO PORTS" );
	}
	else if ( app->port < 0 )
	{
		snprintf( v, sizeof v, "NONE" );
	}
	else
	{
		snprintf( v, sizeof v, "%d/%d %s", app->port + 1, mo_port_count(),
		          mo_port_name( app->port ) );
	}
	draw_widget( app, W_PORT,
	             mo_current() >= 0 ? "MIDI OUT - OPEN" : "MIDI OUT - CLOSED",
	             v, 0 );
	draw_widget( app, W_RESCAN, "PORTS F5", "RESCAN", 0 );
	snprintf( v, sizeof v, "%d", pk_range_get( app->range )->keys );
	draw_widget( app, W_KEYS, "KEYS F2 F3", v, 0 );
	snprintf( v, sizeof v, "%d", app->channel + 1 );
	draw_widget( app, W_CHANNEL, "CHANNEL", v, 0 );
	if ( app->velocity )
	{
		snprintf( v, sizeof v, "%d", app->velocity );
	}
	else
	{
		snprintf( v, sizeof v, "TOUCH" );
	}
	draw_widget( app, W_VELOCITY, "VELOCITY", v, 0 );
	snprintf( v, sizeof v, "C%d", app->octave );
	draw_widget( app, W_OCTAVE, "PC KEYS", v, 0 );
	draw_widget( app, W_SUSTAIN, "SPACE", "SUSTAIN", app->sustain );
	draw_widget( app, W_PANIC, "ESC", "PANIC", 0 );

	snprintf( v, sizeof v, "%d", app->root / 12 - 1 );
	draw_widget( app, W_ROOT, "ROOT OCTAVE", v, 0 );
	for ( i = 0; i < 12; i++ )
	{
		draw_root_button( app, i );
	}
	draw_widget( app, W_UNLATCH, "RIGHT-CLICK HOLDS", "UNLATCH", 0 );
	for ( i = 0; i < CHORD_COUNT; i++ )
	{
		draw_chord_button( app, i );
	}

	// Keyboard cabinet: a dark rounded slab behind the keys.
	f.x = app->kb.area.x - 8.0f;
	f.y = app->kb.area.y - 14.0f;
	f.w = app->kb.area.w + 16.0f;
	f.h = app->kb.area.h + 22.0f;
	SDL_SetRenderDrawColor( app->ren, 12, 11, 12, 255 );
	SDL_RenderFillRectF( app->ren, &f );
	f.h = 9.0f;
	SDL_SetRenderDrawColor( app->ren, 44, 40, 40, 255 );
	SDL_RenderFillRectF( app->ren, &f );

	// Amber for every note held by a latch, from a key or a chord button.
	SDL_memcpy( latch, app->key_latch, sizeof latch );
	for ( i = 0; i < CHORD_COUNT; i++ )
	{
		for ( j = 0; j < app->chord_latch[i].n; j++ )
		{
			latch[app->chord_latch[i].note[j]] = 1;
		}
	}
	pk_draw( app->ren, &app->kb, app->down, latch );

	y = (int)( app->kb.area.y + app->kb.area.h ) + 18;
	fnt_draw( app->ren, (float)MARGIN, (float)y, TXT, app->status,
	          mo_current() >= 0 ? COL_TEXT : COL_RED );
	snprintf( v, sizeof v, "CH %d", app->channel + 1 );
	fnt_draw( app->ren, (float)( ww - MARGIN - fnt_width( TXT, v ) ), (float)y,
	          TXT, v, COL_DIM );

}

/**
  * @brief  Draw one frame and save it as a BMP file.
  *
  * @param  app Application state.
  * @param  path Output file.
  *
  * @retval 0 on success, 2 on failure.
  */
static int save_screenshot( app_state *app, const char *path )
{
	SDL_Surface *s;
	int w, h, rc = 2;

	draw( app );
	SDL_GetRendererOutputSize( app->ren, &w, &h );
	s = SDL_CreateRGBSurfaceWithFormat( 0, w, h, 32, SDL_PIXELFORMAT_ARGB8888 );
	if ( s && SDL_RenderReadPixels( app->ren, NULL, SDL_PIXELFORMAT_ARGB8888,
	                                s->pixels, s->pitch ) == 0 &&
	     SDL_SaveBMP( s, path ) == 0 )
	{
		rc = 0;
	}
	SDL_FreeSurface( s );
	return rc;
}

/**
  * @brief  Handle a key press or release from the computer keyboard.
  *
  * @param  app Application state.
  * @param  e Keyboard event.
  *
  * @retval None.
  */
static void handle_key( app_state *app, const SDL_KeyboardEvent *e )
{
	SDL_Scancode sc = e->keysym.scancode;
	size_t i;
	int note;

	if ( e->type == SDL_KEYUP )
	{
		if ( sc == SDL_SCANCODE_SPACE )
		{
			set_sustain( app, 0 );
		}
		if ( app->scan_note[sc] >= 0 )
		{
			note_release( app, app->scan_note[sc] );
			app->scan_note[sc] = -1;
		}
		return;
	}
	if ( e->repeat )
	{
		return;
	}

	switch ( sc )
	{
		case SDL_SCANCODE_SPACE:    set_sustain( app, 1 ); return;
		case SDL_SCANCODE_ESCAPE:   panic( app ); return;
		case SDL_SCANCODE_F5:       rescan_ports( app ); return;
		case SDL_SCANCODE_F2:       widget_click( app, W_KEYS, 0, -1 ); return;
		case SDL_SCANCODE_F3:       widget_click( app, W_KEYS, 0, 1 ); return;
		case SDL_SCANCODE_LEFT:     widget_click( app, W_OCTAVE, 0, -1 ); return;
		case SDL_SCANCODE_RIGHT:    widget_click( app, W_OCTAVE, 0, 1 ); return;
		case SDL_SCANCODE_DOWN:     widget_click( app, W_VELOCITY, 0, -1 ); return;
		case SDL_SCANCODE_UP:       widget_click( app, W_VELOCITY, 0, 1 ); return;
		case SDL_SCANCODE_PAGEDOWN: widget_click( app, W_CHANNEL, 0, -1 ); return;
		case SDL_SCANCODE_PAGEUP:   widget_click( app, W_CHANNEL, 0, 1 ); return;
		default: break;
	}

	for ( i = 0; i < SDL_arraysize( app_keymap ); i++ )
	{
		if ( app_keymap[i].sc == sc && app->scan_note[sc] < 0 )
		{
			note = ( app->octave + 1 ) * 12 + app_keymap[i].off;
			if ( note >= app->kb.first && note <= app->kb.last )
			{
				app->scan_note[sc] = note;
				note_press( app, note, 100 );
			}
			return;
		}
	}
}

/**
  * @brief  Dispatch one SDL event.
  *
  * @param  app Application state.
  * @param  e Event.
  * @param  quit Set to 1 when the window is closed.
  *
  * @retval None.
  */
static void handle_event( app_state *app, const SDL_Event *e, int *quit )
{
	int ww, wh, note, vel, id, i, slot, right;
	float fx, fy;

	switch ( e->type )
	{
		case SDL_QUIT:
			*quit = 1;
			break;

		case SDL_WINDOWEVENT:
			if ( e->window.event == SDL_WINDOWEVENT_SIZE_CHANGED )
			{
				layout( app );
			}
			else if ( e->window.event == SDL_WINDOWEVENT_FOCUS_LOST )
			{
				// Key-up events are lost while unfocused; let go of PC keys.
				for ( i = 0; i < SDL_NUM_SCANCODES; i++ )
				{
					if ( app->scan_note[i] >= 0 )
					{
						note_release( app, app->scan_note[i] );
						app->scan_note[i] = -1;
					}
				}
			}
			break;

		case SDL_KEYDOWN:
		case SDL_KEYUP:
			handle_key( app, &e->key );
			break;

		case SDL_MOUSEBUTTONDOWN:
			right = e->button.button == SDL_BUTTON_RIGHT;
			if ( ( e->button.button != SDL_BUTTON_LEFT && !right ) ||
			     e->button.which == SDL_TOUCH_MOUSEID )
			{
				break;
			}
			id = widget_at( app, e->button.x, e->button.y );
			if ( id >= 0 )
			{
				if ( !right )
				{
					widget_click( app, id, e->button.x, 0 );
				}
				break;
			}
			id = root_at( app, e->button.x, e->button.y );
			if ( id >= 0 )
			{
				app->root = ( app->root / 12 ) * 12 + id;
				break;
			}
			id = chord_at( app, e->button.x, e->button.y );
			if ( id >= 0 )
			{
				chord_click( app, id, right );
				break;
			}
			note = pk_hit( &app->kb, (float)e->button.x, (float)e->button.y,
			               &vel );
			if ( note >= 0 && ( right || app->key_latch[note] ) )
			{
				key_latch_toggle( app, note, vel );  // no drag from a latch
				break;
			}
			if ( right )
			{
				break;
			}
			app->mouse_drag = 1;
			SDL_CaptureMouse( SDL_TRUE );
			if ( note >= 0 )
			{
				app->mouse_note = note;
				note_press( app, note, vel );
			}
			break;

		case SDL_MOUSEBUTTONUP:
			if ( e->button.button != SDL_BUTTON_LEFT ||
			     e->button.which == SDL_TOUCH_MOUSEID )
			{
				break;
			}
			SDL_CaptureMouse( SDL_FALSE );
			app->mouse_drag = 0;
			note_release( app, app->mouse_note );
			app->mouse_note = -1;
			hold_release( app, &app->mouse_chord );
			app->mouse_chord_id = -1;
			break;

		case SDL_MOUSEMOTION:
			if ( e->motion.which == SDL_TOUCH_MOUSEID )
			{
				break;
			}
			app->hover = widget_at( app, e->motion.x, e->motion.y );
			app->hover_chord = chord_at( app, e->motion.x, e->motion.y );
			app->hover_root = root_at( app, e->motion.x, e->motion.y );
			if ( !app->mouse_drag )
			{
				break;
			}
			note = pk_hit( &app->kb, (float)e->motion.x, (float)e->motion.y,
			               &vel );
			if ( note != app->mouse_note )  // glissando
			{
				note_release( app, app->mouse_note );
				app->mouse_note = note;
				if ( note >= 0 )
				{
					note_press( app, note, vel );
				}
			}
			break;

		case SDL_MOUSEWHEEL:
			SDL_GetMouseState( &ww, &wh );
			id = widget_at( app, ww, wh );
			if ( id >= 0 && app->w[id].spinner && e->wheel.y != 0 )
			{
				widget_click( app, id, 0, e->wheel.y > 0 ? 1 : -1 );
			}
			else if ( root_at( app, ww, wh ) >= 0 && e->wheel.y != 0 )
			{
				app->root = SDL_clamp( app->root + ( e->wheel.y > 0 ? 1 : -1 ),
				                       0, 127 );
			}
			break;

		case SDL_FINGERDOWN:
		case SDL_FINGERMOTION:
		case SDL_FINGERUP:
			SDL_GetRendererOutputSize( app->ren, &ww, &wh );
			fx = e->tfinger.x * (float)ww;
			fy = e->tfinger.y * (float)wh;
			slot = -1;
			for ( i = 0; i < MAX_FINGERS; i++ )
			{
				if ( app->finger_note[i] != -1 &&
				     app->finger_id[i] == e->tfinger.fingerId )
				{
					slot = i;
				}
			}
			if ( e->type == SDL_FINGERUP )
			{
				if ( slot >= 0 )
				{
					note_release( app, app->finger_note[slot] );
					app->finger_note[slot] = -1;
				}
				break;
			}
			if ( e->type == SDL_FINGERDOWN && fy < CHORD_Y + CHORD_H )
			{
				id = widget_at( app, (int)fx, (int)fy );
				if ( id >= 0 )
				{
					widget_click( app, id, (int)fx, 0 );
				}
				id = root_at( app, (int)fx, (int)fy );
				if ( id >= 0 )
				{
					app->root = ( app->root / 12 ) * 12 + id;
				}
				id = chord_at( app, (int)fx, (int)fy );
				if ( id >= 0 )
				{
					chord_click( app, id, 1 );  // a tap toggles the latch
				}
				break;
			}
			note = pk_hit( &app->kb, fx, fy, &vel );
			if ( slot < 0 )
			{
				for ( i = 0; i < MAX_FINGERS && slot < 0; i++ )
				{
					if ( app->finger_note[i] == -1 )
					{
						slot = i;
					}
				}
				if ( slot < 0 || e->type != SDL_FINGERDOWN )
				{
					break;
				}
				app->finger_id[slot] = e->tfinger.fingerId;
				app->finger_note[slot] = -2;  // held, but on no key
			}
			if ( note != app->finger_note[slot] &&
			     !( note < 0 && app->finger_note[slot] == -2 ) )
			{
				note_release( app, app->finger_note[slot] );
				app->finger_note[slot] = note >= 0 ? note : -2;
				if ( note >= 0 )
				{
					note_press( app, note, vel );
				}
			}
			break;

		default:
			break;
	}
}

//------------------------------------------------------------------------------
//------------------------------ Public API ------------------------------------
//------------------------------------------------------------------------------

/**
  * @brief  Program entry: open the window and run the event loop.
  *
  * Test options, used to check the drawing without a person at the screen:
  *   --screenshot FILE.bmp  draw one frame off screen, save it, exit; no MIDI
  *   --keys N               start with the N-key range
  *   --demo                 latch a chord and a key, play one note
  *
  * @param  argc Argument count.
  * @param  argv Arguments.
  *
  * @retval 0 on normal exit, 1 if SDL could not start, 2 screenshot failed.
  */
int main( int argc, char *argv[] )
{
	static app_state app;
	SDL_Event e;
	SDL_Rect ub;
	SDL_Surface *icon;
	const char *shot = NULL;
	int quit = 0, i, keys = 0, demo = 0, w;

	for ( i = 1; i < argc; i++ )
	{
		if ( strcmp( argv[i], "--screenshot" ) == 0 && i + 1 < argc )
		{
			shot = argv[++i];
		}
		else if ( strcmp( argv[i], "--keys" ) == 0 && i + 1 < argc )
		{
			keys = SDL_atoi( argv[++i] );
		}
		else if ( strcmp( argv[i], "--demo" ) == 0 )
		{
			demo = 1;
		}
	}

	// Real pixels on high-DPI Windows screens instead of a blurry upscale.
	SDL_SetHint( SDL_HINT_WINDOWS_DPI_AWARENESS, "permonitorv2" );
	// Fingers are handled as fingers; synthetic mouse events would double them.
	SDL_SetHint( SDL_HINT_TOUCH_MOUSE_EVENTS, "0" );
	SDL_SetHint( SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1" );

	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_EVENTS ) != 0 )
	{
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, APP_TITLE,
		                          SDL_GetError(), NULL );
		return 1;
	}
	// Never larger than the screen's usable area (minus room for the title
	// bar and frame), so the window opens fully visible.
	if ( SDL_GetDisplayUsableBounds( 0, &ub ) != 0 )
	{
		ub.w = 1920;
		ub.h = 1040;
	}
	app.scr_w = SDL_max( WIN_MIN_W, ub.w - 16 );
	app.scr_h = SDL_max( WIN_MIN_H, ub.h - 48 );
	app.range = PK_DEFAULT_RANGE;
	for ( i = 0; i < pk_range_count(); i++ )
	{
		if ( pk_range_get( i )->keys == keys )
		{
			app.range = i;
		}
	}
	w = SDL_min( 1560, app.scr_w );
	app.win = SDL_CreateWindow( APP_TITLE, SDL_WINDOWPOS_CENTERED,
	                            SDL_WINDOWPOS_CENTERED, w,
	                            SDL_min( natural_height( &app, w ), app.scr_h ),
	                            SDL_WINDOW_RESIZABLE |
	                            ( shot ? SDL_WINDOW_HIDDEN : 0 ) );
	if ( app.win )
	{
		// The software renderer reads back reliably from a hidden window.
		app.ren = SDL_CreateRenderer( app.win, -1, shot ? SDL_RENDERER_SOFTWARE
		                              : SDL_RENDERER_ACCELERATED |
		                                SDL_RENDERER_PRESENTVSYNC );
		if ( !app.ren )
		{
			app.ren = SDL_CreateRenderer( app.win, -1, SDL_RENDERER_SOFTWARE );
		}
	}
	if ( !app.win || !app.ren )
	{
		SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, APP_TITLE,
		                          SDL_GetError(), app.win );
		SDL_Quit();
		return 1;
	}
	SDL_SetRenderDrawBlendMode( app.ren, SDL_BLENDMODE_BLEND );

	// Title bar / dock / taskbar icon. RGBA32 is byte order R,G,B,A on any
	// endianness, matching the generated array.
	icon = SDL_CreateRGBSurfaceWithFormatFrom( (void *)icon_rgba, ICON_SIZE,
	                                           ICON_SIZE, 32, ICON_SIZE * 4,
	                                           SDL_PIXELFORMAT_RGBA32 );
	if ( icon )
	{
		SDL_SetWindowIcon( app.win, icon );
		SDL_FreeSurface( icon );
	}

	app.port = -1;
	app.root = 60;  // middle C
	app.mouse_chord_id = -1;
	app.hover_chord = -1;
	app.hover_root = -1;
	app.octave = 3;
	app.hover = -1;
	app.mouse_note = -1;
	for ( i = 0; i < MAX_FINGERS; i++ )
	{
		app.finger_note[i] = -1;
	}
	for ( i = 0; i < SDL_NUM_SCANCODES; i++ )
	{
		app.scan_note[i] = -1;
	}

	layout( &app );
	if ( shot )
	{
		snprintf( app.status, sizeof app.status, "SCREENSHOT MODE - NO MIDI" );
	}
	else if ( mo_init() != 0 )
	{
		snprintf( app.status, sizeof app.status, "MIDI SYSTEM UNAVAILABLE" );
	}
	else
	{
		rescan_ports( &app );
	}

	if ( demo )
	{
		chord_click( &app, 0, 1 );                // latch C major on the root
		key_latch_toggle( &app, app.root + 16, 90 );  // latch E5
		note_press( &app, app.root - 17, 110 );   // play G2
	}
	if ( shot )
	{
		quit = save_screenshot( &app, shot );
		SDL_DestroyRenderer( app.ren );
		SDL_DestroyWindow( app.win );
		SDL_Quit();
		return quit;
	}

	while ( !quit )
	{
		// Sleep until something happens; MIDI is sent while handling the
		// event, so drawing never adds latency.
		if ( SDL_WaitEventTimeout( &e, 250 ) )
		{
			handle_event( &app, &e, &quit );
			while ( SDL_PollEvent( &e ) )
			{
				handle_event( &app, &e, &quit );
			}
		}
		draw( &app );
		SDL_RenderPresent( app.ren );
	}

	panic( &app );
	mo_shutdown();
	SDL_DestroyRenderer( app.ren );
	SDL_DestroyWindow( app.win );
	SDL_Quit();
	return 0;
}
