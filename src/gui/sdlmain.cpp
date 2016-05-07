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

/* $Id: sdlmain.cpp,v 1.154 2009-06-01 10:25:51 qbix79 Exp $ */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <sys/types.h>
#ifdef WIN32
#include <signal.h>
#include <process.h>
#endif

#include "cross.h"
#include "SDL.h"

#include "dosbox.h"
#include "video.h"
#include "mouse.h"
#include "pic.h"
#include "timer.h"
#include "setup.h"
#include "support.h"
#include "debug.h"
#include "mapper.h"
#include "vga.h"
#include "keyboard.h"
#include "cpu.h"
#include "cross.h"
#include "control.h"

#define MAPPERFILE "mapper-" VERSION ".map"
//#define DISABLE_JOYSTICK


#if C_OPENGL
#include "SDL_opengl.h"
#include "SDL_OpenGL_GLExt.h"

#ifndef APIENTRY
#define APIENTRY
#endif
#ifndef APIENTRYP
#define APIENTRYP APIENTRY *
#endif

#endif //C_OPENGL

#if !(ENVIRON_INCLUDED)
extern char** environ;
#endif

#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#if (HAVE_DDRAW_H)
#include <ddraw.h>
struct private_hwdata {
	LPDIRECTDRAWSURFACE3 dd_surface;
	LPDIRECTDRAWSURFACE3 dd_writebuf;
};
#endif

#define STDOUT_FILE	TEXT("stdout.txt")
#define STDERR_FILE	TEXT("stderr.txt")
#define DEFAULT_CONFIG_FILE "/dosbox.conf"
#elif defined(MACOSX)
#define DEFAULT_CONFIG_FILE "/Library/Preferences/DOSBox Preferences"
#else /*linux freebsd*/
#define DEFAULT_CONFIG_FILE "/.dosboxrc"
#endif

#if C_SET_PRIORITY
#include <sys/resource.h>
#define PRIO_TOTAL (PRIO_MAX-PRIO_MIN)
#endif

#ifdef OS2
#define INCL_DOS
#define INCL_WIN
#include <os2.h>
#endif

enum SCREEN_TYPES	{
	SCREEN_OPENGL
};

enum PRIORITY_LEVELS {
	PRIORITY_LEVEL_PAUSE,
	PRIORITY_LEVEL_LOWEST,
	PRIORITY_LEVEL_LOWER,
	PRIORITY_LEVEL_NORMAL,
	PRIORITY_LEVEL_HIGHER,
	PRIORITY_LEVEL_HIGHEST
};

static void __CheckGL(const char *callerFile, int callerLine)
{
	GLenum e = glGetError();

	if (e != GL_NO_ERROR)
	{
		LOG_MSG("OpenGL Error in module \"%s\", line %d: glGetError() returned %x", callerFile, callerLine, e);
	}
}

#define CheckGL			__CheckGL(__FILE__, __LINE__)

struct SDL_Block {
	bool inited;
	bool active;							//If this isn't set don't draw
	bool updating;
	struct {
		Bit32u width;
		Bit32u height;
		Bit32u bpp;
		Bitu flags;
		double scalex,scaley;
		GFX_CallBack_t callback;
	} draw;
	bool wait_on_error;
	struct {
		struct {
			Bit16u width, height;
			bool fixed;
		} full;
		struct {
			Bit16u width, height;
		} window;
		Bit8u bpp;
		bool fullscreen;
		bool doublebuf;
		SCREEN_TYPES type;
		SCREEN_TYPES want_type;
	} desktop;
#if C_OPENGL
	struct {
		Bitu pitch;
		void * framebuf;
		GLuint texture;
		GLint max_texsize;
		GLint pow2TexHeight;
		GLint pow2TexWidth;
		bool bilinear;
		bool packed_pixel;
		bool paletted_texture;
	} opengl;
#endif
	struct {
		SDL_Surface * surface;
#if (HAVE_DDRAW_H) && defined(WIN32)
		RECT rect;
#endif
	} blit;
	struct {
		PRIORITY_LEVELS focus;
		PRIORITY_LEVELS nofocus;
	} priority;
	SDL_Rect clip;
	SDL_Window * window;
	SDL_GLContext context;

	// SDL doesn't fill in the OpenGL 2.x prototypes for us...
	//
	PFNGLCREATESHADERPROC glCreateShader;
	PFNGLSHADERSOURCEPROC glShaderSource;
	PFNGLCOMPILESHADERPROC glCompileShader;
	PFNGLGETSHADERIVPROC glGetShaderiv;
	PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog;
	PFNGLCREATEPROGRAMPROC glCreateProgram;
	PFNGLLINKPROGRAMPROC glLinkProgram;
	PFNGLATTACHSHADERPROC glAttachShader;
	PFNGLGETPROGRAMIVPROC glGetProgramiv;
	PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog;
	PFNGLGENBUFFERSPROC glGenBuffers;
	PFNGLBINDBUFFERPROC glBindBuffer;
	PFNGLBUFFERDATAPROC glBufferData;
	PFNGLDELETEPROGRAMPROC glDeleteProgram;
	PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation;
	PFNGLGETATTRIBLOCATIONPROC glGetAttribLocation;
	PFNGLUSEPROGRAMPROC glUseProgram;
	PFNGLUNIFORM1FPROC glUniform1f;
	PFNGLUNIFORM2FPROC glUniform2f;
	PFNGLUNIFORM4FPROC glUniform4f;
	PFNGLUNIFORM1IPROC glUniform1i;
	PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer;
	PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray;
	PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray;
	PFNGLACTIVETEXTUREPROC glActiveTexture;
	PFNGLGETACTIVEATTRIBPROC glGetActiveAttrib;
	PFNGLGETACTIVEUNIFORMPROC glGetActiveUniform;

	GLuint vsSimple;
	GLuint psSimple;
	GLuint progSimple;

	GLuint vsPalette;
	GLuint psPalette;
	GLuint progPalette;

	GLuint vertBuffer;
	GLuint idxBuffer;

	GLint simpleTexUnitAddr;
	GLint simpleFadeFactorAddr;
	GLint simplePositionAddr;

	GLint palTexUnitAddr;
	GLint palPalUnitAddr;
	GLint palPositionAddr;
	GLint palSourceSizeAddr;
	GLint palOneAddr;
	GLint palTargetSizeAddr;
	GLint palUvRatioAddr;
	GLint palAspectFixAddr;

	GLuint texPalette;

	float windowAspectFor4x3;
	unsigned nextRenderLineNum;
//	SDL_Renderer * renderer;
	SDL_cond *cond;
	struct {
		bool autolock;
		bool autoenable;
		bool requestlock;
		bool locked;
		Bitu sensitivity;
	} mouse;
	SDL_Rect updateRects[1024];
	Bitu num_joysticks;
#if defined (WIN32)
	bool using_windib;
#endif
	// state of alt-keys for certain special handlings
	Bit32u laltstate;
	Bit32u raltstate;
};

static SDL_Block sdl;


/* static variable to show wether there is not a valid stdout.
 * Fixes some bugs when -noconsole is used in a read only directory */
static bool no_stdout = false;
void GFX_ShowMsg(char const* format,...) {
	char buf[512];
	va_list msg;
	va_start(msg,format);
	vsprintf(buf,format,msg);
        strcat(buf,"\n");
	va_end(msg);
	if(!no_stdout) printf("%s",buf); //Else buf is parsed again.
}

static void GFX_AwaitKeypress(void)
{
	bool bKeyFound = false;

	LOG_MSG("Press enter key to quit...");
	char inputBuffer[1024];

	if (!no_stdout)
	{
		fscanf(stdin, "%s", inputBuffer);
	}
}

extern const char* RunningProgram;
extern bool CPU_CycleAutoAdjust;
//Globals for keyboard initialisation
bool startup_state_numlock=false;
bool startup_state_capslock=false;
void GFX_SetTitle(Bit32s cycles,Bits frameskip,bool paused){
	char title[200]={0};
	static Bit32s internal_cycles=0;
	static Bits internal_frameskip=0;
	if(cycles != -1) internal_cycles = cycles;
	if(frameskip != -1) internal_frameskip = frameskip;
	if(CPU_CycleAutoAdjust) {
		sprintf(title,"DOSBox %s, Cpu speed: max %3d%% cycles, Frameskip %2d, Program: %8s",VERSION,internal_cycles,internal_frameskip,RunningProgram);
	} else {
		sprintf(title,"DOSBox %s, Cpu speed: %8d cycles, Frameskip %2d, Program: %8s",VERSION,internal_cycles,internal_frameskip,RunningProgram);
	}

	if(paused) strcat(title," PAUSED");
	SDL_SetWindowTitle(sdl.window, title);
}

static void PauseDOSBox(bool pressed) {
	if (!pressed)
		return;
	GFX_SetTitle(-1,-1,true);
	bool paused = true;
	KEYBOARD_ClrBuffer();
	SDL_Delay(500);
	SDL_Event event;
	while (SDL_PollEvent(&event)) {
		// flush event queue.
	}

	while (paused) {
		SDL_WaitEvent(&event);    // since we're not polling, cpu usage drops to 0.
		switch (event.type) {

			case SDL_QUIT: throw(0); break;
			case SDL_KEYDOWN:   // Must use Pause/Break Key to resume.
			case SDL_KEYUP:
			if(event.key.keysym.sym==SDLK_PAUSE) {

				paused=false;
				GFX_SetTitle(-1,-1,false);
				break;
			}
		}
	}
}

#if defined (WIN32)
bool GFX_SDLUsingWinDIB(void) {
	return sdl.using_windib;
}
#endif

/* Reset the screen with current values in the sdl structure */
Bitu GFX_GetBestMode(Bitu flags) {
check_surface:
	switch (sdl.desktop.want_type) {
#if C_OPENGL
	case SCREEN_OPENGL:
		if (flags & GFX_CAN_8)
		{
			flags |= GFX_LOVE_8;
		}
		if (flags & GFX_CAN_15)
		{
			flags |= GFX_CAN_15;
		}
		if (flags & GFX_CAN_16)
		{
			flags |= GFX_CAN_16;
		}
		if (flags & GFX_CAN_32)
		{
			flags |= GFX_CAN_32;
		}
		flags |= GFX_CAN_RANDOM;
		break;
#endif
	default:
		goto check_surface;
		break;
	}
	return flags;
}


void GFX_ResetScreen(void) {
	GFX_Stop();
	if (sdl.draw.callback)
		(sdl.draw.callback)( GFX_CallBackReset );
	GFX_Start();
	CPU_Reset_AutoAdjust();
}

static int int_log2 (int val) {
    int log = 0;
    while ((val >>= 1) != 0)
	log++;
    return log;
}

static SDL_Window * GFX_SetupSurfaceScaled(Bit32u sdl_flags, Bit32u bpp) {
	Bit16u fixedWidth;
	Bit16u fixedHeight;

	if (sdl.desktop.fullscreen) {
		fixedWidth = sdl.desktop.full.fixed ? sdl.desktop.full.width : 0;
		fixedHeight = sdl.desktop.full.fixed ? sdl.desktop.full.height : 0;
		sdl_flags |= SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL;
	} else {
		fixedWidth = sdl.desktop.window.width;
		fixedHeight = sdl.desktop.window.height;
		sdl_flags |= SDL_WINDOW_OPENGL;
	}
	if (fixedWidth && fixedHeight) {
		double ratio_w=(double)fixedWidth/(sdl.draw.width*sdl.draw.scalex);
		double ratio_h=(double)fixedHeight/(sdl.draw.height*sdl.draw.scaley);
		if ( ratio_w < ratio_h) {
			sdl.clip.w=fixedWidth;
			sdl.clip.h=(Bit16u)(sdl.draw.height*sdl.draw.scaley*ratio_w);
		} else {
			sdl.clip.w=(Bit16u)(sdl.draw.width*sdl.draw.scalex*ratio_h);
			sdl.clip.h=(Bit16u)fixedHeight;
		}
		if (sdl.desktop.fullscreen)
		{
/*
			sdl.window = SDL_CreateWindow("DOSBox", 
										  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
										  fixedWidth, fixedHeight, 
										  sdl_flags | SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL | SDL_WINDOW_BORDERLESS);
*/
			SDL_SetWindowBordered(sdl.window, SDL_FALSE);
			SDL_SetWindowSize(sdl.window, fixedWidth, fixedHeight);
			SDL_SetWindowFullscreen(sdl.window, SDL_WINDOW_FULLSCREEN);
			sdl.clip.w = fixedWidth;
			sdl.clip.h = fixedHeight;
			sdl.windowAspectFor4x3 = (4.0f / 3.0f) / ((float)fixedWidth / fixedHeight);
		}
		else
/*
			if (sdl.window == NULL)
			{
				sdl.window = SDL_CreateWindow("DOSBox", 
											  SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 
											  fixedWidth, fixedHeight, 
											  sdl_flags);
				sdl.clip.w = fixedWidth;
				sdl.clip.h = fixedHeight;
			}
			else
*/
			{
				SDL_SetWindowFullscreen(sdl.window, 0);
				SDL_SetWindowBordered(sdl.window, SDL_TRUE);
				SDL_SetWindowSize(sdl.window, fixedWidth, fixedHeight);
				sdl.clip.w = fixedWidth;
				sdl.clip.h = fixedHeight;
				sdl.windowAspectFor4x3 = (4.0f / 3.0f) / ((float)fixedWidth / fixedHeight);
			}

		if (sdl.window)
		{
			if (sdl.context == NULL)
			{
				sdl.context = SDL_GL_CreateContext(sdl.window);
			}
		}

		if (sdl.window && (SDL_GetWindowFlags(sdl.window) & SDL_WINDOW_FULLSCREEN)) {
			int w, h;
			SDL_GetWindowSize(sdl.window, &w, &h);
			sdl.clip.x=(Sint16)((w-sdl.clip.w)/2);
			sdl.clip.y=(Sint16)((h-sdl.clip.h)/2);
			//SDL_SetWindowFullscreen(sdl.window, SDL_WINDOW_FULLSCREEN);
		} else {
			sdl.clip.x = 0;
			sdl.clip.y = 0;
		}
		return sdl.window;
	} else {
		sdl.clip.x=0;sdl.clip.y=0;
		sdl.clip.w=(Bit16u)(sdl.draw.width*sdl.draw.scalex);
		sdl.clip.h=(Bit16u)(sdl.draw.height*sdl.draw.scaley);
/*
		if (sdl.window == NULL)
		{
			sdl.window = SDL_CreateWindow("DOSBox", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, sdl.clip.w, sdl.clip.h, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_OPENGL);
		}
		else
*/
		{
			SDL_SetWindowSize(sdl.window, sdl.clip.w, sdl.clip.h);
		}
		return sdl.window;
	}
}

Bitu GFX_SetSize(Bitu width,Bitu height,Bitu flags,double scalex,double scaley,GFX_CallBack_t callback) {
	if (sdl.updating)
		GFX_EndUpdate( 0 );

	sdl.draw.width=width;
	sdl.draw.height=height;
	sdl.draw.callback=callback;
	sdl.draw.scalex=scalex;
	sdl.draw.scaley=scaley;

	Bitu bpp=0;
	Bitu retFlags = 0;

	if (sdl.blit.surface) {
		SDL_FreeSurface(sdl.blit.surface);
		sdl.blit.surface=0;
	}
dosurface:
	switch (sdl.desktop.want_type) {
#if C_OPENGL
	case SCREEN_OPENGL:
	{
		if (sdl.opengl.framebuf) {
			free(sdl.opengl.framebuf);
		}
		sdl.opengl.framebuf=0;
		if (!(flags&GFX_CAN_32) || (flags & GFX_RGBONLY)) goto dosurface;
		int texsize=2 << int_log2(width > height ? width : height);
		sdl.opengl.pow2TexWidth = 2 << int_log2(width + 2);		// One texel border on each side
		sdl.opengl.pow2TexHeight = 2 << int_log2(height + 2);	// One texel border on each side
		if (texsize>sdl.opengl.max_texsize) {
			LOG_MSG("SDL:OPENGL:No support for texturesize of %d, falling back to surface",texsize);
			goto dosurface;
		}
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
#if defined (WIN32) && SDL_VERSION_ATLEAST(1, 2, 11)
		SDL_GL_SetSwapInterval(1);
#endif

		GFX_SetupSurfaceScaled(0,0);
		if ( (sdl.window == NULL) || SDL_BITSPERPIXEL(SDL_GetWindowPixelFormat(sdl.window)) < 15) {
			LOG_MSG("SDL:OPENGL:Can't open drawing surface, are you running in 16bpp(or higher) mode?");
			goto dosurface;
		}
		/* Create the texture and display list */
		{
//			sdl.opengl.framebuf=malloc(width*height*4);		//32 bit color
		}
//		sdl.opengl.pitch=width*4;
		glViewport(0, 0, sdl.clip.w, sdl.clip.h);
		CheckGL;
		glDeleteTextures(1,&sdl.opengl.texture);
		CheckGL;
 		glGenTextures(1,&sdl.opengl.texture);
		CheckGL;
		sdl.glActiveTexture(GL_TEXTURE0);
		CheckGL;
		glBindTexture(GL_TEXTURE_2D,sdl.opengl.texture);
		CheckGL;

		if (flags & GFX_LOVE_8)
		{
			sdl.draw.bpp = bpp = 8;
		}
		else if (flags & GFX_LOVE_15)
		{
			sdl.draw.bpp = bpp = 15;
		}
		else if (flags & GFX_LOVE_16)
		{
			sdl.draw.bpp = bpp = 16;
		}
		else if (flags & GFX_LOVE_32)
		{
			sdl.draw.bpp = bpp = 32;
		}

		if (bpp == 8)
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, sdl.opengl.pow2TexWidth, sdl.opengl.pow2TexHeight, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE, NULL);
			CheckGL;
			// Write a blank line at the top
			// This alloc is usually 1K
			//
			GLubyte *tmpBuf = (GLubyte*)calloc(sdl.opengl.pow2TexWidth, sizeof(GLubyte));
			glTexSubImage2D(GL_TEXTURE_2D, 0, 0, sdl.opengl.pow2TexHeight - 1, sdl.opengl.pow2TexWidth, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, tmpBuf);
			free(tmpBuf);
		}
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		CheckGL;
		sdl.glUseProgram(sdl.progPalette);
		CheckGL;
		sdl.glUniform2f(sdl.palOneAddr, 1.0f / (width + 2), 1.0f / (height + 2));
		CheckGL;
		sdl.glUniform4f(sdl.palSourceSizeAddr, (GLfloat)width + 2, (GLfloat)height + 2, 0.0f, 0.0f);
		CheckGL;
		sdl.glUniform2f(sdl.palUvRatioAddr, (GLfloat)(width + 2) / sdl.opengl.pow2TexWidth, (GLfloat)(height + 2) / sdl.opengl.pow2TexHeight);
		CheckGL;
		sdl.glUniform1f(sdl.palAspectFixAddr, sdl.windowAspectFor4x3);
		CheckGL;

		sdl.desktop.type=SCREEN_OPENGL;
		retFlags = GFX_CAN_32 | GFX_SCALING;
	break;
		}//OPENGL
#endif	//C_OPENGL
	default:
		goto dosurface;
		break;
	}//CASE
	if (retFlags)
		GFX_Start();
	if (!sdl.mouse.autoenable) SDL_ShowCursor(sdl.mouse.autolock?SDL_DISABLE:SDL_ENABLE);
	return retFlags;
}

void GFX_CaptureMouse(void) {
	sdl.mouse.locked=!sdl.mouse.locked;
	if (sdl.mouse.locked) {
		SDL_SetRelativeMouseMode(SDL_TRUE);
		SDL_SetWindowGrab(sdl.window, SDL_TRUE);
	} else {
		SDL_SetRelativeMouseMode(SDL_FALSE);
		SDL_SetWindowGrab(sdl.window, SDL_FALSE);
		if (sdl.mouse.autoenable || !sdl.mouse.autolock) SDL_ShowCursor(SDL_ENABLE);
	}
        mouselocked=sdl.mouse.locked;
}

bool mouselocked; //Global variable for mapper
static void CaptureMouse(bool pressed) {
	if (!pressed)
		return;
	GFX_CaptureMouse();
}

void GFX_SwitchFullScreen(void) {
	sdl.desktop.fullscreen=!sdl.desktop.fullscreen;
	if (sdl.desktop.fullscreen) {
		if (!sdl.mouse.locked) GFX_CaptureMouse();
	} else {
		if (sdl.mouse.locked) GFX_CaptureMouse();
	}
	GFX_ResetScreen();
}

static void SwitchFullScreen(bool pressed) {
	if (!pressed)
		return;
	GFX_SwitchFullScreen();
}


bool GFX_StartUpdate(Bit8u * & pixels,Bitu & pitch) {
	if (!sdl.active || sdl.updating)
		return false;
	switch (sdl.desktop.type) {
#if C_OPENGL
	case SCREEN_OPENGL:
		//pixels=(Bit8u *)sdl.opengl.framebuf;
		//pitch=sdl.opengl.pitch;
		pixels = 0;
		pitch = 0;
		sdl.nextRenderLineNum = sdl.opengl.pow2TexHeight - 2;
		sdl.updating=true;
		sdl.glActiveTexture(GL_TEXTURE0);
		CheckGL;
		glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
		CheckGL;
		return true;
#endif
	default:
		break;
	}
	return false;
}

void GFX_LineHandler8(const void * src)
{
	sdl.glActiveTexture(GL_TEXTURE0);
	CheckGL;
    glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
	CheckGL;

	GLubyte tmpBuf = 0;

	// Write a left-side border texel with color 0
	//
	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					0, sdl.nextRenderLineNum,
					1, 1,
					GL_LUMINANCE, GL_UNSIGNED_BYTE,
					&tmpBuf);

	// Then write VGA line
	//
	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					1, sdl.nextRenderLineNum,
					sdl.draw.width, 1,
					GL_LUMINANCE, GL_UNSIGNED_BYTE,
					src);

	// Write a right-side border texel with color 0
	//
	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					sdl.draw.width + 1, sdl.nextRenderLineNum--,
					1, 1,
					GL_LUMINANCE, GL_UNSIGNED_BYTE,
					&tmpBuf);

}

void GFX_LineHandler16(const void * src)
{
	sdl.glActiveTexture(GL_TEXTURE0);
	CheckGL;
    glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
	CheckGL;

	GLushort tmpBuf = 0;

	// Write a left-side border texel with color 0
	//
	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					0, sdl.nextRenderLineNum,
					1, 1,
					GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
					&tmpBuf);

	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					1, sdl.nextRenderLineNum,
					sdl.draw.width, 1,
					GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
					src);

	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					sdl.draw.width + 1, sdl.nextRenderLineNum--,
					1, 1,
					GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
					&tmpBuf);
}

void GFX_LineHandler32(const void * src)
{
	sdl.glActiveTexture(GL_TEXTURE0);
	CheckGL;
    glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
	CheckGL;

	GLuint tmpBuf = 0;

	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					0, sdl.nextRenderLineNum,
					1, 1,
					GL_BGRA_EXT, GL_UNSIGNED_INT_8_8_8_8_REV,
					&tmpBuf);

	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					0, sdl.nextRenderLineNum,
					sdl.draw.width, 1,
					GL_BGRA_EXT, GL_UNSIGNED_INT_8_8_8_8_REV,
					src);

	glTexSubImage2D(GL_TEXTURE_2D, 
					0,
					sdl.draw.width + 1, sdl.nextRenderLineNum--,
					1, 1,
					GL_BGRA_EXT, GL_UNSIGNED_INT_8_8_8_8_REV,
					&tmpBuf);
}


void GFX_EndUpdate( const Bit16u *changedLines ) {
	if (!sdl.updating)
		return;
	sdl.updating=false;
	switch (sdl.desktop.type) {
#if C_OPENGL
	case SCREEN_OPENGL:
			sdl.glActiveTexture(GL_TEXTURE0);
			CheckGL;
            glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
			CheckGL;

			// Draw the final border line of 0's across the bottom
			//
			if (sdl.draw.bpp == 8)
			{
				// This allocation is usually 1K.
				//
				GLubyte *tmpBuf = (GLubyte*)calloc(sdl.opengl.pow2TexWidth, sizeof(GLubyte));
				glTexSubImage2D(GL_TEXTURE_2D, 0, 0, sdl.opengl.pow2TexHeight - (sdl.draw.height + 2), sdl.draw.width + 2, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, tmpBuf);
				free(tmpBuf);
			}
			sdl.glActiveTexture(GL_TEXTURE1);
			CheckGL;
			glBindTexture(GL_TEXTURE_2D, sdl.texPalette);
			CheckGL;

			sdl.glUseProgram(sdl.progPalette);
			CheckGL;

			sdl.glUniform1i(sdl.palTexUnitAddr, 0);
			CheckGL;
			sdl.glUniform1i(sdl.palPalUnitAddr, 1);
			CheckGL;

			sdl.glBindBuffer(GL_ARRAY_BUFFER, sdl.vertBuffer);
			CheckGL;
			sdl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.idxBuffer);
			CheckGL;
			sdl.glVertexAttribPointer(sdl.palPositionAddr, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, (void*)0);
			CheckGL;

			sdl.glEnableVertexAttribArray(sdl.palPositionAddr);
			CheckGL;
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
			CheckGL;
			sdl.glDisableVertexAttribArray(sdl.palPositionAddr);
			CheckGL;

			// Check for palettized vs. bitmapped
			//
			//if (sdl.
			//glTexImage2D(
			SDL_GL_SwapWindow(sdl.window);
		break;
#endif	// C_OPENGL
	default:
		break;
	}
}


void GFX_SetPalette(Bitu start,Bitu count,GFX_PalEntry * entries) {
	/* I should probably not change the GFX_PalEntry :) */
	// The palette is just a 1-dimensional texture.
	// It's the 2nd texture, stored at index 1
	//
	sdl.glActiveTexture(GL_TEXTURE1);
	CheckGL;
	glBindTexture(GL_TEXTURE_2D, sdl.texPalette);
	CheckGL;
	glTexSubImage2D(GL_TEXTURE_2D, 0, start, 0, count, 1, GL_RGBA, GL_UNSIGNED_BYTE, entries);
	CheckGL;
}

Bitu GFX_GetRGB(Bit8u red,Bit8u green,Bit8u blue) {
	switch (sdl.desktop.type) {
	case SCREEN_OPENGL:
//		return ((red << 0) | (green << 8) | (blue << 16)) | (255 << 24);
		//USE BGRA
		return ((blue << 0) | (green << 8) | (red << 16)) | (255 << 24);
	}
	return 0;
}

void GFX_Stop() {
	if (sdl.updating)
		GFX_EndUpdate( 0 );
	sdl.active=false;
}

void GFX_Start() {
	sdl.active=true;
}

static void GUI_ShutDown(Section * /*sec*/) {
	GFX_Stop();
	if (sdl.draw.callback) (sdl.draw.callback)( GFX_CallBackStop );
	if (sdl.mouse.locked) GFX_CaptureMouse();
	if (sdl.desktop.fullscreen) GFX_SwitchFullScreen();
	if (sdl.window)
	{
		if (sdl.progSimple)
		{
			sdl.glDeleteProgram(sdl.progSimple);
		}
		if (sdl.progPalette)
		{
			sdl.glDeleteProgram(sdl.progPalette);
		}
		if (sdl.texPalette)
		{
			glDeleteTextures(1, &sdl.texPalette);
		}
		if (sdl.opengl.texture)
		{
			glDeleteTextures(1, &sdl.opengl.texture);
		}
		if (sdl.context)
		{
			SDL_GL_DeleteContext(sdl.context);
		}
		SDL_DestroyWindow(sdl.window);
		// SDL_DestroyRenderer(sdl.renderer);
	}
}

static void KillSwitch(bool pressed) {
	if (!pressed)
		return;
	throw 1;
}

static void SetPriority(PRIORITY_LEVELS level) {

#if C_SET_PRIORITY
// Do nothing if priorties are not the same and not root, else the highest
// priority can not be set as users can only lower priority (not restore it)

	if((sdl.priority.focus != sdl.priority.nofocus ) &&
		(getuid()!=0) ) return;

#endif
	switch (level) {
#ifdef WIN32
	case PRIORITY_LEVEL_PAUSE:	// if DOSBox is paused, assume idle priority
	case PRIORITY_LEVEL_LOWEST:
		SetPriorityClass(GetCurrentProcess(),IDLE_PRIORITY_CLASS);
		break;
	case PRIORITY_LEVEL_LOWER:
		SetPriorityClass(GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS);
		break;
	case PRIORITY_LEVEL_NORMAL:
		SetPriorityClass(GetCurrentProcess(),NORMAL_PRIORITY_CLASS);
		break;
	case PRIORITY_LEVEL_HIGHER:
		SetPriorityClass(GetCurrentProcess(),ABOVE_NORMAL_PRIORITY_CLASS);
		break;
	case PRIORITY_LEVEL_HIGHEST:
		SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
		break;
#elif C_SET_PRIORITY
/* Linux use group as dosbox has mulitple threads under linux */
	case PRIORITY_LEVEL_PAUSE:	// if DOSBox is paused, assume idle priority
	case PRIORITY_LEVEL_LOWEST:
		setpriority (PRIO_PGRP, 0,PRIO_MAX);
		break;
	case PRIORITY_LEVEL_LOWER:
		setpriority (PRIO_PGRP, 0,PRIO_MAX-(PRIO_TOTAL/3));
		break;
	case PRIORITY_LEVEL_NORMAL:
		setpriority (PRIO_PGRP, 0,PRIO_MAX-(PRIO_TOTAL/2));
		break;
	case PRIORITY_LEVEL_HIGHER:
		setpriority (PRIO_PGRP, 0,PRIO_MAX-((3*PRIO_TOTAL)/5) );
		break;
	case PRIORITY_LEVEL_HIGHEST:
		setpriority (PRIO_PGRP, 0,PRIO_MAX-((3*PRIO_TOTAL)/4) );
		break;
#endif
	default:
		break;
	}
}

extern Bit8u int10_font_14[256 * 14];

static const int gc_warningTexWidth = 640;
static const int gc_warningTexHeight = 400;
static const int gc_warningTexBytesPerPixel = 4;

static void OutputStringGL(Bitu x,Bitu y,const char * text,Bit32u color,Bit32u color2,GLubyte* pDestBuffer) {
	Bit32u * draw=(Bit32u*)(((Bit8u *)pDestBuffer)+((y)*(gc_warningTexWidth * gc_warningTexBytesPerPixel)))+x;
	while (*text) {
		Bit8u * font=&int10_font_14[(*text)*14];
		Bitu i,j;
		Bit32u * draw_line=draw;
		for (i=0;i<14;i++) {
			Bit8u map=*font++;
			for (j=0;j<8;j++) {
				if (map & 0x80) *((Bit32u*)(draw_line+j))=color; else *((Bit32u*)(draw_line+j))=color2;
				map<<=1;
			}
			draw_line+=(gc_warningTexWidth * gc_warningTexBytesPerPixel)/4;
		}
		text++;
		draw+=8;
	}
}

static GLuint CreateObjectBuffer(GLenum target, const void *buffer_data, GLsizei buffer_size) 
{
    GLuint buffer;

    sdl.glGenBuffers(1, &buffer);
	CheckGL;
    sdl.glBindBuffer(target, buffer);
	CheckGL;
    sdl.glBufferData(target, buffer_size, buffer_data, GL_STATIC_DRAW);
	CheckGL;

    return buffer;
}

static GLuint CreateProgram(GLuint vertexShader, GLuint fragmentShader)
{
	GLuint prog = sdl.glCreateProgram();
	CheckGL;

	sdl.glAttachShader(prog, vertexShader);
	CheckGL;
	sdl.glAttachShader(prog, fragmentShader);
	CheckGL;
	sdl.glLinkProgram(prog);
	CheckGL;

	GLint linkResult;
	GLint logLength;

	sdl.glGetProgramiv(prog, GL_LINK_STATUS, &linkResult);
	CheckGL;
	sdl.glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &logLength);
	CheckGL;

	if (logLength > 0)
	{
		char *szLogBuffer = (char*)malloc(logLength);
		szLogBuffer[0] = '\0';
		sdl.glGetProgramInfoLog(prog, logLength, NULL, szLogBuffer);
		CheckGL;
		LOG_MSG("Results of linking: %s", szLogBuffer);
		free(szLogBuffer);
	}
	if (linkResult == GL_TRUE)
	{
		GLint numActiveAttribs = 0;
		GLint numActiveUniforms = 0;

		sdl.glUseProgram(prog);
		CheckGL;
		sdl.glGetProgramiv(prog, GL_ACTIVE_ATTRIBUTES, &numActiveAttribs);
		CheckGL;
		sdl.glGetProgramiv(prog, GL_ACTIVE_UNIFORMS, &numActiveUniforms);
		CheckGL;

		char *progSummary = (char*)malloc(64 * 1024);
		strcpy(progSummary, "Shaders linked successfully.\nAttributes found:\n");

		for (int i = 0; i < numActiveAttribs; i++)
		{
			GLint arraySize = 0;
			GLenum type = 0;
			GLsizei actualLength = 0;
			char nameBuf[256];

			sdl.glGetActiveAttrib(prog, i, 256, &actualLength, &arraySize, &type, nameBuf);
			CheckGL;

			strcat(progSummary, "\t");
			strcat(progSummary, nameBuf);
			strcat(progSummary, "\n");
		}

		strcat(progSummary, "Uniforms found:\n");
		for (int i = 0; i < numActiveUniforms; i++)
		{
			GLint arraySize = 0;
			GLenum type = 0;
			GLsizei actualLength = 0;
			char nameBuf[256];

			sdl.glGetActiveUniform(prog, i, 256, &actualLength, &arraySize, &type, nameBuf);
			CheckGL;

			strcat(progSummary, "\t");
			strcat(progSummary, nameBuf);
			strcat(progSummary, "\n");
		}
		LOG_MSG("%s", progSummary);
		free(progSummary);
	}
	else
	{
		GFX_AwaitKeypress();
	}

	if (linkResult == GL_TRUE)
	{
		return prog;
	}
	else
	{
		return 0;
	}
}

static GLuint LoadShader(const char *szShaderFile, GLenum shaderType)
{
	static char *pathPrep = "shaders\\";

	char *szBuffer = (char*)malloc(strnlen(szShaderFile, 1024) + strlen(pathPrep) + 1);
	strcpy(szBuffer, pathPrep);
	strncat(szBuffer, szShaderFile, 1024);

	FILE *fp = fopen(szBuffer, "rt");
	if (!fp)
	{
		LOG_MSG("Could not open file \"%s\"", szShaderFile);
		GFX_AwaitKeypress();
		return 0;
	}

	char **rgszShaderCodeLine = (char**)malloc(sizeof(char*));
	int *rgLineLengths = (int*)malloc(sizeof(int));
	int codeLine = 0;

	while (!feof(fp))
	{
		char lineBuffer[64 * 1024];

		lineBuffer[0] = 0;
		fgets(lineBuffer, 64 * 1024, fp);

		rgszShaderCodeLine = (char**)realloc(rgszShaderCodeLine, (codeLine + 1) * sizeof(char*));
		rgLineLengths = (int*)realloc(rgLineLengths, (codeLine + 1) * sizeof(int));

		rgLineLengths[codeLine] = strlen(lineBuffer) + 1;

		rgszShaderCodeLine[codeLine] = (char*)malloc(rgLineLengths[codeLine]);
		memcpy(rgszShaderCodeLine[codeLine], lineBuffer, rgLineLengths[codeLine]);

		codeLine++;
	};

	fclose(fp);

	GLuint shaderProg = sdl.glCreateShader(shaderType);
	CheckGL;
	sdl.glShaderSource(shaderProg, codeLine - 1, rgszShaderCodeLine, NULL);
	CheckGL;
	sdl.glCompileShader(shaderProg);
	CheckGL;

	GLint compileResult;
	GLint logLength;

	sdl.glGetShaderiv(shaderProg, GL_COMPILE_STATUS, &compileResult);
	CheckGL;
	sdl.glGetShaderiv(shaderProg, GL_INFO_LOG_LENGTH, &logLength);
	CheckGL;

	if (logLength > 0)
	{
		char *szLogBuffer = (char*)malloc(logLength);
		szLogBuffer[0] = '\0';
		sdl.glGetShaderInfoLog(shaderProg, logLength, NULL, szLogBuffer);
		LOG_MSG("Results of compiling \"%s\": %s", szShaderFile, szLogBuffer);
		free(szLogBuffer);
	}
	else
	{
		LOG_MSG("Compiled shader \"%s\"", szShaderFile);
	}
	if (compileResult != GL_TRUE)
	{
		GFX_AwaitKeypress();
	}

	for (int i = 0; i < codeLine; i++)
	{
		free(rgszShaderCodeLine[i]);
	}
	free(rgszShaderCodeLine);
	free(rgLineLengths);
	free(szBuffer);

	if (compileResult == GL_TRUE)
	{
		return shaderProg;
	}
	else
	{
		return 0;
	}
}

static const GLfloat g_vertex_buffer_data[] = { 
    -1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
     1.0f, -1.0f
};
static const GLubyte g_element_buffer_data[] = { 0, 1, 2, 3 };

static unsigned char logo[32*32*4]= {
#include "dosbox_logo.h"
};
#include "dosbox_splash.h"

//extern void UI_Run(bool);
static void GUI_StartUp(Section * sec) {
	sec->AddDestroyFunction(&GUI_ShutDown);
	Section_prop * section=static_cast<Section_prop *>(sec);
	sdl.active=false;
	sdl.updating=false;

#if !defined(MACOSX)

#if WORDS_BIGENDIAN
	SDL_Surface* logos= SDL_CreateRGBSurfaceFrom((void*)logo,32,32,32,128,0xff000000,0x00ff0000,0x0000ff00,0);
#else
	SDL_Surface* logos= SDL_CreateRGBSurfaceFrom((void*)logo,32,32,32,128,0x000000ff,0x0000ff00,0x00ff0000,0);
#endif
	SDL_SetWindowIcon(sdl.window, logos);

#endif	// !MACOSX

	sdl.desktop.fullscreen=section->Get_bool("fullscreen");
	sdl.wait_on_error=section->Get_bool("waitonerror");

	Prop_multival* p=section->Get_multival("priority");
	std::string focus = p->GetSection()->Get_string("active");
	std::string notfocus = p->GetSection()->Get_string("inactive");

	if      (focus == "lowest")  { sdl.priority.focus = PRIORITY_LEVEL_LOWEST;  }
	else if (focus == "lower")   { sdl.priority.focus = PRIORITY_LEVEL_LOWER;   }
	else if (focus == "normal")  { sdl.priority.focus = PRIORITY_LEVEL_NORMAL;  }
	else if (focus == "higher")  { sdl.priority.focus = PRIORITY_LEVEL_HIGHER;  }
	else if (focus == "highest") { sdl.priority.focus = PRIORITY_LEVEL_HIGHEST; }

	if      (notfocus == "lowest")  { sdl.priority.nofocus=PRIORITY_LEVEL_LOWEST;  }
	else if (notfocus == "lower")   { sdl.priority.nofocus=PRIORITY_LEVEL_LOWER;   }
	else if (notfocus == "normal")  { sdl.priority.nofocus=PRIORITY_LEVEL_NORMAL;  }
	else if (notfocus == "higher")  { sdl.priority.nofocus=PRIORITY_LEVEL_HIGHER;  }
	else if (notfocus == "highest") { sdl.priority.nofocus=PRIORITY_LEVEL_HIGHEST; }
	else if (notfocus == "pause")   {
		/* we only check for pause here, because it makes no sense
		 * for DOSBox to be paused while it has focus
		 */
		sdl.priority.nofocus=PRIORITY_LEVEL_PAUSE;
	}

	SetPriority(sdl.priority.focus); //Assume focus on startup
	sdl.mouse.locked=false;
	mouselocked=false; //Global for mapper
	sdl.mouse.requestlock=false;
	sdl.desktop.full.fixed=false;
	const char* fullresolution=section->Get_string("fullresolution");
	sdl.desktop.full.width  = 0;
	sdl.desktop.full.height = 0;
	if(fullresolution && *fullresolution) {
		char res[100];
		strncpy( res, fullresolution, sizeof( res ));
		fullresolution = lowcase (res);//so x and X are allowed
		if(strcmp(fullresolution,"original")) {
			sdl.desktop.full.fixed = true;
			char* height = const_cast<char*>(strchr(fullresolution,'x'));
			if(height && * height) {
				*height = 0;
				sdl.desktop.full.height = (Bit16u)atoi(height+1);
				sdl.desktop.full.width  = (Bit16u)atoi(res);
			}
		}
	}

	sdl.desktop.window.width  = 0;
	sdl.desktop.window.height = 0;
	const char* windowresolution=section->Get_string("windowresolution");
	if(windowresolution && *windowresolution) {
		char res[100];
		strncpy( res,windowresolution, sizeof( res ));
		windowresolution = lowcase (res);//so x and X are allowed
		if(strcmp(windowresolution,"original")) {
			char* height = const_cast<char*>(strchr(windowresolution,'x'));
			if(height && *height) {
				*height = 0;
				sdl.desktop.window.height = (Bit16u)atoi(height+1);
				sdl.desktop.window.width  = (Bit16u)atoi(res);
			}
		}
	}
	sdl.desktop.doublebuf=section->Get_bool("fulldouble");
	if (!sdl.desktop.full.width) {
#ifdef WIN32
		sdl.desktop.full.width=(Bit16u)GetSystemMetrics(SM_CXSCREEN);
#else
		sdl.desktop.full.width=1024;
#endif
	}
	if (!sdl.desktop.full.height) {
#ifdef WIN32
		sdl.desktop.full.height=(Bit16u)GetSystemMetrics(SM_CYSCREEN);
#else
		sdl.desktop.full.height=768;
#endif
	}
	sdl.mouse.autoenable=section->Get_bool("autolock");
	if (!sdl.mouse.autoenable) SDL_ShowCursor(SDL_DISABLE);
	sdl.mouse.autolock=false;
	sdl.mouse.sensitivity=section->Get_int("sensitivity");
	std::string output=section->Get_string("output");

	/* Setup Mouse correctly if fullscreen */
	if(sdl.desktop.fullscreen) GFX_CaptureMouse();

	sdl.desktop.want_type=SCREEN_OPENGL;
	sdl.opengl.bilinear=true;

#if C_OPENGL
   if(sdl.desktop.want_type==SCREEN_OPENGL){ /* OPENGL is requested */
	sdl.opengl.framebuf=0;
	sdl.opengl.texture=0;
	// Expect minimum 15-bit color
	//
	SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 5);
	SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 5);

	// We don't need a depth buffer
	//
	SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);

	// Require a GPU
	//
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);

	sdl.window = SDL_CreateWindow("DosBox", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 400, SDL_WINDOW_OPENGL);
	// SDL_CreateWindowAndRenderer(640, 480, SDL_WINDOW_OPENGL, &sdl.window, &sdl.renderer);

	sdl.context = SDL_GL_CreateContext(sdl.window);

	glGetIntegerv (GL_MAX_TEXTURE_SIZE, &sdl.opengl.max_texsize);
	CheckGL;


	// We use OpenGL 2.1 prototypes. This is around 2006 - 2007 era of tech,
	// but at that time only high-end PCs would have graphics cards capable of this.
	//
	sdl.glCreateShader = (PFNGLCREATESHADERPROC)SDL_GL_GetProcAddress("glCreateShader");
	sdl.glShaderSource = (PFNGLSHADERSOURCEPROC)SDL_GL_GetProcAddress("glShaderSource");
	sdl.glCompileShader = (PFNGLCOMPILESHADERPROC)SDL_GL_GetProcAddress("glCompileShader");
	sdl.glGetShaderiv = (PFNGLGETSHADERIVPROC)SDL_GL_GetProcAddress("glGetShaderiv");
	sdl.glGetShaderInfoLog = (PFNGLGETSHADERINFOLOGPROC)SDL_GL_GetProcAddress("glGetShaderInfoLog");
	sdl.glCreateProgram = (PFNGLCREATEPROGRAMPROC)SDL_GL_GetProcAddress("glCreateProgram");
	sdl.glLinkProgram = (PFNGLLINKPROGRAMPROC)SDL_GL_GetProcAddress("glLinkProgram");
	sdl.glAttachShader = (PFNGLATTACHSHADERPROC)SDL_GL_GetProcAddress("glAttachShader");
	sdl.glGetProgramiv = (PFNGLGETPROGRAMIVPROC)SDL_GL_GetProcAddress("glGetProgramiv");
	sdl.glGetProgramInfoLog = (PFNGLGETPROGRAMINFOLOGPROC)SDL_GL_GetProcAddress("glGetProgramInfoLog");
	sdl.glGenBuffers = (PFNGLGENBUFFERSPROC)SDL_GL_GetProcAddress("glGenBuffers");
	sdl.glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
	sdl.glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
	sdl.glDeleteProgram = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");
	sdl.glGetUniformLocation = (PFNGLGETUNIFORMLOCATIONPROC)SDL_GL_GetProcAddress("glGetUniformLocation");
	sdl.glGetAttribLocation = (PFNGLGETATTRIBLOCATIONPROC)SDL_GL_GetProcAddress("glGetAttribLocation");
	sdl.glUseProgram = (PFNGLUSEPROGRAMPROC)SDL_GL_GetProcAddress("glUseProgram");
	sdl.glUniform1f = (PFNGLUNIFORM1FPROC)SDL_GL_GetProcAddress("glUniform1f");
	sdl.glUniform2f = (PFNGLUNIFORM2FPROC)SDL_GL_GetProcAddress("glUniform2f");
	sdl.glUniform4f = (PFNGLUNIFORM4FPROC)SDL_GL_GetProcAddress("glUniform4f");
	sdl.glUniform1i = (PFNGLUNIFORM1IPROC)SDL_GL_GetProcAddress("glUniform1i");
	sdl.glVertexAttribPointer = (PFNGLVERTEXATTRIBPOINTERPROC)SDL_GL_GetProcAddress("glVertexAttribPointer");
	sdl.glEnableVertexAttribArray = (PFNGLENABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glEnableVertexAttribArray");
	sdl.glDisableVertexAttribArray = (PFNGLDISABLEVERTEXATTRIBARRAYPROC)SDL_GL_GetProcAddress("glDisableVertexAttribArray");
	sdl.glActiveTexture = (PFNGLACTIVETEXTUREPROC)SDL_GL_GetProcAddress("glActiveTexture");
	sdl.glGetActiveAttrib = (PFNGLGETACTIVEATTRIBPROC)SDL_GL_GetProcAddress("glGetActiveAttrib");
	sdl.glGetActiveUniform = (PFNGLGETACTIVEUNIFORMPROC)SDL_GL_GetProcAddress("glGetActiveUniform");

	sdl.vertBuffer = CreateObjectBuffer(GL_ARRAY_BUFFER, g_vertex_buffer_data, sizeof(g_vertex_buffer_data));
	CheckGL;
	sdl.idxBuffer = CreateObjectBuffer(GL_ELEMENT_ARRAY_BUFFER, g_element_buffer_data, sizeof(g_element_buffer_data));
	CheckGL;

	sdl.vsSimple = LoadShader("simple.vs", GL_VERTEX_SHADER);
	sdl.psSimple = LoadShader("simple.ps", GL_FRAGMENT_SHADER);
	sdl.progSimple = CreateProgram(sdl.vsSimple, sdl.psSimple);

	// Vertex shader is the same
	sdl.vsPalette = LoadShader("pal.vs", GL_VERTEX_SHADER);
	sdl.psPalette = LoadShader("pal2.ps", GL_FRAGMENT_SHADER);
	sdl.progPalette = CreateProgram(sdl.vsPalette, sdl.psPalette);

	glGenTextures(1, &sdl.texPalette);
	CheckGL;

	sdl.glActiveTexture(GL_TEXTURE1);
	CheckGL;
	glBindTexture(GL_TEXTURE_2D, sdl.texPalette);
	CheckGL;
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	CheckGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	CheckGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	CheckGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
	CheckGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
	CheckGL;

	const char * gl_ext = (const char *)glGetString (GL_EXTENSIONS);
	if(gl_ext && *gl_ext)
	{
		sdl.opengl.packed_pixel=(strstr(gl_ext,"EXT_packed_pixels") > 0);
		sdl.opengl.paletted_texture=(strstr(gl_ext,"EXT_paletted_texture") > 0);
    } 
	else 
	{
		sdl.opengl.packed_pixel=sdl.opengl.paletted_texture=false;
	}
	} /* OPENGL is requested end */

#endif	//OPENGL
	/* Initialize screen for first time */
	GFX_Stop();
	SDL_SetWindowTitle(sdl.window, "DOSBox");

	GLuint splashTex;
	glGenTextures(1, &splashTex);
	CheckGL;

	if (splashTex)
	{
		sdl.glActiveTexture(GL_TEXTURE0);
		CheckGL;
		glBindTexture(GL_TEXTURE_2D, splashTex);
		CheckGL;

		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
		CheckGL;

		// We never need any Z buffer support. This frees-up lots
		// of GPU resources for us.
		//
		glDisable(GL_DEPTH_TEST);
		CheckGL;
		glDepthMask(0);
		CheckGL;
		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		CheckGL;
		//glEnable(GL_TEXTURE_2D);
		glEnable(GL_BLEND);
		CheckGL;
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		CheckGL;

		Bit8u* tmpbufp = new Bit8u[640*400*3];
		GIMP_IMAGE_RUN_LENGTH_DECODE(tmpbufp,gimp_image.rle_pixel_data,640*400,3);

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 640, 400, 0, GL_RGB, GL_UNSIGNED_BYTE, tmpbufp);
		CheckGL;
		
		sdl.glUseProgram(sdl.progSimple);
		CheckGL;
		sdl.simpleTexUnitAddr = sdl.glGetUniformLocation(sdl.progSimple, "texUnit");
		CheckGL;
		sdl.simpleFadeFactorAddr = sdl.glGetUniformLocation(sdl.progSimple, "fadeFactor");
		CheckGL;
		sdl.simplePositionAddr = sdl.glGetAttribLocation(sdl.progSimple, "position");
		CheckGL;

		sdl.glUseProgram(sdl.progPalette);
		CheckGL;
		sdl.palTexUnitAddr = sdl.glGetUniformLocation(sdl.progPalette, "texUnit");
		CheckGL;
		sdl.palPalUnitAddr = sdl.glGetUniformLocation(sdl.progPalette, "palUnit");
		CheckGL;
		sdl.palPositionAddr = sdl.glGetAttribLocation(sdl.progPalette, "position");
		CheckGL;
		sdl.palSourceSizeAddr = sdl.glGetUniformLocation(sdl.progPalette, "sourceSize");
		CheckGL;
		sdl.palOneAddr = sdl.glGetUniformLocation(sdl.progPalette, "one");
		CheckGL;
		//sdl.palTargetSizeAddr = sdl.glGetUniformLocation(sdl.progPalette, "targetSize");
		//CheckGL;
		sdl.palUvRatioAddr = sdl.glGetUniformLocation(sdl.progPalette, "uvRatio");
		CheckGL;
		sdl.palAspectFixAddr = sdl.glGetUniformLocation(sdl.progPalette, "aspectFix");
		CheckGL;

		// Now get ready to draw the logo
		//
		sdl.glUseProgram(sdl.progSimple);
		CheckGL;

		sdl.glUniform1f(sdl.simpleFadeFactorAddr, 1.0);
		CheckGL;
		sdl.glUniform1i(sdl.simpleTexUnitAddr, 0);
		CheckGL;

		sdl.glBindBuffer(GL_ARRAY_BUFFER, sdl.vertBuffer);
		CheckGL;
		sdl.glVertexAttribPointer(sdl.simplePositionAddr, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, (void*)0);
		CheckGL;
		sdl.glEnableVertexAttribArray(sdl.simplePositionAddr);
		CheckGL;

		sdl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.idxBuffer);
		CheckGL;

		delete [] tmpbufp;


		bool exit_splash = false;
		float curFadeFactor = 1.0f;
		static Bitu max_splash_loop = 1001;
		static Bitu splash_fade = 1000;
		static bool use_fadeout = true;

		for (Bit32u ct = 0,startticks = GetTicks();ct < max_splash_loop;ct = GetTicks()-startticks) {
			SDL_Event evt;
			while (SDL_PollEvent(&evt)) {
				if (evt.type == SDL_QUIT) {
					exit_splash = true;
					break;
				}
			}
			if (exit_splash) break;

			if (ct<1) 
			{
				glClear(GL_COLOR_BUFFER_BIT);
				CheckGL;
				sdl.glUniform1f(sdl.simpleFadeFactorAddr, curFadeFactor);
				CheckGL;
				sdl.glEnableVertexAttribArray(sdl.simplePositionAddr);
				CheckGL;
				glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
				CheckGL;
				sdl.glDisableVertexAttribArray(sdl.simplePositionAddr);
				CheckGL;
				SDL_GL_SwapWindow(sdl.window);
			}
			else if (ct>=max_splash_loop-splash_fade) 
			{
				if (use_fadeout) 
				{
					glClear(GL_COLOR_BUFFER_BIT);
					CheckGL;
					curFadeFactor = 1.0f - ((ct - (max_splash_loop-splash_fade)) / (float)splash_fade);
					sdl.glUniform1f(sdl.simpleFadeFactorAddr, curFadeFactor);
					CheckGL;
					sdl.glEnableVertexAttribArray(sdl.simplePositionAddr);
					CheckGL;
					glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
					CheckGL;
					sdl.glDisableVertexAttribArray(sdl.simplePositionAddr);
					CheckGL;
					SDL_GL_SwapWindow(sdl.window);
				}
			}
		}

		if (use_fadeout) 
		{
			glClear(GL_COLOR_BUFFER_BIT);
			CheckGL;
			SDL_GL_SwapWindow(sdl.window);
		}

		sdl.glUseProgram(sdl.progPalette);
		CheckGL;
		// We don't need alpha blending anymore.
		// On older hardware it will still be expensive
		//
		glDisable(GL_BLEND);
		CheckGL;
	}

	/* Get some Event handlers */
	MAPPER_AddHandler(KillSwitch,MK_f9,MMOD1,"shutdown","ShutDown");
	MAPPER_AddHandler(CaptureMouse,MK_f10,MMOD1,"capmouse","Cap Mouse");
	MAPPER_AddHandler(SwitchFullScreen,MK_return,MMOD2,"fullscr","Fullscreen");
#if C_DEBUG
	/* Pause binds with activate-debugger */
#else
	MAPPER_AddHandler(&PauseDOSBox, MK_pause, MMOD2, "pause", "Pause");
#endif
	/* Get Keyboard state of numlock and capslock */
	SDL_Keymod keystate = SDL_GetModState();
	if(keystate&KMOD_NUM) startup_state_numlock = true;
	if(keystate&KMOD_CAPS) startup_state_capslock = true;
}

void Mouse_AutoLock(bool enable) {
	sdl.mouse.autolock=enable;
	if (sdl.mouse.autoenable) sdl.mouse.requestlock=enable;
	else {
		SDL_ShowCursor(enable?SDL_DISABLE:SDL_ENABLE);
		sdl.mouse.requestlock=false;
	}
}

static void HandleMouseMotion(SDL_MouseMotionEvent * motion) {
	if (sdl.mouse.locked || !sdl.mouse.autoenable)
		Mouse_CursorMoved((float)motion->xrel*sdl.mouse.sensitivity/100.0f,
						  (float)motion->yrel*sdl.mouse.sensitivity/100.0f,
						  (float)(motion->x-sdl.clip.x)/(sdl.clip.w-1)*sdl.mouse.sensitivity/100.0f,
						  (float)(motion->y-sdl.clip.y)/(sdl.clip.h-1)*sdl.mouse.sensitivity/100.0f,
						  sdl.mouse.locked);
}

static void HandleMouseButton(SDL_MouseButtonEvent * button) {
	switch (button->state) {
	case SDL_PRESSED:
		if (sdl.mouse.requestlock && !sdl.mouse.locked) {
			GFX_CaptureMouse();
			// Dont pass klick to mouse handler
			break;
		}
		if (!sdl.mouse.autoenable && sdl.mouse.autolock && button->button == SDL_BUTTON_MIDDLE) {
			GFX_CaptureMouse();
			break;
		}
		switch (button->button) {
		case SDL_BUTTON_LEFT:
			Mouse_ButtonPressed(0);
			break;
		case SDL_BUTTON_RIGHT:
			Mouse_ButtonPressed(1);
			break;
		case SDL_BUTTON_MIDDLE:
			Mouse_ButtonPressed(2);
			break;
		}
		break;
	case SDL_RELEASED:
		switch (button->button) {
		case SDL_BUTTON_LEFT:
			Mouse_ButtonReleased(0);
			break;
		case SDL_BUTTON_RIGHT:
			Mouse_ButtonReleased(1);
			break;
		case SDL_BUTTON_MIDDLE:
			Mouse_ButtonReleased(2);
			break;
		}
		break;
	}
}

void GFX_LosingFocus(void) {
	sdl.laltstate=SDL_KEYUP;
	sdl.raltstate=SDL_KEYUP;
	MAPPER_LosingFocus();
}

void GFX_Events() {
	SDL_Event event;
#if defined (REDUCE_JOYSTICK_POLLING)
	static int poll_delay=0;
	int time=GetTicks();
	if (time-poll_delay>20) {
		poll_delay=time;
		if (sdl.num_joysticks>0) SDL_JoystickUpdate();
		MAPPER_UpdateJoysticks();
	}
#endif
	while (SDL_PollEvent(&event)) {
		switch (event.type) {
		case SDL_WINDOWEVENT:
			switch (event.window.type)
			{
				case SDL_WINDOWEVENT_FOCUS_GAINED:
				{
					if (sdl.desktop.fullscreen && !sdl.mouse.locked)
						GFX_CaptureMouse();
					SetPriority(sdl.priority.focus);
					CPU_Disable_SkipAutoAdjust();
				}
				break;
				case SDL_WINDOWEVENT_FOCUS_LOST:
				{
					if (sdl.mouse.locked) {
#ifdef WIN32
						if (sdl.desktop.fullscreen) {
							VGA_KillDrawing();
							sdl.desktop.fullscreen=false;
							GFX_ResetScreen();
						}
#endif
						GFX_CaptureMouse();
					}
					SetPriority(sdl.priority.nofocus);
					GFX_LosingFocus();
					CPU_Enable_SkipAutoAdjust();
				}
				break;
				case SDL_WINDOWEVENT_EXPOSED:
				{
					if (sdl.draw.callback) sdl.draw.callback( GFX_CallBackRedraw );
				}
				break;
			}
			/* Non-focus priority is set to pause; check to see if we've lost window or input focus
			 * i.e. has the window been minimised or made inactive?
			 */
			if (sdl.priority.nofocus == PRIORITY_LEVEL_PAUSE) {
				Uint32 sdlWindowFlags = SDL_GetWindowFlags(sdl.window);
				if ( ((sdlWindowFlags & SDL_WINDOW_INPUT_FOCUS) == 0) ||
					 ((sdlWindowFlags & SDL_WINDOW_MINIMIZED) != 0) )
				{
					/* Window has lost focus, pause the emulator.
					 * This is similar to what PauseDOSBox() does, but the exit criteria is different.
					 * Instead of waiting for the user to hit Alt-Break, we wait for the window to
					 * regain window or input focus.
					 */
					bool paused = true;
					SDL_Event ev;

					GFX_SetTitle(-1,-1,true);
					KEYBOARD_ClrBuffer();
//					SDL_Delay(500);
//					while (SDL_PollEvent(&ev)) {
						// flush event queue.
//					}

					while (paused) {
						// WaitEvent waits for an event rather than polling, so CPU usage drops to zero
						SDL_WaitEvent(&ev);

						switch (ev.type) {
						case SDL_QUIT: throw(0); break; // a bit redundant at linux at least as the active events gets before the quit event.
						case SDL_WINDOWEVENT:
						{
							switch (ev.window.type)
							{
								case SDL_WINDOWEVENT_SHOWN:
								case SDL_WINDOWEVENT_FOCUS_GAINED:
								{
								// We've got focus back, so unpause and break out of the loop
									paused = false;
									GFX_SetTitle(-1,-1,false);
								}
							}
							/* Now poke a "release ALT" command into the keyboard buffer
								* we have to do this, otherwise ALT will 'stick' and cause
								* problems with the app running in the DOSBox.
								*/
							KEYBOARD_AddKey(KBD_leftalt, false);
							KEYBOARD_AddKey(KBD_rightalt, false);
						}
						break;
					}
				}
			}
		}

		case SDL_MOUSEMOTION:
			HandleMouseMotion(&event.motion);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			HandleMouseButton(&event.button);
			break;
		case SDL_QUIT:
			throw(0);
			break;
#ifdef WIN32
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			// ignore event alt+tab
			if (event.key.keysym.sym==SDLK_LALT) sdl.laltstate = event.key.type;
			if (event.key.keysym.sym==SDLK_RALT) sdl.raltstate = event.key.type;
			if (((event.key.keysym.sym==SDLK_TAB)) &&
				((sdl.laltstate==SDL_KEYDOWN) || (sdl.raltstate==SDL_KEYDOWN))) break;
#endif
		default:
			void MAPPER_CheckEvent(SDL_Event * event);
			MAPPER_CheckEvent(&event);
		}
	}
}

#if defined (WIN32)
static BOOL WINAPI ConsoleEventHandler(DWORD event) {
	switch (event) {
	case CTRL_SHUTDOWN_EVENT:
	case CTRL_LOGOFF_EVENT:
	case CTRL_CLOSE_EVENT:
	case CTRL_BREAK_EVENT:
		raise(SIGTERM);
		return TRUE;
	case CTRL_C_EVENT:
	default: //pass to the next handler
		return FALSE;
	}
}
#endif

void Config_Add_SDL() {
	Section_prop * sdl_sec=control->AddSection_prop("sdl",&GUI_StartUp);
	sdl_sec->AddInitFunction(&MAPPER_StartUp);
	Prop_bool* Pbool;
	Prop_string* Pstring;
	Prop_int* Pint;
	Prop_multival* Pmulti;

	Pbool = sdl_sec->Add_bool("fullscreen",Property::Changeable::Always,false);
	Pbool->Set_help("Start dosbox directly in fullscreen. (Press ALT-Enter to go back)");
     
	Pbool = sdl_sec->Add_bool("fulldouble",Property::Changeable::Always,false);
	Pbool->Set_help("Use double buffering in fullscreen. It can reduce screen flickering, but it can also result in a slow DOSBox.");

	Pstring = sdl_sec->Add_string("fullresolution",Property::Changeable::Always,"original");
	Pstring->Set_help("What resolution to use for fullscreen: original or fixed size (e.g. 1024x768).\n"
	                  "  Using your monitor's native resolution with aspect=true might give the best results.\n"
			  "  If you end up with small window on a large screen, try an output different from surface.");

	Pstring = sdl_sec->Add_string("windowresolution",Property::Changeable::Always,"original");
	Pstring->Set_help("Scale the window to this size IF the output device supports hardware scaling.\n"
	                  "  (output=surface does not!)");

	const char* outputs[] = {
		"surface", "overlay",
#if C_OPENGL
		"opengl", "openglnb",
#endif
#if (HAVE_DDRAW_H) && defined(WIN32)
		"ddraw",
#endif
		0 };
	Pstring = sdl_sec->Add_string("output",Property::Changeable::Always,"surface");
	Pstring->Set_help("What video system to use for output.");
	Pstring->Set_values(outputs);

	Pbool = sdl_sec->Add_bool("autolock",Property::Changeable::Always,true);
	Pbool->Set_help("Mouse will automatically lock, if you click on the screen. (Press CTRL-F10 to unlock)");

	Pint = sdl_sec->Add_int("sensitivity",Property::Changeable::Always,100);
	Pint->SetMinMax(1,1000);
	Pint->Set_help("Mouse sensitivity.");

	Pbool = sdl_sec->Add_bool("waitonerror",Property::Changeable::Always, true);
	Pbool->Set_help("Wait before closing the console if dosbox has an error.");

	Pmulti = sdl_sec->Add_multi("priority", Property::Changeable::Always, ",");
	Pmulti->SetValue("higher,normal");
	Pmulti->Set_help("Priority levels for dosbox. Second entry behind the comma is for when dosbox is not focused/minimized.\n"
	                 "  pause is only valid for the second entry.");

	const char* actt[] = { "lowest", "lower", "normal", "higher", "highest", "pause", 0};
	Pstring = Pmulti->GetSection()->Add_string("active",Property::Changeable::Always,"higher");
	Pstring->Set_values(actt);

	const char* inactt[] = { "lowest", "lower", "normal", "higher", "highest", "pause", 0};
	Pstring = Pmulti->GetSection()->Add_string("inactive",Property::Changeable::Always,"normal");
	Pstring->Set_values(inactt);

	Pstring = sdl_sec->Add_path("mapperfile",Property::Changeable::Always,MAPPERFILE);
	Pstring->Set_help("File used to load/save the key/event mappings from. Resetmapper only works with the defaul value.");

	Pbool = sdl_sec->Add_bool("usescancodes",Property::Changeable::Always,true);
	Pbool->Set_help("Avoid usage of symkeys, might not work on all operating systems.");
}

static void show_warning(char const * const message) {
	bool textonly = true;
#ifdef WIN32
	textonly = false;
	if ( !sdl.inited && SDL_Init(SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE) < 0 ) textonly = true;
	sdl.inited = true;
#endif
	printf(message);
	if(textonly) return;
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
	Bit32u rmask = 0xff000000;
	Bit32u gmask = 0x00ff0000;
	Bit32u bmask = 0x0000ff00;
#else
	Bit32u rmask = 0x000000ff;
	Bit32u gmask = 0x0000ff00;                    
	Bit32u bmask = 0x00ff0000;
#endif
	GLuint splashTex = 0;
	glGenTextures(1, &splashTex);
	sdl.glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, splashTex);
	GLubyte *surfaceBuffer = (GLubyte*)calloc(gc_warningTexWidth * gc_warningTexHeight * gc_warningTexBytesPerPixel, sizeof(GLubyte));

	int x = 120,y = 20;
	std::string m(message),m2;
	std::string::size_type a,b,c,d;
   
	while(m.size()) { //Max 50 characters. break on space before or on a newline
		c = m.find('\n');
		d = m.rfind(' ',50);
		if(c>d) a=b=d; else a=b=c;
		if( a != std::string::npos) b++; 
		m2 = m.substr(0,a); m.erase(0,b);
		OutputStringGL(x,y,m2.c_str(),0xffffffff,0,surfaceBuffer);
		y += 20;
	}

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, gc_warningTexWidth, gc_warningTexHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, surfaceBuffer);
	CheckGL;
	free(surfaceBuffer);

	sdl.glUseProgram(sdl.progSimple);
	CheckGL;

	sdl.glUniform1f(sdl.simpleFadeFactorAddr, 1.0);
	CheckGL;
	sdl.glUniform1i(sdl.simpleTexUnitAddr, 0);
	CheckGL;

	sdl.glBindBuffer(GL_ARRAY_BUFFER, sdl.vertBuffer);
	CheckGL;
	sdl.glVertexAttribPointer(sdl.simplePositionAddr, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, (void*)0);
	CheckGL;
	sdl.glEnableVertexAttribArray(sdl.simplePositionAddr);
	CheckGL;

	sdl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.idxBuffer);
	CheckGL;

	sdl.glEnableVertexAttribArray(sdl.simplePositionAddr);
	CheckGL;
	glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
	CheckGL;
	sdl.glDisableVertexAttribArray(sdl.simplePositionAddr);
	CheckGL;
	SDL_GL_SwapWindow(sdl.window);

	SDL_Delay(12000);
}
   
static void launcheditor() {
	std::string path,file;
	Cross::CreatePlatformConfigDir(path);
	Cross::GetPlatformConfigName(file);
	path += file;
	FILE* f = fopen(path.c_str(),"r");
	if(!f && !control->PrintConfig(path.c_str())) {
		printf("tried creating %s. but failed.\n",path.c_str());
		exit(1);
	}
	if(f) fclose(f);
/*	if(edit.empty()) {
		printf("no editor specified.\n");
		exit(1);
	}*/
	std::string edit;
	while(control->cmdline->FindString("-editconf",edit,true)) //Loop until one succeeds
		execlp(edit.c_str(),edit.c_str(),path.c_str(),(char*) 0);
	//if you get here the launching failed!
	printf("can't find editor(s) specified at the command line.\n");
	exit(1);
}
static void launchcaptures(std::string const& edit) {
	std::string path,file;
	Section* t = control->GetSection("dosbox");
	if(t) file = t->GetPropValue("captures");
	if(!t || file == NO_SUCH_PROPERTY) {
		printf("Config system messed up.\n");
		exit(1);
	}
	Cross::CreatePlatformConfigDir(path);
	path += file;
	Cross::CreateDir(path);
	struct stat cstat;
	if(stat(path.c_str(),&cstat) || (cstat.st_mode & S_IFDIR) == 0) {
		printf("%s doesn't exists or isn't a directory.\n",path.c_str());
		exit(1);
	}
/*	if(edit.empty()) {
		printf("no editor specified.\n");
		exit(1);
	}*/

	execlp(edit.c_str(),edit.c_str(),path.c_str(),(char*) 0);
	//if you get here the launching failed!
	printf("can't find filemanager %s\n",edit.c_str());
	exit(1);
}

static void printconfiglocation() {
	std::string path,file;
	Cross::CreatePlatformConfigDir(path);
	Cross::GetPlatformConfigName(file);
	path += file;
     
	FILE* f = fopen(path.c_str(),"r");
	if(!f && !control->PrintConfig(path.c_str())) {
		printf("tried creating %s. but failed",path.c_str());
		exit(1);
	}
	if(f) fclose(f);
	printf("%s\n",path.c_str());
	exit(0);
}

static void eraseconfigfile() {
	FILE* f = fopen("dosbox.conf","r");
	if(f) {
		fclose(f);
		show_warning("Warning: dosbox.conf exists in current working directory.\nThis will override the configuration file at runtime.\n");
	}
	std::string path,file;
	Cross::GetPlatformConfigDir(path);
	Cross::GetPlatformConfigName(file);
	path += file;
	f = fopen(path.c_str(),"r");
	if(!f) exit(0);
	fclose(f);
	unlink(path.c_str());
	exit(0);
}

static void erasemapperfile() {
	FILE* g = fopen("dosbox.conf","r");
	if(g) {
		fclose(g);
		show_warning("Warning: dosbox.conf exists in current working directory.\nKeymapping might not be properly reset.\n"
		             "Please reset configuration as well and delete the dosbox.conf.\n");
	}

	std::string path,file=MAPPERFILE;
	Cross::GetPlatformConfigDir(path);
	path += file;
	FILE* f = fopen(path.c_str(),"r");
	if(!f) exit(0);
	fclose(f);
	unlink(path.c_str());
	exit(0);
}



//extern void UI_Init(void);
int main(int argc, char* argv[]) {
	try {
		CommandLine com_line(argc,argv);
		Config myconf(&com_line);
		control=&myconf;
		/* Init the configuration system and add default values */
		Config_Add_SDL();
		DOSBOX_Init();

		std::string editor;
		if(control->cmdline->FindString("-editconf",editor,false)) launcheditor();
		if(control->cmdline->FindString("-opencaptures",editor,true)) launchcaptures(editor);
		if(control->cmdline->FindExist("-eraseconf")) eraseconfigfile();
		if(control->cmdline->FindExist("-resetconf")) eraseconfigfile();
		if(control->cmdline->FindExist("-erasemapper")) erasemapperfile();
		if(control->cmdline->FindExist("-resetmapper")) erasemapperfile();

		/* Can't disable the console with debugger enabled */
#if defined(WIN32) && !(C_DEBUG)
		if (control->cmdline->FindExist("-noconsole")) {
			FreeConsole();
			/* Redirect standard input and standard output */
			if(freopen(STDOUT_FILE, "w", stdout) == NULL)
				no_stdout = true; // No stdout so don't write messages
			freopen(STDERR_FILE, "w", stderr);
			setvbuf(stdout, NULL, _IOLBF, BUFSIZ);	/* Line buffered */
			setbuf(stderr, NULL);					/* No buffering */
		} else {
			if (AllocConsole()) {
				fclose(stdin);
				fclose(stdout);
				fclose(stderr);
				freopen("CONIN$","r",stdin);
				freopen("CONOUT$","w",stdout);
				freopen("CONOUT$","w",stderr);
			}
			SetConsoleTitle("DOSBox Status Window");
		}
#endif  //defined(WIN32) && !(C_DEBUG)
		if (control->cmdline->FindExist("-version") ||
		    control->cmdline->FindExist("--version") ) {
			printf("\nDOSBox version %s, copyright 2002-2010 DOSBox Team.\n\n",VERSION);
			printf("DOSBox is written by the DOSBox Team (See AUTHORS file))\n");
			printf("DOSBox comes with ABSOLUTELY NO WARRANTY.  This is free software,\n");
			printf("and you are welcome to redistribute it under certain conditions;\n");
			printf("please read the COPYING file thoroughly before doing so.\n\n");
			return 0;
		}
		if(control->cmdline->FindExist("-printconf")) printconfiglocation();

#if C_DEBUG
		DEBUG_SetupConsole();
#endif

#if defined(WIN32)
	SetConsoleCtrlHandler((PHANDLER_ROUTINE) ConsoleEventHandler,TRUE);
#endif

#ifdef OS2
        PPIB pib;
        PTIB tib;
        DosGetInfoBlocks(&tib, &pib);
        if (pib->pib_ultype == 2) pib->pib_ultype = 3;
        setbuf(stdout, NULL);
        setbuf(stderr, NULL);
#endif

	/* Display Welcometext in the console */
	LOG_MSG("DOSBox version %s",VERSION);
	LOG_MSG("Copyright 2002-2010 DOSBox Team, published under GNU GPL.");
	LOG_MSG("---");

	/* Init SDL */
#if SDL_VERSION_ATLEAST(1, 2, 14)
	putenv(const_cast<char*>("SDL_DISABLE_LOCK_KEYS=1"));
#endif
	if ( SDL_Init( SDL_INIT_AUDIO|SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_EVENTS) < 0 ) 
		E_Exit("Can't init SDL %s",SDL_GetError());
	sdl.inited = true;

#ifndef DISABLE_JOYSTICK
	if( SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0 ) 
		LOG_MSG("Failed to init joystick support");
#endif

	sdl.laltstate = SDL_KEYUP;
	sdl.raltstate = SDL_KEYUP;

	sdl.num_joysticks=SDL_NumJoysticks();

	/* Parse configuration files */
	std::string config_file,config_path;
	bool parsed_anyconfigfile = false;
	//First Parse -userconf
	if(control->cmdline->FindExist("-userconf",true)){
		config_file.clear();
		Cross::GetPlatformConfigDir(config_path);
		Cross::GetPlatformConfigName(config_file);
		config_path += config_file;
		if(control->ParseConfigFile(config_path.c_str())) parsed_anyconfigfile = true;
		if(!parsed_anyconfigfile) {
			//Try to create the userlevel configfile.
			config_file.clear();
			Cross::CreatePlatformConfigDir(config_path);
			Cross::GetPlatformConfigName(config_file);
			config_path += config_file;
			if(control->PrintConfig(config_path.c_str())) {
				LOG_MSG("CONFIG: Generating default configuration.\nWriting it to %s",config_path.c_str());
				//Load them as well. Makes relative paths much easier
				if(control->ParseConfigFile(config_path.c_str())) parsed_anyconfigfile = true;
			}
		}
	}

	//Second parse -conf entries
	while(control->cmdline->FindString("-conf",config_file,true))
		if (control->ParseConfigFile(config_file.c_str())) parsed_anyconfigfile = true;

	//if none found => parse localdir conf
	config_file = "dosbox.conf";
	if (!parsed_anyconfigfile && control->ParseConfigFile(config_file.c_str())) parsed_anyconfigfile = true;

	//if none found => parse userlevel conf
	if(!parsed_anyconfigfile) {
		config_file.clear();
		Cross::GetPlatformConfigDir(config_path);
		Cross::GetPlatformConfigName(config_file);
		config_path += config_file;
		if(control->ParseConfigFile(config_path.c_str())) parsed_anyconfigfile = true;
	}

	if(!parsed_anyconfigfile) {
		//Try to create the userlevel configfile.
		config_file.clear();
		Cross::CreatePlatformConfigDir(config_path);
		Cross::GetPlatformConfigName(config_file);
		config_path += config_file;
		if(control->PrintConfig(config_path.c_str())) {
			LOG_MSG("CONFIG: Generating default configuration.\nWriting it to %s",config_path.c_str());
			//Load them as well. Makes relative paths much easier
			control->ParseConfigFile(config_path.c_str());
		} else {
			LOG_MSG("CONFIG: Using default settings. Create a configfile to change them");
		}
	}


#if (ENVIRON_LINKED)
		control->ParseEnv(environ);
#endif
//		UI_Init();
//		if (control->cmdline->FindExist("-startui")) UI_Run(false);
		/* Init all the sections */
		control->Init();
		/* Some extra SDL Functions */
		Section_prop * sdl_sec=static_cast<Section_prop *>(control->GetSection("sdl"));

		if (control->cmdline->FindExist("-fullscreen") || sdl_sec->Get_bool("fullscreen")) {
			if(!sdl.desktop.fullscreen) { //only switch if not allready in fullscreen
				GFX_SwitchFullScreen();
			}
		}

		/* Init the keyMapper */
		MAPPER_Init();
		if (control->cmdline->FindExist("-startmapper")) MAPPER_RunInternal();
		/* Start up main machine */
		control->StartUp();
		/* Shutdown everything */
	} catch (char * error) {
		GFX_ShowMsg("Exit to error: %s",error);
		fflush(NULL);
		if(sdl.wait_on_error) {
			//TODO Maybe look for some way to show message in linux?
#if (C_DEBUG)
			GFX_ShowMsg("Press enter to continue");
			fflush(NULL);
			fgetc(stdin);
#elif defined(WIN32)
			Sleep(5000);
#endif
		}

	}
	catch (int){
		;//nothing pressed killswitch
	}
	catch(...){
		SDL_SetRelativeMouseMode(SDL_FALSE);
		throw;//dunno what happened. rethrow for sdl to catch
	}
	SDL_SetRelativeMouseMode(SDL_FALSE);

	SDL_Quit();//Let's hope sdl will quit as well when it catches an exception
	return 0;
}

void GFX_GetSize(int &width, int &height, bool &fullscreen) {
	width = sdl.draw.width;
	height = sdl.draw.height;
	fullscreen = sdl.desktop.fullscreen;
}
