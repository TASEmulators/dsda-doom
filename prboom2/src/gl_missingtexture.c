/* Emacs style mode select   -*- C -*-
 *-----------------------------------------------------------------------------
 *
 *
 *  PrBoom: a Doom port merged with LxDoom and LSDLDoom
 *  based on BOOM, a modified and improved DOOM engine
 *  Copyright (C) 1999 by
 *  id Software, Chi Hoang, Lee Killough, Jim Flynn, Rand Phares, Ty Halderman
 *  Copyright (C) 1999-2000 by
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
 *
 *---------------------------------------------------------------------
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <assert.h>

#include "gl_opengl.h"

#include <SDL.h>
#ifdef HAVE_LIBSDL2_IMAGE
#include <SDL_image.h>
#endif
#include "doomstat.h"
#include "v_video.h"
#include "gl_intern.h"
#include "i_system.h"
#include "lprintf.h"
#include "i_video.h"
#include "hu_lib.h"
#include "hu_stuff.h"
#include "r_main.h"
#include "e6y.h"

typedef struct
{
  int count;        // size of the list with adjoining sectors
  int validcount;   // finding of the best sector in the group only once in tic
  int ceiling;      // this group is for ceilings or flats
  sector_t *sector; // sector with the 'best' height for the sectors in list
  sector_t **list;  // list of adjoining sectors
} fakegroup_t;

typedef struct
{
  sector_t *source; // The sector to receive a fake bleed-through flat
  sector_t *target; // The floor sector whose properties should be copied
  enum bleedtype type; // Ceiling or floor, occlusion-based or not
} bleedthrough_t;

extern int numfakeplanes = 0;
extern fakegroup_t *fakeplanes = NULL;
extern sector_t **sectors2 = NULL;
extern bleedthrough_t *bleedsectors = NULL;
extern int numbleedsectors = 0;

static void gld_PrepareSectorSpecialEffects(void);
static void gld_PreprocessFakeSector(int ceiling, sector_t *sector, int groupid);
static void gld_RegisterBleedthroughSector(sector_t* source, sector_t* target, enum bleedtype type);

static void gld_PrepareSectorSpecialEffects(void)
{
  int i, num;

  /* free memory if allocated by previous maps */
  if (bleedsectors)
  {
    Z_Free(bleedsectors);
    numbleedsectors = 0;
    bleedsectors = NULL;
  }

  for (num = 0; num < numsectors; num++)
  {
    // the following is for specialeffects. see r_bsp.c in R_Subsector
    sectors[num].flags |= (NO_TOPTEXTURES | NO_BOTTOMTEXTURES);

    for (i=0; i<sectors[num].linecount; i++)
    {
      unsigned short sidenum0 = sectors[num].lines[i]->sidenum[0];
      unsigned short sidenum1 = sectors[num].lines[i]->sidenum[1];
      side_t *side0 = (sidenum0 == NO_INDEX ? NULL : &sides[sidenum0]);
      side_t *side1 = (sidenum1 == NO_INDEX ? NULL : &sides[sidenum1]);
      side_t *front;
      side_t *back;
      dboolean needs_front_lower, needs_front_upper;

      if (!side0 || !side1 || side0->sector == side1->sector)
      {
        sectors[num].flags &= ~NO_TOPTEXTURES;
        sectors[num].flags &= ~NO_BOTTOMTEXTURES;
        continue;
      }

      if (side0->sector == &sectors[num])
      {
        front = side0;
        back = side1;
      }
      else
      {
        front = side1;
        back = side0;
      }

      if (front->toptexture != NO_TEXTURE)
        sectors[num].flags &= ~NO_TOPTEXTURES;
      if (front->bottomtexture != NO_TEXTURE)
        sectors[num].flags &= ~NO_BOTTOMTEXTURES;
      if (back->toptexture != NO_TEXTURE)
        sectors[num].flags &= ~NO_TOPTEXTURES;
      if (back->bottomtexture != NO_TEXTURE)
        sectors[num].flags &= ~NO_BOTTOMTEXTURES;

      needs_front_lower =
          back->sector->floorpic != skyflatnum &&
          front->sector->floorheight < back->sector->floorheight;
      needs_front_upper =
          back->sector->ceilingpic != skyflatnum &&
          front->sector->ceilingheight > back->sector->ceilingheight;

      /* now mark the sectors if they require flat bleed-through */
      if (needs_front_upper && front->toptexture == NO_TEXTURE)
      {
        back->sector->flags |= MISSING_TOPTEXTURES;
        gld_RegisterBleedthroughSector(front->sector, back->sector, BLEED_CEILING);
        front->sector->flags |= MISSING_TOPTEXTURES;
        gld_RegisterBleedthroughSector(back->sector, front->sector, BLEED_CEILING | BLEED_OCCLUDE);
      }
      if (needs_front_lower && front->bottomtexture == NO_TEXTURE)
      {
        back->sector->flags |= MISSING_BOTTOMTEXTURES;
        gld_RegisterBleedthroughSector(front->sector, back->sector, BLEED_NONE);
        front->sector->flags |= MISSING_BOTTOMTEXTURES;
        gld_RegisterBleedthroughSector(back->sector, front->sector, BLEED_OCCLUDE);
      }
    }
#ifdef PRBOOM_DEBUG
    if (sectors[num].flags & NO_TOPTEXTURES)
      lprintf(LO_INFO,"Sector %i has no toptextures\n",num);
    if (sectors[num].flags & NO_BOTTOMTEXTURES)
      lprintf(LO_INFO,"Sector %i has no bottomtextures\n",num);
#endif
  }
}

static void gld_RegisterBleedthroughSector(sector_t* source, sector_t* target, enum bleedtype type)
{
  int i;
  int idx = -1;
  assert(source);
  assert(target);

  /* check whether the target sector is processed already */
  for (i = 0; i < numbleedsectors && idx == -1; i++)
    if (bleedsectors[i].target == target && bleedsectors[i].type == type)
      idx = i;

  if (idx == -1)
  {
    /* allocate memory for new sector */
    bleedsectors = (bleedthrough_t*) Z_Realloc(bleedsectors, (numbleedsectors + 1) * sizeof(bleedthrough_t));
    if(!bleedsectors) I_Error("gld_RegisterBleedthroughSector: Out of memory");
    memset(&bleedsectors[numbleedsectors], 0, sizeof(bleedthrough_t));
    numbleedsectors++;

    idx = numbleedsectors - 1;
  }

  bleedsectors[idx].type = type;
  bleedsectors[idx].target = target;

  /* either register the proposed source since it is first,
   * or check if the new proposed source is a better option
   * and register it instead */
  if (bleedsectors[idx].source == NULL ||
      ((type & BLEED_CEILING) && bleedsectors[idx].source->ceilingheight > source->ceilingheight) ||
      (!(type & BLEED_CEILING) && bleedsectors[idx].source->floorheight < source->floorheight))
    bleedsectors[idx].source = source;
}

//
// Recursive mark of all adjoining sectors with no bottom/top texture
//

static void gld_PreprocessFakeSector(int ceiling, sector_t *sector, int groupid)
{
  int i;

  if (sector->fakegroup[ceiling] != groupid)
  {
    sector->fakegroup[ceiling] = groupid;
    if (groupid >= numfakeplanes)
    {
      fakeplanes = Z_Realloc(fakeplanes, (numfakeplanes + 1) * sizeof(fakegroup_t));
      memset(&fakeplanes[numfakeplanes], 0, sizeof(fakegroup_t));
      numfakeplanes++;
    }
    sectors2[fakeplanes[groupid].count++] = sector;
  }

  for (i = 0; i < sector->linecount; i++)
  {
    sector_t *sec = NULL;
    line_t *line = sector->lines[i];

    if (line->frontsector && line->frontsector != sector)
    {
      sec = line->frontsector;
    }
    else
    {
      if (line->backsector && line->backsector != sector)
      {
        sec = line->backsector;
      }
    }

    if (sec && sec->fakegroup[ceiling] == -1 &&
       (sec->flags & (ceiling ? NO_TOPTEXTURES : NO_BOTTOMTEXTURES)))
    {
      gld_PreprocessFakeSector(ceiling, sec, groupid);
    }
  }
}

//
// Split of all sectors into groups
// with adjoining sectors with no bottom/top texture
//

void gld_PreprocessFakeSectors(void)
{
  int i, j, k, ceiling;
  int groupid;

  if (gl_use_stencil)
  {
    // precalculate NO_TOPTEXTURES and NO_BOTTOMTEXTURES flags
    gld_PrepareSectorSpecialEffects();
    return;
  }

  // free memory
  if (fakeplanes)
  {
    for (i = 0; i < numfakeplanes; i++)
    {
      fakeplanes[i].count = 0;
      Z_Free(fakeplanes[i].list);
      fakeplanes[i].list = NULL;
    }
    numfakeplanes = 0;
    Z_Free(fakeplanes);
    fakeplanes = NULL;
  }
  if (sectors2)
  {
    Z_Free(sectors2);
  }
  sectors2 = Z_Malloc(numsectors * sizeof(sector_t*));

  // reset all groups with fake floors and ceils
  // 0 - floor; 1 - ceil;
  for (i = 0; i < numsectors; i++)
  {
    sectors[i].fakegroup[0] = -1;
    sectors[i].fakegroup[1] = -1;
  }

  // precalculate NO_TOPTEXTURES and NO_BOTTOMTEXTURES flags
  gld_PrepareSectorSpecialEffects();

  groupid = 0;

  for (ceiling = 0; ceiling <= 1; ceiling++)
  {
    unsigned int no_texture_flag = (ceiling ? NO_TOPTEXTURES : NO_BOTTOMTEXTURES);

    do
    {
      for (i = 0; i < numsectors; i++)
      {
        if (!(sectors[i].flags & no_texture_flag)
          && (sectors[i].fakegroup[ceiling] == -1))
        {
          gld_PreprocessFakeSector(ceiling, &sectors[i], groupid);
          fakeplanes[groupid].ceiling = ceiling;
          fakeplanes[groupid].list = Z_Malloc(fakeplanes[groupid].count * sizeof(sector_t*));
          for (j = 0, k = 0; k < fakeplanes[groupid].count; k++)
          {
            if (!(sectors2[k]->flags & no_texture_flag))
            {
              fakeplanes[groupid].list[j++] = sectors2[k];
            }
          }
          fakeplanes[groupid].count = j;
          groupid++;
          break;
        }
      }
    }
    while (i < numsectors);
  }
}

//
// Get highest surounding floorheight for flors and
// lowest surounding ceilingheight for ceilings
//

sector_t* GetBestFake(sector_t *sector, int ceiling, int validcount)
{
  int i;
  int groupid = sector->fakegroup[ceiling];

  if (groupid == -1)
    return NULL;

  if (fakeplanes[groupid].validcount != validcount)
  {
    fakeplanes[groupid].validcount = validcount;
    fakeplanes[groupid].sector = NULL;

    if (fakeplanes[groupid].ceiling)
    {
      fixed_t min_height = INT_MAX;
      for (i = 0; i < fakeplanes[groupid].count; i++)
      {
        if (!(fakeplanes[groupid].list[i]->flags & NO_TOPTEXTURES) &&
          fakeplanes[groupid].list[i]->ceilingheight < min_height)
        {
          min_height = fakeplanes[groupid].list[i]->ceilingheight;
          fakeplanes[groupid].sector = fakeplanes[groupid].list[i];
        }
      }
    }
    else
    {
      fixed_t max_height = INT_MIN;
      for (i = 0; i < fakeplanes[groupid].count; i++)
      {
        if (!(fakeplanes[groupid].list[i]->flags & NO_BOTTOMTEXTURES) &&
          fakeplanes[groupid].list[i]->floorheight > max_height)
        {
          max_height = fakeplanes[groupid].list[i]->floorheight;
          fakeplanes[groupid].sector = fakeplanes[groupid].list[i];
        }
      }
    }
  }

  if (fakeplanes[groupid].sector)
  {
    if (fakeplanes[groupid].ceiling)
    {
      if (sector->ceilingheight < fakeplanes[groupid].sector->ceilingheight)
      {
        return sector;
      }
    }
    else
    {
      if (sector->floorheight > fakeplanes[groupid].sector->floorheight)
      {
        return sector;
      }
    }
  }

  return fakeplanes[groupid].sector;
}

//==========================================================================
//
// Flood gaps with the back side's ceiling/floor texture
// This requires a stencil because the projected plane interferes with
// the depth buffer
//
//==========================================================================

void gld_SetupFloodStencil(GLWall *wall)
{
  int recursion = 0;

  // Create stencil
  glStencilFunc(GL_EQUAL, recursion, ~0); // create stencil
  glStencilOp(GL_KEEP, GL_KEEP, GL_INCR); // increment stencil of valid pixels
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // don't write to the graphics buffer
  gld_EnableTexture2D(GL_TEXTURE0_ARB, false);
  glColor3f(1, 1, 1);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(true);

  glBegin(GL_TRIANGLE_FAN);
  glVertex3f(wall->glseg->x1, wall->ytop, wall->glseg->z1);
  glVertex3f(wall->glseg->x1, wall->ybottom, wall->glseg->z1);
  glVertex3f(wall->glseg->x2, wall->ybottom, wall->glseg->z2);
  glVertex3f(wall->glseg->x2, wall->ytop, wall->glseg->z2);
  glEnd();

  glStencilFunc(GL_EQUAL, recursion+1, ~0); // draw sky into stencil
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);   // this stage doesn't modify the stencil

  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); // don't write to the graphics buffer
  gld_EnableTexture2D(GL_TEXTURE0_ARB, true);
  glDisable(GL_DEPTH_TEST);
  glDepthMask(false);
}

void gld_ClearFloodStencil(GLWall *wall)
{
  int recursion = 0;

  glStencilOp(GL_KEEP, GL_KEEP, GL_DECR);
  gld_EnableTexture2D(GL_TEXTURE0_ARB, false);
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE); // don't write to the graphics buffer
  glColor3f(1, 1, 1);

  glBegin(GL_TRIANGLE_FAN);
  glVertex3f(wall->glseg->x1, wall->ytop, wall->glseg->z1);
  glVertex3f(wall->glseg->x1, wall->ybottom, wall->glseg->z1);
  glVertex3f(wall->glseg->x2, wall->ybottom, wall->glseg->z2);
  glVertex3f(wall->glseg->x2, wall->ytop, wall->glseg->z2);
  glEnd();

  // restore old stencil op.
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glStencilFunc(GL_EQUAL, recursion, ~0);
  gld_EnableTexture2D(GL_TEXTURE0_ARB, true);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glEnable(GL_DEPTH_TEST);
  glDepthMask(true);
}

//
// Calculation of the coordinates of the gap
//
void gld_SetupFloodedPlaneCoords(GLWall *wall, gl_strip_coords_t *c)
{
  float prj_fac1, prj_fac2;
  float k = 0.5f;
  float ytop, ybottom, planez;

  if (wall->flag == GLDWF_TOPFLUD)
  {
    ytop = wall->ybottom;
    ybottom = wall->ytop;
    planez = wall->ybottom;
  }
  else
  {
    ytop = wall->ytop;
    ybottom = wall->ybottom;
    planez = wall->ytop;
  }

  prj_fac1 = (ytop - zCamera) / (ytop - zCamera);
  prj_fac2 = (ytop - zCamera) / (ybottom - zCamera);

  c->v[0][0] = xCamera + prj_fac1 * (wall->glseg->x1 - xCamera);
  c->v[0][1] = planez;
  c->v[0][2] = yCamera + prj_fac1 * (wall->glseg->z1 - yCamera);

  c->v[1][0] = xCamera + prj_fac2 * (wall->glseg->x1 - xCamera);
  c->v[1][1] = planez;
  c->v[1][2] = yCamera + prj_fac2 * (wall->glseg->z1 - yCamera);

  c->v[2][0] = xCamera + prj_fac1 * (wall->glseg->x2 - xCamera);
  c->v[2][1] = planez;
  c->v[2][2] = yCamera + prj_fac1 * (wall->glseg->z2 - yCamera);

  c->v[3][0] = xCamera + prj_fac2 * (wall->glseg->x2 - xCamera);
  c->v[3][1] = planez;
  c->v[3][2] = yCamera + prj_fac2 * (wall->glseg->z2 - yCamera);

  c->t[0][0] = -c->v[0][0] / k;
  c->t[0][1] = -c->v[0][2] / k;

  c->t[1][0] = -c->v[1][0] / k;
  c->t[1][1] = -c->v[1][2] / k;

  c->t[2][0] = -c->v[2][0] / k;
  c->t[2][1] = -c->v[2][2] / k;

  c->t[3][0] = -c->v[3][0] / k;
  c->t[3][1] = -c->v[3][2] / k;
}

void gld_SetupFloodedPlaneLight(GLWall *wall)
{
  if (wall->seg->backsector)
  {
    float light;
    light = gld_CalcLightLevel(wall->seg->backsector->lightlevel+(extralight<<5));
    gld_StaticLightAlpha(light, wall->alpha);
  }
  else
  {
    gld_StaticLightAlpha(wall->light, wall->alpha);
  }
}
