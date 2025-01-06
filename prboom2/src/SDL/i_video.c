/* Emacs style mode select   -*- C -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2006 by
 *  Jess Haas, Nicolas Kalkhof, Colin Phipps, Florian Schulze
 *  Copyright 2005, 2006 by
 *  Florian Schulze, Colin Phipps, Neil Stevens, Andrey Budko
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version 2
 *  of the License, or (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *  02111-1307, USA.
 *
 * DESCRIPTION:
 *  DOOM graphics stuff for SDL
 *
 *-----------------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif // _WIN32

#include <stdlib.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "SDL.h"
//e6y
#ifdef _WIN32
#include <SDL_syswm.h>
#endif

#include "doomstat.h"
#include "doomdef.h"
#include "doomtype.h"
#include "v_video.h"
#include "r_draw.h"
#include "r_things.h"
#include "r_plane.h"
#include "r_main.h"
#include "f_wipe.h"
#include "d_main.h"
#include "d_event.h"
#include "d_deh.h"
#include "i_video.h"
#include "i_capture.h"
#include "z_zone.h"
#include "s_sound.h"
#include "sounds.h"
#include "w_wad.h"
#include "st_stuff.h"
#include "am_map.h"
#include "g_game.h"
#include "lprintf.h"
#include "i_system.h"
#include "gl_struct.h"

#include "e6y.h"//e6y
#include "i_main.h"

#include "dsda/args.h"
#include "dsda/configuration.h"
#include "dsda/game_controller.h"
#include "dsda/palette.h"
#include "dsda/pause.h"
#include "dsda/settings.h"
#include "dsda/time.h"
#include "dsda/gl/render_scale.h"

dboolean window_focused;

// Window resize state.
static void ApplyWindowResize(SDL_Event *resize_event);

static void ActivateMouse(void);
static void DeactivateMouse(void);
//static int AccelerateMouse(int val);
static void I_ReadMouse(void);
static dboolean MouseShouldBeGrabbed();
static void UpdateFocus(void);

extern const int gl_colorbuffer_bits;
extern const int gl_depthbuffer_bits;

extern void M_QuitDOOM(int choice);
int desired_fullscreen;
int exclusive_fullscreen;
SDL_Surface *screen;
SDL_Surface *buffer;
SDL_Window *sdl_window;
SDL_Renderer *sdl_renderer;
unsigned int windowid = 0;
SDL_Rect src_rect = { 0, 0, 0, 0 };

////////////////////////////////////////////////////////////////////////////
// Input code
int             leds_always_off = 0; // Expected by m_misc, not relevant

// Mouse handling
static dboolean mouse_enabled; // usemouse, but can be overriden by -nomouse

/////////////////////////////////////////////////////////////////////////////////
// Keyboard handling

// Vanilla keymap taken from chocolate-doom and adjusted for prboom-plus
#define SCANCODE_TO_KEYS_ARRAY {                                          \
  0,   0,   0,   0,   'a',                                  /* 0-9 */     \
  'b', 'c', 'd', 'e', 'f',                                                \
  'g', 'h', 'i', 'j', 'k',                                  /* 10-19 */   \
  'l', 'm', 'n', 'o', 'p',                                                \
  'q', 'r', 's', 't', 'u',                                  /* 20-29 */   \
  'v', 'w', 'x', 'y', 'z',                                                \
  '1', '2', '3', '4', '5',                                  /* 30-39 */   \
  '6', '7', '8', '9', '0',                                                \
  KEYD_ENTER, KEYD_ESCAPE, KEYD_BACKSPACE, KEYD_TAB, ' ',   /* 40-49 */   \
  KEYD_MINUS, KEYD_EQUALS, '[', ']', '\\',                                \
  '\\', ';', '\'', '`', ',',                                /* 50-59 */   \
  '.', '/', KEYD_CAPSLOCK, KEYD_F1, KEYD_F2,                              \
  KEYD_F3, KEYD_F4, KEYD_F5, KEYD_F6, KEYD_F7,              /* 60-69 */   \
  KEYD_F8, KEYD_F9, KEYD_F10, KEYD_F11, KEYD_F12, KEYD_PRINTSC,           \
  KEYD_SCROLLLOCK, KEYD_PAUSE, KEYD_INSERT, KEYD_HOME,      /* 70-79 */   \
  KEYD_PAGEUP, KEYD_DEL, KEYD_END, KEYD_PAGEDOWN, KEYD_RIGHTARROW,        \
  KEYD_LEFTARROW, KEYD_DOWNARROW, KEYD_UPARROW,             /* 80-89 */   \
  KEYD_NUMLOCK, KEYD_KEYPADDIVIDE,                                        \
  KEYD_KEYPADMULTIPLY, KEYD_KEYPADMINUS, KEYD_KEYPADPLUS,                 \
  KEYD_KEYPADENTER, KEYD_KEYPAD1, KEYD_KEYPAD2, KEYD_KEYPAD3,             \
  KEYD_KEYPAD4, KEYD_KEYPAD5, KEYD_KEYPAD6,                 /* 90-99 */   \
  KEYD_KEYPAD7, KEYD_KEYPAD8, KEYD_KEYPAD9, KEYD_KEYPAD0,                 \
  KEYD_KEYPADPERIOD, 0, 0, 0, KEYD_EQUALS                   /* 100-103 */ \
}

// Map keys like vanilla doom
static int VanillaTranslateKey(SDL_Keysym* key)
{
  static const int scancode_map[] = SCANCODE_TO_KEYS_ARRAY;
  int rc = 0, sc = key->scancode;

  if (sc > 3 && sc < sizeof(scancode_map) / sizeof(scancode_map[0]))
    rc = scancode_map[sc];

  // Key is mapped..
  if (rc)
    return rc;

  switch (sc) { // Code (Ctrl/Shift/Alt) from scancode.
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
      return KEYD_RSHIFT;

    case SDL_SCANCODE_LCTRL:
    case SDL_SCANCODE_RCTRL:
      return KEYD_RCTRL;

    case SDL_SCANCODE_LALT:
    case SDL_SCANCODE_RALT:
    case SDL_SCANCODE_LGUI:
    case SDL_SCANCODE_RGUI:
      return KEYD_RALT;

    // Default to the symbolic key (outside of vanilla keys)
    default:
      return key->sym;
  }
}

//
//  Translates the key currently in key
//

static int I_TranslateKey(SDL_Keysym* key)
{
  return 0;
}

dboolean I_WindowFocused(void)
{
  return window_focused;
}

/////////////////////////////////////////////////////////////////////////////////
// Main input code

/* cph - pulled out common button code logic */
//e6y static
int I_SDLtoDoomMouseState(Uint32 buttonstate)
{
  return 0     ;
}

static void I_GetEvent(void)
{
}

//
// I_StartTic
//

void I_StartTic (void)
{
  I_GetEvent();

  if (dsda_AllowMouse())
    I_ReadMouse();

  if (dsda_AllowGameController())
    dsda_PollGameController();
}

//
// I_StartFrame
//
void I_StartFrame (void)
{
}

static void I_FlushMousePosition(void)
{
}

void I_InitMouse(void)
{
}

//
// I_InitInputs
//

static void I_InitInputs(void)
{
  AccelChanging();
  I_InitMouse();
  dsda_InitGameController();
}

///////////////////////////////////////////////////////////
// Palette stuff.
//
static void I_UploadNewPalette(int pal, int force)
{
  // This is used to replace the current 256 colour cmap with a new one
  // Used by 256 colour PseudoColor modes

  static int cachedgamma;
  static size_t num_pals;
  dsda_playpal_t* playpal_data;

  if (V_IsOpenGLMode())
    return;

  playpal_data = dsda_PlayPalData();

  if ((playpal_data->colours == NULL) || (cachedgamma != usegamma) || force) {
    int pplump;
    int gtlump;
    register const byte * palette;
    register const byte * gtable;
    register int i;

    pplump = W_GetNumForName(playpal_data->lump_name);
    gtlump = W_CheckNumForName2("GAMMATBL", ns_prboom);
    palette = (const byte*) W_LumpByNum(pplump);
    gtable = (const byte*) W_LumpByNum(gtlump) + 256 * (cachedgamma = usegamma);

    num_pals = W_LumpLength(pplump) / (3 * 256);
    num_pals *= 256;

    if (!playpal_data->colours) {
      // First call - allocate and prepare colour array
      playpal_data->colours =
        (SDL_Color*) Z_Malloc(sizeof(*playpal_data->colours) * num_pals);
    }

    // set the colormap entries
    for (i = 0; (size_t) i < num_pals; i++) {
      playpal_data->colours[i].r = gtable[palette[0]];
      playpal_data->colours[i].g = gtable[palette[1]];
      playpal_data->colours[i].b = gtable[palette[2]];
      palette += 3;
    }

    num_pals /= 256;
  }

}

//////////////////////////////////////////////////////////////////////////////
// Graphics API

void I_ShutdownGraphics(void)
{
}

static dboolean queue_frame_capture;
static dboolean queue_screenshot;

void I_QueueFrameCapture(void)
{
  queue_frame_capture = true;
}

void I_QueueScreenshot(void)
{
  queue_screenshot = true;
}

void I_HandleCapture(void)
{
  if (queue_frame_capture)
  {
    I_CaptureFrame();
    queue_frame_capture = false;
  }

  if (queue_screenshot)
  {
    M_ScreenShot();
    queue_screenshot = false;
  }
}

//
// I_FinishUpdate
//
static int newpal = 0;
#define NO_PALETTE_CHANGE 1000

void I_FinishUpdate (void)
{
  /* Update the display buffer (flipping video pages if supported)
   * If we need to change palette, that implicitely does a flip */
  if (newpal != NO_PALETTE_CHANGE) {
    I_UploadNewPalette(newpal, false);
    newpal = NO_PALETTE_CHANGE;
  }
}

//
// I_ScreenShot - moved to i_sshot.c
//

//
// I_SetPalette
//
void I_SetPalette (int pal)
{
  newpal = pal;
}

// I_PreInitGraphics

static void I_ShutdownSDL(void)
{
  if (screen) SDL_FreeSurface(screen);
  if (buffer) SDL_FreeSurface(buffer);
  if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
  if (sdl_window) SDL_DestroyWindow(sdl_window);

  SDL_Quit();
  return;
}

void I_PreInitGraphics(void)
{
  int p;

  // Initialize SDL
  unsigned int flags = 0;
  if (!(dsda_Flag(dsda_arg_nodraw) && dsda_Flag(dsda_arg_nosound)))
    flags = SDL_INIT_VIDEO;
#ifdef PRBOOM_DEBUG
  flags |= SDL_INIT_NOPARACHUTE;
#endif

  p = SDL_Init(flags);
  if (p < 0)
  {
    I_Error("Could not initialize SDL [%s]", SDL_GetError());
  }

  I_AtExit(I_ShutdownSDL, true, "I_ShutdownSDL", exit_priority_normal);
}

// e6y: resolution limitation is removed
void I_InitBuffersRes(void)
{
  R_InitMeltRes();
  R_InitSpritesRes();
  R_InitBuffersRes();
  R_InitPlanesRes();
  R_InitVisplanesRes();
}

#define MAX_RESOLUTIONS_COUNT 128
const char *screen_resolutions_list[MAX_RESOLUTIONS_COUNT] = {NULL};

//
// I_GetScreenResolution
// Get current resolution from the config variable (WIDTHxHEIGHT format)
// 640x480 if screen_resolution variable has wrong data
//
void I_GetScreenResolution(void)
{
  int width, height;
  const char *screen_resolution;

  desired_screenwidth = 640;
  desired_screenheight = 480;

  screen_resolution = dsda_StringConfig(dsda_config_screen_resolution);

  if (screen_resolution)
  {
    if (sscanf(screen_resolution, "%dx%d", &width, &height) == 2)
    {
      desired_screenwidth = width;
      desired_screenheight = height;
    }
  }
}

// make sure the canonical resolutions are always available
static const struct {
  const int w, h;
} canonicals[] = {
  { 640, 480}, // Doom 95
  { 320, 240}, // Doom 95
  {1120, 400}, // 21:9
  { 854, 400}, // 16:9
  { 768, 400}, // 16:10
  { 640, 400}, // MBF
  { 560, 200}, // 21:9
  { 426, 200}, // 16:9
  { 384, 200}, // 16:10
  { 320, 200}, // Vanilla Doom
};
static const int num_canonicals = sizeof(canonicals)/sizeof(*canonicals);

// [FG] sort resolutions by width first and height second
static int cmp_resolutions (const void *a, const void *b)
{
    const char *const *sa = (const char *const *) a;
    const char *const *sb = (const char *const *) b;

    int wa, wb, ha, hb;

    if (sscanf(*sa, "%dx%d", &wa, &ha) != 2) wa = ha = 0;
    if (sscanf(*sb, "%dx%d", &wb, &hb) != 2) wb = hb = 0;

    return (wa == wb) ? ha - hb : wa - wb;
}

static void I_AppendResolution(SDL_DisplayMode *mode, int *current_resolution_index, int *list_size)
{
  int i;
  char mode_name[256];


  snprintf(mode_name, sizeof(mode_name), "%dx%d", mode->w, mode->h);

  for(i = 0; i < *list_size; i++)
    if (!strcmp(mode_name, screen_resolutions_list[i]))
      return;

  screen_resolutions_list[*list_size] = Z_Strdup(mode_name);

  if (mode->w == desired_screenwidth && mode->h == desired_screenheight)
    *current_resolution_index = *list_size;

  (*list_size)++;
}

static void I_AppendCustomResolution(int *current_resolution_index, int *list_size)
{
  const char *custom_resolution;

  custom_resolution = dsda_StringConfig(dsda_config_custom_resolution);

  if (strlen(custom_resolution))
  {
    SDL_DisplayMode mode;

    if (sscanf(custom_resolution, "%4dx%4d", &mode.w, &mode.h) == 2)
    {
      I_AppendResolution(&mode, current_resolution_index, list_size);
    }
  }
}

//
// I_FillScreenResolutionsList
// Get all the supported screen resolutions
// and fill the list with them
//
static void I_FillScreenResolutionsList(void)
{
  int display_index = 0;
  SDL_DisplayMode mode;
  int i, list_size, current_resolution_index, count;
  char desired_resolution[256];

  // do it only once
  if (screen_resolutions_list[0])
  {
    return;
  }

  if (desired_screenwidth == 0 || desired_screenheight == 0)
  {
    I_GetScreenResolution();
  }

  // Don't call SDL_ListModes if SDL has not been initialized
  count = 0;
  if (!nodrawers)
    count = SDL_GetNumDisplayModes(display_index);

  list_size = 0;
  current_resolution_index = -1;

  // on success, SDL_GetNumDisplayModes() always returns at least 1
  if (count > 0)
  {
    // -2 for the desired resolution and for NULL
    count = MIN(count, MAX_RESOLUTIONS_COUNT - 2 - num_canonicals);

    for(i = count - 1 + num_canonicals; i >= 0; i--)
    {
      // make sure the canonical resolutions are always available
      if (i > count - 1)
      {
        // no hard-coded resolutions for mode-changing fullscreen
        if (exclusive_fullscreen)
          continue; 

        mode.w = canonicals[i - count].w;
        mode.h = canonicals[i - count].h;
      }
      else
      {
        SDL_GetDisplayMode(display_index, i, &mode);
      }

      I_AppendResolution(&mode, &current_resolution_index, &list_size);
    }

    I_AppendCustomResolution(&current_resolution_index, &list_size);

    screen_resolutions_list[list_size] = NULL;
  }

  snprintf(desired_resolution, sizeof(desired_resolution), "%dx%d", desired_screenwidth, desired_screenheight);

  // [FG] if the desired resolution not in the list, append it
  if (current_resolution_index == -1)
  {
    screen_resolutions_list[list_size] = Z_Strdup(desired_resolution);
    list_size++;
  }

  // [FG] sort the list
  SDL_qsort(screen_resolutions_list, list_size, sizeof(*screen_resolutions_list), cmp_resolutions);

  // [FG] find the desired resolution again
  for (i = 0; i < list_size; i++)
  {
    if (!strcmp(desired_resolution, screen_resolutions_list[i]))
    {
      current_resolution_index = i;
      break;
    }
  }

  screen_resolutions_list[list_size] = NULL;
  // This code is inside of the onUpdate for screen resolution, so it must avoid recursion
  {
    const char* dsda_HackStringConfig(dsda_config_identifier_t id, const char* value, dboolean persist);

    dsda_HackStringConfig(dsda_config_screen_resolution,
                          screen_resolutions_list[current_resolution_index], false);
  }
}

// e6y
// Function for trying to set the closest supported resolution if the requested mode can't be set correctly.
// For example dsda-doom.exe -geom 1025x768 -nowindow will set 1024x768.
// It should be used only for fullscreen modes.
static void I_ClosestResolution (int *width, int *height)
{
  int display_index = 0;
  int twidth, theight;
  int cwidth = 0, cheight = 0;
  int i, count;
  unsigned int closest = UINT_MAX;
  unsigned int dist;

  if (!SDL_WasInit(SDL_INIT_VIDEO))
    return;

  count = SDL_GetNumDisplayModes(display_index);

  if (count > 0)
  {
    for(i=0; i<count; ++i)
    {
      SDL_DisplayMode mode;
      SDL_GetDisplayMode(display_index, i, &mode);

      twidth = mode.w;
      theight = mode.h;

      if (twidth == *width && theight == *height)
        return;

      //if (iteration == 0 && (twidth < *width || theight < *height))
      //  continue;

      dist = (twidth - *width) * (twidth - *width) +
             (theight - *height) * (theight - *height);

      if (dist < closest)
      {
        closest = dist;
        cwidth = twidth;
        cheight = theight;
      }
    }
    if (closest != 4294967295u)
    {
      *width = cwidth;
      *height = cheight;
      return;
    }
  }
}

// e6y
// It is a simple test of CPU cache misses.
unsigned int I_TestCPUCacheMisses(int width, int height, unsigned int mintime)
{
  int i, k;
  char *s, *d, *ps, *pd;
  unsigned int tickStart;

  s = (char*)Z_Malloc(width * height);
  d = (char*)Z_Malloc(width * height);

  tickStart = SDL_GetTicks();
  k = 0;
  do
  {
    ps = s;
    pd = d;
    for(i = 0; i < height; i++)
    {
      pd[0] = ps[0];
      pd += width;
      ps += width;
    }
    k++;
  }
  while (SDL_GetTicks() - tickStart < mintime);

  Z_Free(d);
  Z_Free(s);

  return k;
}

// CPhipps -
// I_CalculateRes
// Calculates the screen resolution, possibly using the supplied guide
void I_CalculateRes(int width, int height)
{
  if (desired_fullscreen && exclusive_fullscreen)
  {
    I_ClosestResolution(&width, &height);
  }

#ifdef __ENABLE_OPENGL_
  if (V_IsOpenGLMode()) {
    SCREENWIDTH = width;
    SCREENHEIGHT = height;
    SCREENPITCH = SCREENWIDTH;
  } else
  #endif
   {
    unsigned int count1, count2;
    int pitch1, pitch2;

    SCREENWIDTH = width;//(width+15) & ~15;
    SCREENHEIGHT = height;

    // e6y
    // Trying to optimise screen pitch for reducing of CPU cache misses.
    // It is extremally important for wiping in software.
    // I have ~20x improvement in speed with using 1056 instead of 1024 on Pentium4
    // and only ~10% for Core2Duo
    if (nodrawers)
    {
      SCREENPITCH = ((width + 15) & ~15) + 32;
    }
    else
    {
      unsigned int mintime = 100;
      int w = (width+15) & ~15;
      pitch1 = w;
      pitch2 = w + 32;

      count1 = I_TestCPUCacheMisses(pitch1, SCREENHEIGHT, mintime);
      count2 = I_TestCPUCacheMisses(pitch2, SCREENHEIGHT, mintime);

      lprintf(LO_DEBUG, "I_CalculateRes: trying to optimize screen pitch\n");
      lprintf(LO_DEBUG, " test case for pitch=%d is processed %d times for %d msec\n", pitch1, count1, mintime);
      lprintf(LO_DEBUG, " test case for pitch=%d is processed %d times for %d msec\n", pitch2, count2, mintime);

      SCREENPITCH = (count2 > count1 ? pitch2 : pitch1);

      lprintf(LO_DEBUG, " optimized screen pitch is %d\n", SCREENPITCH);
    }
  }
}

static video_mode_t I_GetModeFromString(const char *modestr)
{
  video_mode_t mode;

  if (!stricmp(modestr,"gl")) {
    mode = VID_MODEGL;
  } else if (!stricmp(modestr,"OpenGL")) {
    mode = VID_MODEGL;
  } else {
    mode = VID_MODESW;
  }

  return mode;
}

static video_mode_t I_DesiredVideoMode(void) {
  dsda_arg_t *arg;
  video_mode_t mode;

  arg = dsda_Arg(dsda_arg_vidmode);
  if (arg->found)
    mode = I_GetModeFromString(arg->value.v_string);
  else
    mode = I_GetModeFromString(dsda_StringConfig(dsda_config_videomode));

  return mode;
}

// CPhipps -
// I_InitScreenResolution
// Sets the screen resolution
void I_InitScreenResolution(void)
{
  int i, w, h;
  char c, x;
  dsda_arg_t *arg;
  video_mode_t mode;
  int init = (sdl_window == NULL);

  I_GetScreenResolution();

  desired_fullscreen = dsda_IntConfig(dsda_config_use_fullscreen);

  if (dsda_Flag(dsda_arg_fullscreen))
    desired_fullscreen = 1;

  if (dsda_Flag(dsda_arg_window))
    desired_fullscreen = 0;

  if (init)
  {
    //e6y: ability to change screen resolution from GUI
    I_FillScreenResolutionsList();

    // Video stuff
    arg = dsda_Arg(dsda_arg_width);
    if (arg->found)
      desired_screenwidth = arg->value.v_int;

    arg = dsda_Arg(dsda_arg_height);
    if (arg->found)
      desired_screenheight = arg->value.v_int;

    // e6y
    // change the screen size for the current session only
    // syntax: -geom WidthxHeight[w|f]
    // examples: -geom 320x200f, -geom 640x480w, -geom 1024x768
    w = desired_screenwidth;
    h = desired_screenheight;

    arg = dsda_Arg(dsda_arg_geometry);
    if (arg->found)
    {
      int count = sscanf(arg->value.v_string, "%d%c%d%c", &w, &x, &h, &c);

      // at least width and height must be specified
      // restoring original values if not
      if (count < 3 || tolower(x) != 'x')
      {
        w = desired_screenwidth;
        h = desired_screenheight;
      }
      else
      {
        if (count >= 4)
        {
          if (tolower(c) == 'w')
            desired_fullscreen = 0;
          if (tolower(c) == 'f')
            desired_fullscreen = 1;
        }
      }
    }
  }
  else
  {
    w = desired_screenwidth;
    h = desired_screenheight;
  }

  mode = I_DesiredVideoMode();

  V_InitMode(mode);

  I_CalculateRes(w, h);
  V_FreeScreens();

  // set first three to standard values
  for (i=0; i<3; i++) {
    screens[i].width = SCREENWIDTH;
    screens[i].height = SCREENHEIGHT;
    screens[i].pitch = SCREENPITCH;
  }

  // statusbar
  screens[4].width = SCREENWIDTH;
  screens[4].height = SCREENHEIGHT;
  screens[4].pitch = SCREENPITCH;

  I_InitBuffersRes();

  lprintf(LO_DEBUG, "I_InitScreenResolution: Using resolution %dx%d\n", SCREENWIDTH, SCREENHEIGHT);
}

//
// Set the window caption
//

void I_SetWindowCaption(void)
{
  SDL_SetWindowTitle(NULL, PACKAGE_NAME " " PACKAGE_VERSION);
}

//
// Set the application icon
//

#include "icon.c"

void I_SetWindowIcon(void)
{
  static SDL_Surface *surface = NULL;

  // do it only once, because of crash in SDL_InitVideoMode in SDL 1.3
  if (!surface)
  {
    surface = SDL_CreateRGBSurfaceFrom(icon_data,
      icon_w, icon_h, 32, icon_w * 4,
      0xff << 0, 0xff << 8, 0xff << 16, 0xff << 24);
  }

  if (surface)
  {
    SDL_SetWindowIcon(NULL, surface);
  }
}

void I_InitGraphics(void)
{
  static int    firsttime=1;

  if (firsttime)
  {
    firsttime = 0;

    I_AtExit(I_ShutdownGraphics, true, "I_ShutdownGraphics", exit_priority_normal);
    lprintf(LO_DEBUG, "I_InitGraphics: %dx%d\n", SCREENWIDTH, SCREENHEIGHT);

    /* Set the video mode */
    I_UpdateVideoMode();

    //e6y: setup the window title
    I_SetWindowCaption();

    //e6y: set the application icon
    I_SetWindowIcon();

    /* Initialize the input system */
    I_InitInputs();

    //e6y: new mouse code
    UpdateFocus();
    UpdateGrab();
  }
}

void I_UpdateVideoMode(void)
{
  int init_flags = SDL_WINDOW_ALLOW_HIGHDPI;
  int screen_multiply;
  int actualheight;
  int render_vsync;
  // int integer_scaling;
  // const char *sdl_video_window_pos;
  const dboolean novsync = dsda_Flag(dsda_arg_timedemo) ||
                           dsda_Flag(dsda_arg_fastdemo);

  exclusive_fullscreen = dsda_IntConfig(dsda_config_exclusive_fullscreen) &&
                         I_DesiredVideoMode() == VID_MODESW;
  render_vsync = dsda_IntConfig(dsda_config_render_vsync) && !novsync;
  // sdl_video_window_pos = dsda_StringConfig(dsda_config_sdl_video_window_pos);
  screen_multiply = dsda_IntConfig(dsda_config_render_screen_multiply);
  // integer_scaling = dsda_IntConfig(dsda_config_integer_scaling);

  if(sdl_window)
  {
    // video capturing cannot be continued with new screen settings
    I_CaptureFinish();
  
  #ifdef __ENABLE_OPENGL_
    if (V_IsOpenGLMode())
    {
      gld_CleanMemory();
      gld_CleanStaticMemory();
    }
  #endif

    I_InitScreenResolution();

    if (screen) SDL_FreeSurface(screen);
    if (buffer) SDL_FreeSurface(buffer);
    if (sdl_renderer) SDL_DestroyRenderer(sdl_renderer);
    SDL_DestroyWindow(sdl_window);

    sdl_renderer = NULL;
    sdl_window = NULL;
    screen = NULL;
    buffer = NULL;
  }

  // Initialize SDL with this graphics mode
  #ifdef __ENABLE_OPENGL_
  if (V_IsOpenGLMode()) {
    init_flags |= SDL_WINDOW_OPENGL;
  }
  #endif
  
  // [FG] aspect ratio correction for the canonical video modes
  if (SCREENHEIGHT == 200 || SCREENHEIGHT == 400)
  {
    actualheight = 6*SCREENHEIGHT/5;
  }
  else
  {
    actualheight = SCREENHEIGHT;
  }

  if (desired_fullscreen)
  {
    if (exclusive_fullscreen)
      init_flags |= SDL_WINDOW_FULLSCREEN;
    else
      init_flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  }
  else
  {
    init_flags |= SDL_WINDOW_RESIZABLE;

    // [FG] make sure initial window size is always >= 640x480
    while (screen_multiply*SCREENWIDTH < 640 || screen_multiply*actualheight < 480)
    {
      screen_multiply++;
    }
  }

    screen = SDL_CreateRGBSurface(0, SCREENWIDTH, SCREENHEIGHT, 8, 0, 0, 0, 0);
    buffer = SDL_CreateRGBSurface(0, SCREENWIDTH, SCREENHEIGHT, 32, 0, 0, 0, 0);
    SDL_FillRect(buffer, NULL, 0);
  windowid = SDL_GetWindowID(sdl_window);

  if (V_IsSoftwareMode())
  {
    lprintf(LO_DEBUG, "I_UpdateVideoMode: 0x%x, %s, %s\n", init_flags, screen && screen->pixels ? "SDL buffer" : "own buffer", screen && SDL_MUSTLOCK(screen) ? "lock-and-copy": "direct access");

    // Get the info needed to render to the display
    if (!SDL_MUSTLOCK(screen))
    {
      screens[0].not_on_heap = true;
      screens[0].data = (unsigned char *) (screen->pixels);
      screens[0].pitch = screen->pitch;
    }
    else
    {
      screens[0].not_on_heap = false;
    }

    V_AllocScreens();

    R_InitBuffer(SCREENWIDTH, SCREENHEIGHT);
  }

  // e6y: wide-res
  // Need some initialisations before level precache
  R_ExecuteSetViewSize();

  V_SetPalette(0);
  I_UploadNewPalette(0, true);

  ST_SetResolution();
  AM_SetResolution();

  src_rect.w = SCREENWIDTH;
  src_rect.h = SCREENHEIGHT;
}

static void ActivateMouse(void)
{
  SDL_SetRelativeMouseMode(SDL_TRUE);
  SDL_GetRelativeMouseState(NULL, NULL);
}

static void DeactivateMouse(void)
{
  SDL_SetRelativeMouseMode(SDL_FALSE);
}

// Interpolates mouse input to mitigate stuttering
static void CorrectMouseStutter(int *x, int *y)
{
  static int x_remainder_old, y_remainder_old;
  int x_remainder, y_remainder;
  fixed_t fractic, correction_factor;

  if (!dsda_IntConfig(dsda_config_mouse_stutter_correction))
  {
    return;
  }

  fractic = dsda_TickElapsedTime();

  *x += x_remainder_old;
  *y += y_remainder_old;

  correction_factor = FixedDiv(fractic, fractic + 1000000 / TICRATE);

  x_remainder = FixedMul(*x, correction_factor);
  *x -= x_remainder;
  x_remainder_old = x_remainder;

  y_remainder = FixedMul(*y, correction_factor);
  *y -= y_remainder;
  y_remainder_old = y_remainder;
}

//
// Read the change in mouse state to generate mouse motion events
//
// This is to combine all mouse movement for a tic into one mouse
// motion event.
static void I_ReadMouse(void)
{
  if (!mouse_enabled)
    return;

  if (window_focused)
  {
    int x, y;

    SDL_GetRelativeMouseState(&x, &y);
    CorrectMouseStutter(&x, &y);

    if (x != 0 || y != 0)
    {
      event_t event;
      event.type = ev_mousemotion;
      event.data1.i = x;
      event.data2.i = -y;

      D_PostEvent(&event);
    }
  }
}

static dboolean MouseShouldBeGrabbed()
{
  // never grab the mouse when in screensaver mode

  //if (screensaver_mode)
  //    return false;

  // if the window doesnt have focus, never grab it
  if (!window_focused)
    return false;

  // always grab the mouse when full screen (dont want to
  // see the mouse pointer)
  if (desired_fullscreen)
    return true;

  // if we specify not to grab the mouse, never grab
  if (!mouse_enabled)
    return false;

  // always grab the mouse in camera mode when playing levels
  // and menu is not active
  if (walkcamera.type)
    return (demoplayback && gamestate == GS_LEVEL && !menuactive);

  // when menu is active or game is paused, release the mouse
  if (menuactive || dsda_Paused())
    return false;

  // only grab mouse when playing levels (but not demos)
  return !demoplayback;
}

// Update the value of window_focused when we get a focus event
//
// We try to make ourselves be well-behaved: the grab on the mouse
// is removed if we lose focus (such as a popup window appearing),
// and we dont move the mouse around if we aren't focused either.
static void UpdateFocus(void)
{
  Uint32 flags = 0;

  window_focused = false;
  if(sdl_window)
  {
    flags = SDL_GetWindowFlags(sdl_window);
    if ((flags & SDL_WINDOW_SHOWN) && !(flags & SDL_WINDOW_MINIMIZED) && (flags & SDL_WINDOW_INPUT_FOCUS))
    {
      window_focused = true;
    }
  }

  // e6y
  // Reuse of a current palette to avoid black screen at software fullscreen modes
  // after switching to OS and back
  if (desired_fullscreen && window_focused)
  {
    V_TouchPalette();
  }

  S_ResetVolume();
}

void UpdateGrab(void)
{
  static dboolean currently_grabbed = false;
  dboolean grab;

  grab = MouseShouldBeGrabbed();

  if (grab && !currently_grabbed)
  {
    ActivateMouse();
  }

  if (!grab && currently_grabbed)
  {
    DeactivateMouse();
  }

  currently_grabbed = grab;
}

static void ApplyWindowResize(SDL_Event *resize_event)
{
  if (!V_IsOpenGLMode() || !sdl_window)
    return;


#ifdef __ENABLE_OPENGL_
  dsda_GLGetSDLWindowSize(sdl_window);
  dsda_GLSetRenderViewportParams();
  #endif
}

/////////// Headless function

void* headlessGetVideoBuffer() { return screens[0].data; }
int headlessGetVideoPitch() { return screens[0].pitch; }
int headlessGetVideoWidth() { return screens[0].width; }
int headlessGetVideoHeight() { return screens[0].height; }
SDL_Surface* headlessGetVideoSurface() { return buffer; }