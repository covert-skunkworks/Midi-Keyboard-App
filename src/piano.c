//  Description:
//
//		piano.c -- layout, hit testing and drawing of an 88-key keyboard.
//
//		All shading is done with SDL_RenderGeometry: every key is a convex
//		polygon with rounded front corners, drawn as a triangle fan whose
//		vertex colours give a vertical gradient. No textures or images.
//
//		A white key is drawn as a darker front lip with the lighter top
//		face over it; a pressed key gets a thinner lip, so it reads as
//		pushed down. Black keys get a soft drop shadow, a bevelled body and
//		an inset face, and the pressed colour deepens with velocity.
//

//------------------------------------------------------------------------------
//------------------------------- Includes -------------------------------------
//------------------------------------------------------------------------------

#include "piano.h"
#include "font.h"

#include <stdio.h>

//------------------------------------------------------------------------------
//--------------------- Local macros and constants -----------------------------
//------------------------------------------------------------------------------

#define PK_KEY_RATIO    6.4f   // white key length / width
#define PK_KEY_MIN_RATIO 4.5f  // stubbiest allowed, about a mini controller
#define PK_BLACK_LEN    0.63f  // black key length / white key length
#define PK_BLACK_WIDTH  0.58f  // black key width / white key width
#define PK_ARC_STEPS    6      // segments per rounded corner

//------------------------------------------------------------------------------
//---------------------------- Local variables ---------------------------------
//------------------------------------------------------------------------------

// 1 for black keys, indexed by note % 12. Kept separate from the shift
// table because G# has no shift and would otherwise read as white.
static const uint8_t pk_black[12] = { 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0 };

// Shift of each black key from the white-key gap, in white key widths,
// indexed by note % 12.
static const float pk_black_shift[12] =
{
	0.0f, -0.10f, 0.0f, 0.10f, 0.0f, 0.0f, -0.13f, 0.0f, 0.0f, 0.0f, 0.13f, 0.0f
};

// Note names use middle C = C4 (note 60). The extended Boesendorfer grands
// are the 225 (92 keys, F0-C8) and the 290 Imperial (97 keys, C0-C8).
static const pk_range pk_ranges[] =
{
	{  25, 48,  72, "2 OCTAVES - MINI CONTROLLER" },
	{  37, 48,  84, "3 OCTAVES - KORG PROPHECY" },
	{  49, 36,  84, "4 OCTAVES - CONTROLLER" },
	{  61, 36,  96, "5 OCTAVES - SYNTH" },
	{  73, 28, 100, "E1-E7 - STAGE PIANO" },
	{  88, 21, 108, "A0-C8 - PIANO" },
	{  92, 17, 108, "F0-C8 - BOESENDORFER 225" },
	{  97, 12, 108, "C0-C8 - BOESENDORFER IMPERIAL" },
	{ 128,  0, 127, "FULL MIDI RANGE" },
};

// Pressed-key colours: [0] played (blue), [1] latched (amber). Order:
// white top, white bottom, white lip top, white lip bottom, black body top,
// black body bottom, black face top, black face bottom.
static const SDL_Color pk_hot[2][8] =
{
	{ { 128, 176, 238, 255 }, { 200, 224, 255, 255 }, { 120, 160, 215, 255 },
	  {  84, 120, 175, 255 }, {   8,  22,  48, 255 }, {  40,  82, 150, 255 },
	  {  34,  86, 170, 255 }, {  82, 142, 230, 255 } },
	{ { 236, 170,  80, 255 }, { 255, 224, 160, 255 }, { 220, 156,  70, 255 },
	  { 176, 112,  36, 255 }, {  52,  30,   4, 255 }, { 150,  96,  22, 255 },
	  { 176, 110,  24, 255 }, { 240, 172,  64, 255 } },
};

static const char *const pk_names[12] =
{
	"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
};

//------------------------------------------------------------------------------
//----------------------- Local function prototypes ----------------------------
//------------------------------------------------------------------------------

static SDL_Color mix( SDL_Color a, SDL_Color b, float t );
static SDL_Color rgba( Uint8 r, Uint8 g, Uint8 b, Uint8 a );
static void fill_key_shape( SDL_Renderer *ren, float x, float y, float w,
                            float h, float rad, SDL_Color top, SDL_Color bot );

//------------------------------------------------------------------------------
//---------------------------- Local functions ---------------------------------
//------------------------------------------------------------------------------

/**
  * @brief  Build a colour.
  *
  * @param  r Red.
  * @param  g Green.
  * @param  b Blue.
  * @param  a Alpha.
  *
  * @retval The colour.
  */
static SDL_Color rgba( Uint8 r, Uint8 g, Uint8 b, Uint8 a )
{
	SDL_Color c;

	c.r = r; c.g = g; c.b = b; c.a = a;
	return c;
}

/**
  * @brief  Blend two colours linearly.
  *
  * @param  a Colour at t = 0.
  * @param  b Colour at t = 1.
  * @param  t Position 0..1.
  *
  * @retval The blended colour.
  */
static SDL_Color mix( SDL_Color a, SDL_Color b, float t )
{
	SDL_Color c;

	c.r = (Uint8)( a.r + ( b.r - a.r ) * t );
	c.g = (Uint8)( a.g + ( b.g - a.g ) * t );
	c.b = (Uint8)( a.b + ( b.b - a.b ) * t );
	c.a = (Uint8)( a.a + ( b.a - a.a ) * t );
	return c;
}

/**
  * @brief  Fill a rectangle whose bottom corners are rounded, with a
  *         vertical gradient.
  *
  * The shape is convex, so a fan around its centre covers it exactly.
  *
  * @param  ren Renderer.
  * @param  x Left edge.
  * @param  y Top edge.
  * @param  w Width.
  * @param  h Height.
  * @param  rad Corner radius, 0 for square corners.
  * @param  top Colour at the top edge.
  * @param  bot Colour at the bottom edge.
  *
  * @retval None.
  */
static void fill_key_shape( SDL_Renderer *ren, float x, float y, float w,
                            float h, float rad, SDL_Color top, SDL_Color bot )
{
	SDL_Vertex v[4 + 2 * ( PK_ARC_STEPS + 1 )];
	int idx[3 * ( 3 + 2 * ( PK_ARC_STEPS + 1 ) )];
	float a, cx, cy;
	int n = 0, ni = 0, i;

	if ( w <= 0.0f || h <= 0.0f )
	{
		return;
	}
	if ( rad > w * 0.5f )
	{
		rad = w * 0.5f;
	}
	if ( rad > h )
	{
		rad = h;
	}

	SDL_memset( v, 0, sizeof v );
	v[n].position.x = x + w * 0.5f;
	v[n++].position.y = y + h * 0.5f;
	v[n].position.x = x;
	v[n++].position.y = y;
	v[n].position.x = x + w;
	v[n++].position.y = y;

	// Right corner sweeps from pointing right (0) to pointing down (90).
	cx = x + w - rad;
	cy = y + h - rad;
	for ( i = 0; i <= PK_ARC_STEPS; i++ )
	{
		a = (float)i / PK_ARC_STEPS * 1.5707963f;
		v[n].position.x = cx + rad * SDL_cosf( a );
		v[n++].position.y = cy + rad * SDL_sinf( a );
	}
	cx = x + rad;
	for ( i = 0; i <= PK_ARC_STEPS; i++ )
	{
		a = 1.5707963f + (float)i / PK_ARC_STEPS * 1.5707963f;
		v[n].position.x = cx + rad * SDL_cosf( a );
		v[n++].position.y = cy + rad * SDL_sinf( a );
	}

	for ( i = 0; i < n; i++ )
	{
		v[i].color = mix( top, bot, ( v[i].position.y - y ) / h );
	}
	for ( i = 1; i < n; i++ )
	{
		idx[ni++] = 0;
		idx[ni++] = i;
		idx[ni++] = ( i + 1 < n ) ? i + 1 : 1;
	}
	SDL_RenderGeometry( ren, NULL, v, n, idx, ni );
}

//------------------------------------------------------------------------------
//------------------------------ Public API ------------------------------------
//------------------------------------------------------------------------------

/**
  * @brief  Get one of the built-in keyboard sizes.
  *
  * @param  index 0 .. pk_range_count() - 1.
  *
  * @retval The range, or the 88-key range for a bad index.
  */
const pk_range *pk_range_get( int index )
{
	if ( index < 0 || index >= pk_range_count() )
	{
		index = PK_DEFAULT_RANGE;
	}
	return &pk_ranges[index];
}

/**
  * @brief  Get the number of built-in keyboard sizes.
  *
  * @retval Number of entries in the range table.
  */
int pk_range_count( void ) { return (int)SDL_arraysize( pk_ranges ); }

/**
  * @brief  Report whether a MIDI note is a black key.
  *
  * @param  note MIDI note number.
  *
  * @retval 1 for a black key, 0 for a white one.
  */
int pk_is_black( int note )
{
	return pk_black[( ( note % 12 ) + 12 ) % 12];
}

/**
  * @brief  Format a note name such as "C#4".
  *
  * @param  note MIDI note number 0..127.
  * @param  out Receives the name.
  * @param  size Size of out.
  *
  * @retval None.
  */
void pk_note_name( int note, char *out, size_t size )
{
	note &= 0x7F;  // lets GCC prove the octave is -1..9 and fits
	snprintf( out, size, "%s%d", pk_names[note % 12], note / 12 - 1 );
}

/**
  * @brief  Height a keyboard needs to have natural key proportions.
  *
  * @param  range Notes to show.
  * @param  w Width available.
  *
  * @retval Key length in pixels when the keys use the full width.
  */
float pk_natural_height( const pk_range *range, float w )
{
	int n, whites = 0;

	for ( n = range->first; n <= range->last; n++ )
	{
		whites += !pk_is_black( n );
	}
	return w / (float)whites * PK_KEY_RATIO;
}

/**
  * @brief  Lay the keyboard out in a rectangle.
  *
  * Small keyboards would otherwise stretch to the full width and get
  * short, fat keys, so a white key is never wider than the height allows
  * at PK_KEY_MIN_RATIO; such a keyboard is centred horizontally.
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
                float x, float y, float w, float h )
{
	float ww, bw, kh;
	int n, wi = 0, whites = 0;

	for ( n = range->first; n <= range->last; n++ )
	{
		whites += !pk_is_black( n );
	}
	ww = w / (float)whites;
	if ( ww * PK_KEY_MIN_RATIO > h )
	{
		ww = h / PK_KEY_MIN_RATIO;
	}
	bw = ww * PK_BLACK_WIDTH;
	kh = ww * PK_KEY_RATIO;
	if ( kh > h )
	{
		kh = h;
	}

	SDL_memset( kb, 0, sizeof *kb );
	x += ( w - ww * (float)whites ) * 0.5f;
	kb->area.x = x;
	kb->area.y = y;
	kb->area.w = ww * (float)whites;
	kb->area.h = kh;
	kb->first = range->first;
	kb->last = range->last;
	kb->white_w = ww;
	kb->black_h = kh * PK_BLACK_LEN;

	for ( n = kb->first; n <= kb->last; n++ )
	{
		if ( pk_is_black( n ) )
		{
			kb->key[n].black = 1;
			kb->key[n].w = bw;
			kb->key[n].x = x + ( (float)wi + pk_black_shift[n % 12] ) * ww
			               - bw * 0.5f;
		}
		else
		{
			kb->key[n].w = ww;
			kb->key[n].x = x + (float)wi * ww;
			wi++;
		}
	}
}

/**
  * @brief  Find the key under a point.
  *
  * @param  kb Keyboard layout.
  * @param  x Point x.
  * @param  y Point y.
  * @param  velocity Receives the velocity 1..127, may be NULL.
  *
  * @retval The MIDI note, or -1 if the point misses the keyboard.
  */
int pk_hit( const pk_keyboard *kb, float x, float y, int *velocity )
{
	float len, t;
	int n, found = -1, v;

	if ( x < kb->area.x || x >= kb->area.x + kb->area.w ||
	     y < kb->area.y || y >= kb->area.y + kb->area.h )
	{
		return -1;
	}
	if ( y < kb->area.y + kb->black_h )
	{
		for ( n = kb->first; n <= kb->last && found < 0; n++ )
		{
			if ( kb->key[n].black && x >= kb->key[n].x &&
			     x < kb->key[n].x + kb->key[n].w )
			{
				found = n;
			}
		}
	}
	for ( n = kb->first; n <= kb->last && found < 0; n++ )
	{
		if ( !kb->key[n].black && x >= kb->key[n].x &&
		     x < kb->key[n].x + kb->key[n].w )
		{
			found = n;
		}
	}
	if ( found < 0 )
	{
		return -1;
	}

	if ( velocity )
	{
		len = kb->key[found].black ? kb->black_h : kb->area.h;
		t = ( y - kb->area.y ) / len;
		v = (int)( 24.0f + t * 110.0f );
		*velocity = v < 1 ? 1 : ( v > 127 ? 127 : v );
	}
	return found;
}

/**
  * @brief  Draw the keyboard.
  *
  * @param  ren Renderer, with blend mode set to SDL_BLENDMODE_BLEND.
  * @param  kb Keyboard layout.
  * @param  down Velocity of each sounding note, 0 for keys that are up.
  * @param  latch Non-zero for notes held by a latch, may be NULL.
  *
  * @retval None.
  */
void pk_draw( SDL_Renderer *ren, const pk_keyboard *kb,
              const uint8_t down[128], const uint8_t *latch )
{
	const SDL_FRect *a = &kb->area;
	const pk_key *k;
	char label[8];
	SDL_FRect r;
	const SDL_Color *hot;
	SDL_Color top, bot, ltop, lbot, lab;
	float gap, rad, lip, s, side, face_h, sh;
	int n, ls;

	gap = kb->white_w > 14.0f ? 1.0f : 0.5f;
	rad = kb->white_w * 0.14f;

	// Gaps between white keys show this through.
	SDL_SetRenderDrawColor( ren, 18, 16, 15, 255 );
	SDL_RenderFillRectF( ren, a );

	for ( n = kb->first; n <= kb->last; n++ )
	{
		k = &kb->key[n];
		if ( k->black )
		{
			continue;
		}
		s = down[n] ? 0.55f + 0.45f * (float)down[n] / 127.0f : 0.0f;
		hot = pk_hot[latch && latch[n] ? 1 : 0];
		top  = mix( rgba( 232, 228, 219, 255 ), hot[0], s );
		bot  = mix( rgba( 255, 254, 251, 255 ), hot[1], s );
		ltop = mix( rgba( 214, 210, 201, 255 ), hot[2], s );
		lbot = mix( rgba( 168, 163, 154, 255 ), hot[3], s );
		lip = a->h * ( down[n] ? 0.014f : 0.04f );

		fill_key_shape( ren, k->x + gap, a->y, k->w - 2.0f * gap, a->h, rad,
		                ltop, lbot );
		fill_key_shape( ren, k->x + gap, a->y, k->w - 2.0f * gap,
		                a->h - lip, rad * 0.6f, top, bot );

		if ( n % 12 == 0 )
		{
			ls = kb->white_w >= 26.0f ? 2 : 1;
			pk_note_name( n, label, sizeof label );
			lab = ( n == 60 ) ? rgba( 214, 70, 60, 255 )
			                  : rgba( 150, 144, 134, 255 );
			fnt_draw( ren, k->x + ( k->w - (float)fnt_width( ls, label ) ) * 0.5f,
			          a->y + a->h - lip - (float)( ( FNT_CELL_H + 3 ) * ls ),
			          ls, label, lab );
		}
	}

	// Shadow cast by the fallboard over the back of the keys.
	sh = a->h * 0.07f;
	fill_key_shape( ren, a->x, a->y, a->w, sh, 0.0f,
	                rgba( 0, 0, 0, 120 ), rgba( 0, 0, 0, 0 ) );

	for ( n = kb->first; n <= kb->last; n++ )
	{
		k = &kb->key[n];
		if ( !k->black )
		{
			continue;
		}
		s = down[n] ? 0.55f + 0.45f * (float)down[n] / 127.0f : 0.0f;
		hot = pk_hot[latch && latch[n] ? 1 : 0];
		rad = k->w * 0.14f;
		side = k->w * 0.11f;
		face_h = kb->black_h * ( down[n] ? 0.95f : 0.87f );

		fill_key_shape( ren, k->x + k->w * 0.2f, a->y,
		                k->w, kb->black_h + a->h * 0.03f, rad,
		                rgba( 0, 0, 0, 110 ), rgba( 0, 0, 0, 30 ) );
		fill_key_shape( ren, k->x, a->y, k->w, kb->black_h, rad,
		                mix( rgba( 6, 6, 7, 255 ), hot[4], s ),
		                mix( rgba( 52, 52, 56, 255 ), hot[5], s ) );
		fill_key_shape( ren, k->x + side, a->y, k->w - 2.0f * side, face_h,
		                rad * 0.7f,
		                mix( rgba( 16, 16, 18, 255 ), hot[6], s ),
		                mix( rgba( 44, 44, 48, 255 ), hot[7], s ) );

		// Narrow gloss streak down the face.
		r.x = k->x + side + k->w * 0.08f;
		r.y = a->y + kb->black_h * 0.08f;
		r.w = k->w * 0.07f;
		r.h = face_h * 0.8f;
		fill_key_shape( ren, r.x, r.y, r.w, r.h, r.w * 0.5f,
		                rgba( 255, 255, 255, 0 ), rgba( 255, 255, 255, 40 ) );
	}

	// Red felt strip along the back, as on a real piano.
	fill_key_shape( ren, a->x, a->y - 5.0f, a->w, 5.0f, 0.0f,
	                rgba( 128, 22, 30, 255 ), rgba( 82, 12, 18, 255 ) );
}
