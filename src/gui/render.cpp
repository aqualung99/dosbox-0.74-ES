/*
 *  Copyright (C) 2002-2010  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/* $Id: render.cpp,v 1.60 2009-04-26 19:14:50 harekiet Exp $ */

#include <sys/types.h>
#include <assert.h>
#include <math.h>

#include "dosbox.h"
#include "video.h"
#include "render.h"
#include "setup.h"
#include "control.h"
#include "mapper.h"
#include "cross.h"
#include "hardware.h"
#include "support.h"

Render_t render;
ScalerLineHandler_t RENDER_DrawLine;

static void RENDER_CallBack( GFX_CallBackFunctions_t function );

static void Check_Palette(void) {
	if (render.pal.first>render.pal.last)
		return;
	GFX_SetPalette(render.pal.first,render.pal.last-render.pal.first+1,(GFX_PalEntry *)&render.pal.rgb[render.pal.first]);
	/* Setup pal index to startup values */
	render.pal.first=256;
	render.pal.last=0;
}

void RENDER_SetPal(Bit8u entry,Bit8u red,Bit8u green,Bit8u blue) {
	render.pal.rgb[entry].red=red;
	render.pal.rgb[entry].green=green;
	render.pal.rgb[entry].blue=blue;
	if (render.pal.first>entry) render.pal.first=entry;
	if (render.pal.last<entry) render.pal.last=entry;
}

static void RENDER_EmptyLineHandler(const void * src) {
}

static void RENDER_StartLineHandler(const void * s) {
	if (s) {
		const Bitu *src = (Bitu*)s;
		Bit8u* unusedPtr;
		Bitu unusedPitch;
		if (!GFX_StartUpdate( unusedPtr, unusedPitch )) {
			RENDER_DrawLine = RENDER_EmptyLineHandler;
			return;
		}
		if (render.src.bpp == 8)
		{
			RENDER_DrawLine = GFX_LineHandler8;
		}
		else if ((render.src.bpp == 15) || (render.src.bpp == 16))
		{
			RENDER_DrawLine = GFX_LineHandler16;
		}
		else
		{
			RENDER_DrawLine = GFX_LineHandler32;
		}

		RENDER_DrawLine( s );
		return;
	}
}

static void RENDER_FinishLineHandler(const void * s) {
}

static unsigned sg_StartUpdateCounter = 0;

bool RENDER_StartUpdate(void) {
    static int startCount = 0;

	if (GCC_UNLIKELY(render.updating))
		return false;
	if (GCC_UNLIKELY(!render.active))
		return false;

	if (GCC_UNLIKELY(render.frameskip.count<render.frameskip.max)) {
		render.frameskip.count++;
		return false;
	}
	render.frameskip.count=0;
	if (render.src.bpp == 8)
	{
		Check_Palette();
	}

	RENDER_DrawLine = RENDER_StartLineHandler;
	if (GCC_UNLIKELY(CaptureState & (CAPTURE_IMAGE|CAPTURE_VIDEO)))
		render.fullFrame = true;
	else
		render.fullFrame = false;
	render.updating = true;
	return true;
}

static void RENDER_Halt( void ) {
	RENDER_DrawLine = RENDER_EmptyLineHandler;
	GFX_EndUpdate( 0 );
	render.updating=false;
	render.active=false;
}

extern Bitu PIC_Ticks;
void RENDER_EndUpdate( bool abort ) {
	if (GCC_UNLIKELY(!render.updating))
		return;
	RENDER_DrawLine = RENDER_EmptyLineHandler;
	GFX_EndUpdate(NULL);
	/*
	if (GCC_UNLIKELY(CaptureState & (CAPTURE_IMAGE|CAPTURE_VIDEO))) {
		Bitu pitch, flags;
		flags = 0;
		if (render.src.dblw != render.src.dblh) {
			if (render.src.dblw) flags|=CAPTURE_FLAG_DBLW;
			if (render.src.dblh) flags|=CAPTURE_FLAG_DBLH;
		}
		float fps = render.src.fps;
		pitch = render.scale.cachePitch;
		if (render.frameskip.max)
			fps /= 1+render.frameskip.max;
		CAPTURE_AddImage( render.src.width, render.src.height, render.src.bpp, pitch,
			flags, fps, (Bit8u *)&scalerSourceCache, (Bit8u*)&render.pal.rgb );
	}
	*/
	render.updating=false;
}


static void RENDER_Reset( void ) {
	Bitu width=render.src.width;
	Bitu height=render.src.height;
	bool dblw=render.src.dblw;
	bool dblh=render.src.dblh;

	double gfx_scalew;
	double gfx_scaleh;

	Bitu gfx_flags = GFX_CAN_8 | GFX_CAN_15 | GFX_CAN_16 | GFX_CAN_32, xscale = dblw ? 2 : 1;
	if (render.aspect) {
		if (render.src.ratio>1.0) {
			gfx_scalew = 1;
			gfx_scaleh = render.src.ratio;
		} else {
			gfx_scalew = (1/render.src.ratio);
			gfx_scaleh = 1;
		}
	} else {
		gfx_scalew = 1;
		gfx_scaleh = 1;
	}
	switch (render.src.bpp) {
	case 8:
		render.src.start = ( render.src.width * 1) / sizeof(Bitu);
		if (gfx_flags & GFX_CAN_8)
			gfx_flags |= GFX_LOVE_8;
		else
			gfx_flags |= GFX_LOVE_32;
			break;
	case 15:
			render.src.start = ( render.src.width * 2) / sizeof(Bitu);
			gfx_flags |= GFX_LOVE_15;
			gfx_flags = (gfx_flags & ~GFX_CAN_8) | GFX_RGBONLY;
			break;
	case 16:
			render.src.start = ( render.src.width * 2) / sizeof(Bitu);
			gfx_flags |= GFX_LOVE_16;
			gfx_flags = (gfx_flags & ~GFX_CAN_8) | GFX_RGBONLY;
			break;
	case 32:
			render.src.start = ( render.src.width * 4) / sizeof(Bitu);
			gfx_flags |= GFX_LOVE_32;
			gfx_flags = (gfx_flags & ~GFX_CAN_8) | GFX_RGBONLY;
			break;
	}
	gfx_flags=GFX_GetBestMode(gfx_flags);
/* Setup the scaler variables */
	gfx_flags=GFX_SetSize(width,height,gfx_flags,gfx_scalew,gfx_scaleh,&RENDER_CallBack);
	/* Reset the palette change detection to it's initial value */
	render.pal.first= 0;
	render.pal.last = 255;
	//Finish this frame using a copy only handler
	RENDER_DrawLine = RENDER_FinishLineHandler;
	render.active=true;
}

static void RENDER_CallBack( GFX_CallBackFunctions_t function ) {
	if (function == GFX_CallBackStop) {
		RENDER_Halt( );
		return;
	} else if (function == GFX_CallBackRedraw) {
		return;
	} else if ( function == GFX_CallBackReset) {
		GFX_EndUpdate( 0 );
		RENDER_Reset();
	} else {
		E_Exit("Unhandled GFX_CallBackReset %d", function );
	}
}

void RENDER_SetSize(Bitu width,Bitu height,Bitu bpp,float fps,double ratio,bool dblw,bool dblh) {
	RENDER_Halt( );
	if (!width || !height) {
		return;
	}
	if ( ratio > 1 ) {
		double target = height * ratio + 0.1;
		ratio = target / height;
	} else {
		//This would alter the width of the screen, we don't care about rounding errors here
	}
	render.src.width=width;
	render.src.height=height;
	render.src.bpp=bpp;
	render.src.dblw=dblw;
	render.src.dblh=dblh;
	render.src.fps=fps;
	render.src.ratio=ratio;
	RENDER_Reset( );
}

extern void GFX_SetTitle(Bit32s cycles, Bits frameskip,bool paused);
static void IncreaseFrameSkip(bool pressed) {
	if (!pressed)
		return;
	if (render.frameskip.max<10) render.frameskip.max++;
	LOG_MSG("Frame Skip at %d",render.frameskip.max);
	GFX_SetTitle(-1,render.frameskip.max,false);
}

static void DecreaseFrameSkip(bool pressed) {
	if (!pressed)
		return;
	if (render.frameskip.max>0) render.frameskip.max--;
	LOG_MSG("Frame Skip at %d",render.frameskip.max);
	GFX_SetTitle(-1,render.frameskip.max,false);
}

/* Disabled as I don't want to waste a keybind for that. Might be used in the future (Qbix)
static void ChangeScaler(bool pressed) {
	if (!pressed)
		return;
	render.scale.op = (scalerOperation)((int)render.scale.op+1);
	if((render.scale.op) >= scalerLast || render.scale.size == 1) {
		render.scale.op = (scalerOperation)0;
		if(++render.scale.size > 3)
			render.scale.size = 1;
	}
	RENDER_CallBack( GFX_CallBackReset );
} */

void RENDER_Init(Section * sec) {
	Section_prop * section=static_cast<Section_prop *>(sec);

	//For restarting the renderer.
	static bool running = false;
	bool aspect = render.aspect;

	render.pal.first=256;
	render.pal.last=0;
	render.aspect=section->Get_bool("aspect");
	render.frameskip.max=section->Get_int("frameskip");
	render.frameskip.count=0;

	//If something changed that needs a ReInit
	// Only ReInit when there is a src.bpp (fixes crashes on startup and directly changing the scaler without a screen specified yet)
	if(running && render.src.bpp && render.aspect != aspect)
		RENDER_CallBack( GFX_CallBackReset );

	if(!running) render.updating=true;
	running = true;

	MAPPER_AddHandler(DecreaseFrameSkip,MK_f7,MMOD1,"decfskip","Dec Fskip");
	MAPPER_AddHandler(IncreaseFrameSkip,MK_f8,MMOD1,"incfskip","Inc Fskip");
	GFX_SetTitle(-1,render.frameskip.max,false);
}
