//  Description:
//
//		font.h -- tiny built-in 5x7 bitmap font for SDL renderers.
//
//		Built in so the app needs no font file and no SDL_ttf. Covers ASCII
//		32..95; lower case is drawn as upper case, anything else as '?'.
//		Each glyph cell is 6x8 pixels before scaling (one column and one row
//		of spacing).
//

#ifndef FONT_H
#define FONT_H

#include <SDL.h>

#define FNT_CELL_W 6
#define FNT_CELL_H 8

/**
  * @brief  Draw a string with the current renderer blend mode.
  *
  * @param  ren Renderer.
  * @param  x Left edge in pixels.
  * @param  y Top edge in pixels.
  * @param  scale Pixel size of one font dot, 1 or more.
  * @param  text NUL terminated string.
  * @param  c Colour.
  *
  * @retval None.
  */
void fnt_draw( SDL_Renderer *ren, float x, float y, int scale,
               const char *text, SDL_Color c );

/**
  * @brief  Width of a string in pixels, without the trailing spacing column.
  *
  * @param  scale Pixel size of one font dot.
  * @param  text NUL terminated string.
  *
  * @retval Width in pixels, 0 for an empty string.
  */
int fnt_width( int scale, const char *text );

#endif
