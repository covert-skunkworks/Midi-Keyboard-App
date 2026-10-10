//  Description:
//
//		piano.h -- layout, hit testing and drawing of a piano keyboard.
//
//		The range is chosen at layout time from the table of common sizes
//		(pk_range_get), from a 25-key controller to the full 128-note MIDI
//		range; the default is 88 keys, A0 (MIDI note 21) to C8 (note 108).
//		Every range starts and ends on a white key, which is how real
//		keyboards are built. Black keys are not centred on the gap between
//		white keys; like a real piano they are pushed outwards inside each
//		group (C#/D# and F#/G#/A#), which is what makes it read as a piano
//		rather than a grid.
//
//		Proportions follow a real action: a white key is about 6.4 times
//		as long as it is wide, a black key about 63 % of that length and
//		58 % of the white key width.
//

#ifndef PIANO_H
#define PIANO_H

#include <stdint.h>
#include <stddef.h>

#include <SDL.h>

#define PK_DEFAULT_RANGE  5  // index of 88 keys in the range table

typedef struct
{
	int         keys;   // number of keys
	int         first;  // lowest MIDI note
	int         last;   // highest MIDI note
	const char *what;   // typical instrument, for the status line
} pk_range;

typedef struct
{
	float x;      // left edge in pixels
	float w;      // width in pixels
	int   black;  // 1 for a black key
} pk_key;

typedef struct
{
	SDL_FRect area;      // whole keyboard, white key height
	int       first;     // lowest MIDI note shown
	int       last;      // highest MIDI note shown
	float     white_w;   // width of one white key
	float     black_h;   // length of a black key
	pk_key    key[128];  // indexed by MIDI note, valid for first..last
} pk_keyboard;

/**
  * @brief  Get one of the built-in keyboard sizes.
  *
  * @param  index 0 .. pk_range_count() - 1.
  *
  * @retval The range, or the 88-key range for a bad index.
  */
const pk_range *pk_range_get( int index );

/**
  * @brief  Get the number of built-in keyboard sizes.
  *
  * @retval Number of entries in the range table.
  */
int pk_range_count( void );

/**
  * @brief  Report whether a MIDI note is a black key.
  *
  * @param  note MIDI note number.
  *
  * @retval 1 for a black key, 0 for a white one.
  */
int pk_is_black( int note );

/**
  * @brief  Format a note name such as "C#4" (middle C is C4, note 60).
  *
  * @param  note MIDI note number 0..127.
  * @param  out Receives the name.
  * @param  size Size of out, at least 5.
  *
  * @retval None.
  */
void pk_note_name( int note, char *out, size_t size );

/**
  * @brief  Height a keyboard needs to have natural key proportions.
  *
  * @param  range Notes to show.
  * @param  w Width available.
  *
  * @retval Key length in pixels when the keys use the full width.
  */
float pk_natural_height( const pk_range *range, float w );

/**
  * @brief  Lay the keyboard out in a rectangle.
  *
  * The keys take the full width. The height is limited to the natural
  * proportion of a key; the keyboard sits at the top of the rectangle.
  *
  * @param  kb Receives the layout.
  * @param  range Notes to show.
  * @param  x Left edge.
  * @param  y Top edge.
  * @param  w Width available.
  * @param  h Height available.
  *
  * @retval None.
  */
void pk_layout( pk_keyboard *kb, const pk_range *range,
                float x, float y, float w, float h );

/**
  * @brief  Find the key under a point.
  *
  * Black keys are tested first because they lie on top of the white ones.
  * The velocity depends on where the key is struck: near the player
  * (the front edge) is loud, near the fallboard is soft.
  *
  * @param  kb Keyboard layout.
  * @param  x Point x.
  * @param  y Point y.
  * @param  velocity Receives the velocity 1..127, may be NULL.
  *
  * @retval The MIDI note, or -1 if the point misses the keyboard.
  */
int pk_hit( const pk_keyboard *kb, float x, float y, int *velocity );

/**
  * @brief  Draw the keyboard.
  *
  * Latched keys are drawn amber instead of blue, so a held note can be
  * told from one being played.
  *
  * @param  ren Renderer, with blend mode set to SDL_BLENDMODE_BLEND.
  * @param  kb Keyboard layout.
  * @param  down Velocity of each sounding note, 0 for keys that are up.
  * @param  latch Non-zero for notes held by a latch, may be NULL.
  *
  * @retval None.
  */
void pk_draw( SDL_Renderer *ren, const pk_keyboard *kb,
              const uint8_t down[128], const uint8_t *latch );

#endif
