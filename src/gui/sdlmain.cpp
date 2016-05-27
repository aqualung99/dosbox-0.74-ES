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
#include <assert.h>

#include <sys/types.h>
#ifdef WIN32
#include <signal.h>
#include <process.h>
#endif

#include "cross.h"
#include "SDL.h"

#ifdef JOEL_REMOVED
#include <errno.h>
#endif


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
#include "hardware.h"
#include "control.h"

#define MAPPERFILE "mapper-" VERSION ".map"
//#define DISABLE_JOYSTICK


#if C_OPENGL
#ifdef WIN32
#include "SDL_opengl.h"
#include "SDL_opengl_glext.h"
#else
#include "SDL2/SDL_opengl.h"
#include "SDL2/SDL_opengl_glext.h"
#endif

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
//		bool doublebuf;
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
	//
	// SDL doesn't fill in the OpenGL 2.x prototypes for us...
	// We get all dynamic proc address for all OpenGL functions
	// so that OpenGL-ES works smoothly.
	//
    // "Newish-school" OpenGL API
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
	PFNGLDELETEBUFFERSPROC glDeleteBuffers;
	PFNGLBINDBUFFERPROC glBindBuffer;
	PFNGLBUFFERDATAPROC glBufferData;
	PFNGLDELETEPROGRAMPROC glDeleteProgram;
	PFNGLDELETESHADERPROC glDeleteShader;
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
	PFNGLGENRENDERBUFFERSPROC glGenRenderBuffers;
	PFNGLBINDRENDERBUFFERPROC glBindRenderBuffer;
	PFNGLDELETERENDERBUFFERSPROC glDeleteRenderBuffers;
	PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers;
	PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers;
	PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer;
	PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D;
	PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus;
//	PFNGLREADPIXELSPROC glReadPixels;

	GLuint vsSimple;
	GLuint psSimple;
	GLuint progSimple;

	GLuint vsPalette;
	GLuint psPalette;
	GLuint psNoPalette;
	GLuint progPalette;
	GLuint progNoPalette;
	GLuint progMainToUse;

    GLuint vsRect;
    GLuint psRect;
    GLuint progRect;

    GLuint vsFB;
    GLuint psFB;
    GLuint progFB;

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

	GLint rectPositionAddr;
	GLint rectSolidColorAddr;

	GLint fbPositionAddr;
	GLint fbTexcoordScaleAddr;
	GLint fbAspectFixAddr;
	GLint fbTexUnitAddr;
	GLint fbSourceSizeAddr;
	GLint fbOneAddr;

	GLuint texPalette;
    GLuint texBackbuffer;

    GLint originalBackbufferBinding;
    GLuint fboBackbuffer;

    bool bSmooth2Pass;
	bool use16bitTextures;

    int fboWidth, fboHeight;

	float windowAspectFor4x3;
	unsigned nextRenderLineNum;

	bool bShowPerf;

    FILE *fpcapFile;

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

	// state of alt-keys for certain special handlings
	Bit32u laltstate;
	Bit32u raltstate;
};

static SDL_Block sdl;


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

static void DeleteObjectBuffer(GLuint buffer)
{
    sdl.glDeleteBuffers(1, &buffer);
}

static int int_log2 (int val) {
    int log = 0;
    while ((val >>= 1) != 0)
	log++;
    return log;
}


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

	// LOG_MSG("Press enter key to quit...");
	char inputBuffer[1024];

	if (!no_stdout)
	{
		char *szRemoveWarning = fgets(inputBuffer, 1024, stdin);
	}
}


static void GFX_RebuildFramebufferTexture()
{
    sdl.glBindFramebuffer(GL_FRAMEBUFFER, sdl.originalBackbufferBinding);

    if (sdl.fboBackbuffer != -1)
    {
        sdl.glDeleteFramebuffers(1, &sdl.fboBackbuffer);
        CheckGL;
        sdl.fboBackbuffer = -1;
    }
    if (sdl.texBackbuffer != -1)
    {
        glDeleteTextures(1, &sdl.texBackbuffer);
        CheckGL;
        sdl.texBackbuffer = -1;
    }

    glGenTextures(1, &sdl.texBackbuffer);
    CheckGL;

//    sdl.fboWidth = 2 << int_log2(sdl.clip.w);
//    sdl.fboHeight = 2 << int_log2(sdl.clip.h);
    sdl.fboWidth = sdl.opengl.pow2TexWidth;
    sdl.fboHeight = sdl.opengl.pow2TexHeight;

    sdl.glActiveTexture(GL_TEXTURE0);
    CheckGL;

    // Regenerate the texture backing for the new
    // framebuffer
    //
    glBindTexture(GL_TEXTURE_2D, sdl.texBackbuffer);
    CheckGL;

    // Set parameters on the texture backing
    //
	if (sdl.use16bitTextures)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sdl.fboWidth, sdl.fboHeight, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, 0);
	}
	else
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, sdl.fboWidth, sdl.fboHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, 0);
	}
    CheckGL;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    CheckGL;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    CheckGL;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    CheckGL;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    CheckGL;

    // Generate a new framebuffer host
    //
    sdl.glGenFramebuffers(1, &sdl.fboBackbuffer);
    CheckGL;

    sdl.glBindFramebuffer(GL_FRAMEBUFFER, sdl.fboBackbuffer);
    CheckGL;

    // Attach the texture backing to the framebuffer host
    //
    sdl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sdl.texBackbuffer, 0);
    CheckGL;

    GLenum fboStat = sdl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (fboStat != GL_FRAMEBUFFER_COMPLETE)
    {
        LOG_MSG("Warning: Framebuffer object incomplete! (%d)", fboStat);
    }

    glViewport(0,0, sdl.draw.width + 2, sdl.draw.height + 2);
    CheckGL;
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    CheckGL;
    glClear(GL_COLOR_BUFFER_BIT);
    CheckGL;

    sdl.glBindFramebuffer(GL_FRAMEBUFFER, sdl.originalBackbufferBinding);
    CheckGL;
}


struct DrawRectData
{
    float x1, y1, x2, y2;
    float r, g, b, a;
};

extern Bit8u int10_font_14[256 * 14];

static const int MAX_RECT_BUFFER = 32;
static DrawRectData sg_rectBuffer[MAX_RECT_BUFFER];
static int sg_nextRectBuffer = 0;

static const int gc_warningTexWidth = 640;
static const int gc_warningTexHeight = 400;
static const int gc_warningFontHeight = 14;
static const int gc_warningFontWidth = 8;
static const int gc_warningTexBytesPerPixel = 4;

static const GLfloat gc_vertex_buffer_data[] = {
    -1.0f,  1.0f,
    -1.0f, -1.0f,
     1.0f,  1.0f,
     1.0f, -1.0f
};
static const GLubyte gc_element_buffer_data[] = { 0, 1, 2, 3 };
static const unsigned gc_tabSize = 8;
static unsigned char logo[32*32*4]= {
#include "dosbox_logo.h"
};
#include "dosbox_splash.h"

static GLuint sg_PerfStatsTexture = -1;
static GLubyte *sg_PerfStatsBuffer = NULL;

static unsigned sg_GFXUpdateCounter = 0;
static unsigned sg_mainLoopCount = 0;
static float sg_picTime = 0.0f, sg_cpuTime = 0.0f, sg_callbackTime = 0.0f, sg_windowTime = 0.0f, sg_timerTime = 0.0f, sg_delayTime = 0.0f;
static float sg_picAbs = 0.0f, sg_cpuAbs = 0.0f, sg_callbackAbs = 0.0f, sg_windowAbs = 0.0f, sg_timerAbs = 0.0f, sg_delayAbs = 0.0f;
static Uint64 sg_fpsCounter;
static float sg_fps = 0.0f;


static inline void OutputCharGL(Bitu x, Bitu y, char c, Bit32u colForeground, Bit32u colBackground, GLubyte *pDestBuffer)
{
	Bit32u * draw=(Bit32u*)(((Bit8u *)pDestBuffer)+((y)*(gc_warningTexWidth * gc_warningTexBytesPerPixel)))+x;

	Bit8u *font = &int10_font_14[c * 14];
	Bitu i,j;
	Bit32u * draw_line=draw;

	for (i=0;i<14;i++)
	{
		Bit8u map=*font++;
		for (j=0;j<8;j++)
		{
			if (map & 0x80)
			{
				*((Bit32u*)(draw_line+j))=colForeground;
			}
			else
			{
				*((Bit32u*)(draw_line+j))=colBackground;
			}
			map <<= 1;
		}

		draw_line += (gc_warningTexWidth * gc_warningTexBytesPerPixel) / gc_warningTexBytesPerPixel;
	}
}

void GFX_Puts(char *szBuffer, GLubyte *sfcBuffer, int startX, int startY)
{
	int nextX = startX, nextY = startY;

	while (szBuffer && (*szBuffer))
	{
		// Handle explicit line breaks
		//
		if (*szBuffer == '\n')
		{
			nextX = startX;
			nextY += gc_warningFontHeight;
		}
		else if (*szBuffer == '\t')
		{
			ldiv_t result = ldiv(nextX, gc_warningFontWidth * gc_tabSize);

			nextX += (gc_tabSize * gc_warningFontWidth) - result.rem;
		}
		else if (*szBuffer == ' ')
		{
			nextX += gc_warningFontWidth;
		}
		else
		{
			OutputCharGL(nextX, nextY, *szBuffer, 0xFFFFFFFF, 0xC0000000, sfcBuffer);
			nextX += gc_warningFontWidth;
		}

		// Auto-wrap
		//
		if (nextX > (gc_warningTexWidth - gc_warningFontWidth))
		{
			nextX = startX;
			nextY += gc_warningFontHeight;
		}

		++szBuffer;
	};
}

static void GFX_DrawRect(float x1, float y1, float x2, float y2, float r, float g, float b, float a)
{
    assert(sg_nextRectBuffer < MAX_RECT_BUFFER);

    sg_rectBuffer[sg_nextRectBuffer].x1 = x1;
    sg_rectBuffer[sg_nextRectBuffer].y1 = y1;
    sg_rectBuffer[sg_nextRectBuffer].x2 = x2;
    sg_rectBuffer[sg_nextRectBuffer].y2 = y2;
    sg_rectBuffer[sg_nextRectBuffer].r = r;
    sg_rectBuffer[sg_nextRectBuffer].g = g;
    sg_rectBuffer[sg_nextRectBuffer].b = b;
    sg_rectBuffer[sg_nextRectBuffer].a = a;

    sg_nextRectBuffer++;
}

static void GFX_DrawAllRectBuffers()
{
    static float rectVerts[8] = { 0.0f };
    GLuint vbo;

    sdl.glUseProgram(sdl.progRect);
    CheckGL;
    sdl.glGenBuffers(1, &vbo);
	CheckGL;
    sdl.glBindBuffer(GL_ARRAY_BUFFER, vbo);
    CheckGL;

    for (int i = 0; i < sg_nextRectBuffer; i++)
    {
        float denormalX1 = (sg_rectBuffer[i].x1 * 2.0f) - 1.0f;
        float denormalY1 = (sg_rectBuffer[i].y1 * 2.0f) - 1.0f;
        float denormalX2 = (sg_rectBuffer[i].x2 * 2.0f) - 1.0f;
        float denormalY2 = (sg_rectBuffer[i].y2 * 2.0f) - 1.0f;

        rectVerts[0] = denormalX1;
        rectVerts[1] = denormalY2;

        rectVerts[2] = denormalX1;
        rectVerts[3] = denormalY1;

        rectVerts[4] = denormalX2;
        rectVerts[5] = denormalY2;

        rectVerts[6] = denormalX2;
        rectVerts[7] = denormalY1;

        sdl.glBufferData(GL_VERTEX_ARRAY, sizeof(rectVerts), rectVerts, GL_DYNAMIC_DRAW);
        CheckGL;

		sdl.glEnableVertexAttribArray(sdl.rectPositionAddr);
		CheckGL;

		sdl.glUniform4f(sdl.rectSolidColorAddr, sg_rectBuffer[i].r, sg_rectBuffer[i].g, sg_rectBuffer[i].b, sg_rectBuffer[i].a);
        CheckGL;

        glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
        CheckGL;

        sdl.glDisableVertexAttribArray(sdl.rectPositionAddr);
    }

    sdl.glDeleteBuffers(1, &vbo);

    sg_nextRectBuffer = 0;
}

void GFX_DrawSimpleTexture(GLint texName);

static void ShowPerfStats()
{
	if (sdl.bShowPerf == false)
	{
		return;
	}

	if (sg_PerfStatsTexture == -1)
	{
		glGenTextures(1, &sg_PerfStatsTexture);
	}

	if (sg_PerfStatsBuffer)
	{
		memset(sg_PerfStatsBuffer, 0, gc_warningTexWidth * gc_warningTexHeight * gc_warningTexBytesPerPixel);
	}
	else
	{
		sg_PerfStatsBuffer = (GLubyte*)calloc(gc_warningTexWidth * gc_warningTexHeight * gc_warningTexBytesPerPixel, sizeof(GLubyte));
	}

	char *szLineBuffer = (char *)alloca(1024);

	sprintf(szLineBuffer, "%d Loops:\tAverage\tTotal\nPIC      :\t%0.2f\t%0.2f\nCPU      :\t%0.2f\t%0.2f\nCALLBACKS:\t%0.2f\t%0.2f\nWND EVTS :\t%0.2f\t%0.2f\nTIMERS   :\t%0.2f\t%0.2f\nDELAY    :\t%0.2f\t%0.2f\n\nFPS: %0.2f",
			sg_mainLoopCount + 1,
			sg_picTime, sg_picAbs,
			sg_cpuTime, sg_cpuAbs,
			sg_callbackTime, sg_callbackAbs,
			sg_windowTime, sg_windowAbs,
			sg_timerTime, sg_timerAbs,
			sg_delayTime, sg_delayAbs,
			sg_fps);

	GFX_Puts(szLineBuffer, sg_PerfStatsBuffer, 0, 0);

	if (sg_PerfStatsTexture != -1)
	{
		sdl.glActiveTexture(GL_TEXTURE0);
		CheckGL;
		glBindTexture(GL_TEXTURE_2D, sg_PerfStatsTexture);
		CheckGL;

		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, gc_warningTexWidth, gc_warningTexHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, sg_PerfStatsBuffer);
		CheckGL;

        GFX_DrawSimpleTexture(sg_PerfStatsTexture);
	}
}


void GFX_SetMainShader(bool bPaletted)
{
    if (bPaletted)
    {
        sdl.progMainToUse = sdl.progPalette;
    }
    else
    {
        sdl.progMainToUse = sdl.progNoPalette;
    }

    sdl.glUseProgram(sdl.progMainToUse);
    CheckGL;
    sdl.palTexUnitAddr = sdl.glGetUniformLocation(sdl.progMainToUse, "texUnit");
    CheckGL;
    sdl.palPalUnitAddr = sdl.glGetUniformLocation(sdl.progMainToUse, "palUnit");
    CheckGL;
    sdl.palPositionAddr = sdl.glGetAttribLocation(sdl.progMainToUse, "position");
    CheckGL;
    sdl.palSourceSizeAddr = sdl.glGetUniformLocation(sdl.progMainToUse, "sourceSize");
    CheckGL;
    sdl.palOneAddr = sdl.glGetUniformLocation(sdl.progMainToUse, "one");
    CheckGL;
    //sdl.palTargetSizeAddr = sdl.glGetUniformLocation(sdl.progPalette, "targetSize");
    //CheckGL;
    sdl.palUvRatioAddr = sdl.glGetUniformLocation(sdl.progMainToUse, "uvRatio");
    CheckGL;
    sdl.palAspectFixAddr = sdl.glGetUniformLocation(sdl.progMainToUse, "aspectFix");
    CheckGL;

    sdl.glVertexAttribPointer(sdl.palPositionAddr, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, (void*)0);
    CheckGL;
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
		double ratio_w=(double)fixedWidth/((sdl.draw.width*sdl.draw.scalex) + 2);
		double ratio_h=(double)fixedHeight/((sdl.draw.height*sdl.draw.scaley) + 2);
		if ( ratio_w < ratio_h) {
			sdl.clip.w=fixedWidth;
			sdl.clip.h=(Bit16u)(sdl.draw.height*sdl.draw.scaley*ratio_w) + 2;
		} else {
			sdl.clip.w=(Bit16u)(sdl.draw.width*sdl.draw.scalex*ratio_h) + 2;
			sdl.clip.h=(Bit16u)fixedHeight;
		}

		if (sdl.desktop.fullscreen)
		{
			SDL_SetWindowBordered(sdl.window, SDL_FALSE);
			SDL_SetWindowSize(sdl.window, fixedWidth, fixedHeight);
			SDL_SetWindowFullscreen(sdl.window, SDL_WINDOW_FULLSCREEN);
			sdl.clip.w = fixedWidth;
			sdl.clip.h = fixedHeight;
			sdl.windowAspectFor4x3 = (4.0f / 3.0f) / ((float)fixedWidth / fixedHeight);
		}
		else
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
	} else {
		sdl.clip.x=0;sdl.clip.y=0;

		if (sdl.desktop.fullscreen)
		{
			sdl.clip.w = (Bit16u)(sdl.draw.width*sdl.draw.scalex);
			sdl.clip.h=(Bit16u)(sdl.draw.height*sdl.draw.scaley);
		}
		else
		{
			sdl.clip.w=(Bit16u)(sdl.draw.width*sdl.draw.scalex) + 2;
			sdl.clip.h=(Bit16u)(sdl.draw.height*sdl.draw.scaley) + 2;

			// Check for non-square pixels, and adjust
			//
			float clipRatio = (float)sdl.clip.w / sdl.clip.h;
			float fourbythreeRatio = (3.0f * clipRatio) / 4.0f;
			if (clipRatio > (4.0f / 3.0f))
			{
				// Pixels are vertically stretched tall & skinny
				// We need extra height to accomodate
				//
				sdl.clip.h = (int)((sdl.clip.h * fourbythreeRatio) + 0.5f);
			}
			else if (clipRatio < (4.0f / 3.0f))
			{
				// Pixels are horizontally stretched short & fat
				// We need extra width to accomodate
				//
				sdl.clip.w = (int)((sdl.clip.w * fourbythreeRatio) + 0.5f);
			}
		}

		float clipRatio = (float)sdl.clip.w / sdl.clip.h;
		sdl.windowAspectFor4x3 = 4.0f / (3.0f * clipRatio);

		if (sdl.desktop.fullscreen)
		{
			SDL_SetWindowBordered(sdl.window, SDL_FALSE);
			SDL_SetWindowSize(sdl.window, sdl.clip.w, sdl.clip.h);
			SDL_SetWindowFullscreen(sdl.window, SDL_WINDOW_FULLSCREEN);
		}
		else
		{
			SDL_SetWindowFullscreen(sdl.window, 0);
			SDL_SetWindowBordered(sdl.window, SDL_TRUE);
			SDL_SetWindowSize(sdl.window, sdl.clip.w, sdl.clip.h);
		}
	}

	if (sdl.bSmooth2Pass)
	{
        GFX_RebuildFramebufferTexture();
	}

    return sdl.window;
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

		int texsize=2 << int_log2(width > height ? width : height);
		sdl.opengl.pow2TexWidth = 2 << int_log2(width + 2);		// One texel border on each side
		sdl.opengl.pow2TexHeight = 2 << int_log2(height + 2);	// One texel border on each side
		if (texsize>sdl.opengl.max_texsize) {
			LOG_MSG("SDL:OPENGL:No support for texturesize of %d, falling back to surface",texsize);
			goto dosurface;
		}
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );

        SDL_GL_SetSwapInterval(1);

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
		else if ((flags & GFX_LOVE_15) || (flags & GFX_LOVE_16))
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
			// Fill with blank
			// This alloc is usually 1K
			//
			GLubyte *tmpBuf = (GLubyte*)calloc(sdl.opengl.pow2TexWidth, sizeof(GLubyte));
			for (int texClear = 0; texClear < sdl.opengl.pow2TexHeight; texClear++)
			{
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, texClear, sdl.opengl.pow2TexWidth, 1, GL_LUMINANCE, GL_UNSIGNED_BYTE, tmpBuf);
			}
			free(tmpBuf);
		}
		else if (bpp == 16)
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, sdl.opengl.pow2TexWidth, sdl.opengl.pow2TexHeight, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, NULL);
			CheckGL;
			// Fill with blank
			// This alloc is usually 2K
			//
			GLubyte *tmpBuf = (GLubyte*)calloc(sdl.opengl.pow2TexWidth, sizeof(GLushort));
			for (int texClear = 0; texClear < sdl.opengl.pow2TexHeight; texClear++)
			{
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, texClear, sdl.opengl.pow2TexWidth, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, tmpBuf);
			}
			free(tmpBuf);
		}
		else if (bpp == 32)
		{
			glTexImage2D(GL_TEXTURE_2D, 0, GL_BGRA, sdl.opengl.pow2TexWidth, sdl.opengl.pow2TexHeight, 0, GL_BGRA, GL_UNSIGNED_BYTE, NULL);
			CheckGL;
			// Fill with blank
			// This alloc is usually 4K
			//
			GLubyte *tmpBuf = (GLubyte*)calloc(sdl.opengl.pow2TexWidth, sizeof(GLuint));
			for (int texClear = 0; texClear < sdl.opengl.pow2TexHeight; texClear++)
			{
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, texClear, sdl.opengl.pow2TexWidth, 1, GL_BGRA, GL_UNSIGNED_BYTE, tmpBuf);
			}
			free(tmpBuf);
		}

		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		CheckGL;
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		CheckGL;
		if (bpp == 8)
		{
            GFX_SetMainShader(true);
		}
		else
		{
            GFX_SetMainShader(false);
		}
		sdl.glUniform2f(sdl.palOneAddr, 1.0f / (width + 2), 1.0f / (height + 2));
		CheckGL;
		sdl.glUniform4f(sdl.palSourceSizeAddr, (GLfloat)width + 2, (GLfloat)height + 2, 0.0f, 0.0f);
		CheckGL;
		sdl.glUniform2f(sdl.palUvRatioAddr, (GLfloat)(width + 2) / sdl.opengl.pow2TexWidth, (GLfloat)(height + 2) / sdl.opengl.pow2TexHeight);
		CheckGL;

		// Don't do any aspect correction when we're rendering pass 1 of 2
		//
		if (sdl.bSmooth2Pass)
		{
			sdl.glUniform1f(sdl.palAspectFixAddr, 1.0f);
		}
		else
		{
			sdl.glUniform1f(sdl.palAspectFixAddr, sdl.windowAspectFor4x3);
		}
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


unsigned GFX_GetFrameCount()
{
	return sg_GFXUpdateCounter;
}


void AddToRollingAverage(float *pInOutAverage, float newVal)
{
	if (*pInOutAverage == 0.0f)
	{
		*pInOutAverage = newVal;
	}
	else
	{
		*pInOutAverage = ((*pInOutAverage) * 0.95f) + (newVal * 0.05f);
	}
}

void GFX_UpdatePerf(float picTime, float cpuTime, float callbackTime, float windowTime, float timerTime, float delayTime)
{
	AddToRollingAverage(&sg_picTime, picTime);
	AddToRollingAverage(&sg_cpuTime, cpuTime);
	AddToRollingAverage(&sg_callbackTime, callbackTime);
	AddToRollingAverage(&sg_windowTime, windowTime);
	AddToRollingAverage(&sg_timerTime, timerTime);
	AddToRollingAverage(&sg_delayTime, delayTime);

	sg_picAbs += picTime;
	sg_cpuAbs += cpuTime;
	sg_callbackAbs += callbackTime;
	sg_windowAbs += windowTime;
	sg_timerAbs += timerTime;
	sg_delayAbs += delayTime;

	sg_mainLoopCount++;
}


bool GFX_StartUpdate(Bit8u * & pixels,Bitu & pitch) {
	if (!sdl.active || sdl.updating)
		return false;

	Uint64 curTime = SDL_GetPerformanceCounter();
	Uint64 diffTime = curTime - sg_fpsCounter;
	sg_fpsCounter = curTime;
	double elapsedSecs = double(diffTime) / double(SDL_GetPerformanceFrequency());

	sg_fps = (float)(1.0 / elapsedSecs);

	sg_GFXUpdateCounter++;
	sg_mainLoopCount = 0;

	sg_picAbs = 0.0f;
	sg_cpuAbs = 0.0f;
	sg_callbackAbs = 0.0f;
	sg_windowAbs = 0.0f;
	sg_timerAbs = 0.0f;
	sg_delayAbs = 0.0f;

//    if (!(sg_GFXUpdateCounter & 0x01))
//    {
        // LOG_MSG("%.1f FPS", fps);
//        return false;
//    }

	switch (sdl.desktop.type) {
#if C_OPENGL
	case SCREEN_OPENGL:
		//pixels=(Bit8u *)sdl.opengl.framebuf;
		//pitch=sdl.opengl.pitch;
		pixels = 0;
		pitch = 0;
		// OpenGL texture coords are goofy; so we have to fill our
		// texture from the bottom up
		//
		sdl.nextRenderLineNum = sdl.draw.height;
		sdl.updating=true;

        if (sdl.bSmooth2Pass)
        {
            // Switch back to the texture render target
            //
            sdl.glBindFramebuffer(GL_FRAMEBUFFER, sdl.fboBackbuffer);
            CheckGL;
            glViewport(0,0, sdl.draw.width + 2, sdl.draw.height + 2);
            CheckGL;
        }

		glClear(GL_COLOR_BUFFER_BIT);
		sdl.glActiveTexture(GL_TEXTURE0);
		CheckGL;
		glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
		CheckGL;

/*
        if (GCC_UNLIKELY(CaptureState & (CAPTURE_IMAGE|CAPTURE_VIDEO)))
        {
            sdl.fpcapFile = fopen("screenshot.raw", "wb");
        }
*/

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

	// Write VGA line. Note we leave a 1-pixel border
	// around the edge.
	//
	glTexSubImage2D(GL_TEXTURE_2D,
					0,
					1, sdl.nextRenderLineNum--,
					sdl.draw.width, 1,
					GL_LUMINANCE, GL_UNSIGNED_BYTE,
					src);

/*
    if (sdl.fpcapFile)
    {
        fwrite(src, 1, sdl.draw.width, sdl.fpcapFile);
    }
*/
}

void GFX_LineHandler16(const void * src)
{
	sdl.glActiveTexture(GL_TEXTURE0);
	CheckGL;
    glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
	CheckGL;

	glTexSubImage2D(GL_TEXTURE_2D,
					0,
					1, sdl.nextRenderLineNum--,
					sdl.draw.width, 1,
					GL_RGB, GL_UNSIGNED_SHORT_5_6_5,
					src);

}

void GFX_LineHandler32(const void * src)
{
	sdl.glActiveTexture(GL_TEXTURE0);
	CheckGL;
    glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
	CheckGL;

	glTexSubImage2D(GL_TEXTURE_2D,
					0,
					1, sdl.nextRenderLineNum--,
					sdl.draw.width, 1,
					GL_BGRA, GL_UNSIGNED_BYTE,
					src);

}


void GFX_EndUpdate( const Bit16u *changedLines ) {
	if (!sdl.updating)
		return;
	sdl.updating=false;

/*
	if (sdl.fpcapFile)
	{
        fclose(sdl.fpcapFile);
        sdl.fpcapFile = NULL;
        CaptureState &= ~CAPTURE_IMAGE;
	}
*/

	switch (sdl.desktop.type) {
#if C_OPENGL
	case SCREEN_OPENGL:
			sdl.glActiveTexture(GL_TEXTURE0);
			CheckGL;
            glBindTexture(GL_TEXTURE_2D, sdl.opengl.texture);
            CheckGL;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            CheckGL;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            CheckGL;

			sdl.glActiveTexture(GL_TEXTURE1);
			CheckGL;
			glBindTexture(GL_TEXTURE_2D, sdl.texPalette);
			CheckGL;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            CheckGL;
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            CheckGL;

			sdl.glUniform1i(sdl.palTexUnitAddr, 0);
			CheckGL;
			if (sdl.draw.bpp == 8)
			{
                sdl.glUniform1i(sdl.palPalUnitAddr, 1);
                CheckGL;
            }

			sdl.glBindBuffer(GL_ARRAY_BUFFER, sdl.vertBuffer);
			CheckGL;
			sdl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.idxBuffer);
			CheckGL;

			// Render from our two textures (the bitmap & palette)
			// onto the current Framebuffer
			//
			sdl.glEnableVertexAttribArray(sdl.palPositionAddr);
			CheckGL;
			glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
			CheckGL;
			sdl.glDisableVertexAttribArray(sdl.palPositionAddr);
			CheckGL;

            // Draw debug rects
            //
            // GFX_DrawAllRectBuffers();

            if (sdl.bSmooth2Pass)
            {
                // Now switch the framebuffer back to the default one, and blit
                // our rendered texture
                //
                sdl.glBindFramebuffer(GL_FRAMEBUFFER, sdl.originalBackbufferBinding);
                CheckGL;
                glViewport(0,0, sdl.clip.w, sdl.clip.h);
                CheckGL;

                glClear(GL_COLOR_BUFFER_BIT);
                CheckGL;

                sdl.glActiveTexture(GL_TEXTURE0);
                CheckGL;
                glBindTexture(GL_TEXTURE_2D, sdl.texBackbuffer);
                CheckGL;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                CheckGL;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                CheckGL;

                sdl.glUseProgram(sdl.progFB);
                CheckGL;
                sdl.glUniform1i(sdl.fbTexUnitAddr, 0);
                CheckGL;
                sdl.glUniform2f(sdl.fbTexcoordScaleAddr, (GLfloat)(sdl.draw.width + 2) / sdl.fboWidth, (GLfloat)(sdl.draw.height + 2) / sdl.fboHeight);
//                sdl.glUniform2f(sdl.fbTexcoordScaleAddr, 1.0f, 1.0f);
                CheckGL;
				sdl.glUniform1f(sdl.fbAspectFixAddr, sdl.windowAspectFor4x3);
				CheckGL;
				sdl.glUniform4f(sdl.fbSourceSizeAddr, (GLfloat)(sdl.draw.width + 2), (GLfloat)(sdl.draw.height + 2), 0.0f, 0.0f);
				CheckGL;
//				sdl.glUniform2f(sdl.fbOneAddr, (GLfloat)(1.0f / (sdl.draw.width + 2)), (GLfloat)(1.0f / (sdl.draw.height)));
				sdl.glUniform2f(sdl.fbOneAddr, (GLfloat)(1.0f / sdl.fboWidth), (GLfloat)(1.0f / sdl.fboHeight));
				CheckGL;

                sdl.glBindBuffer(GL_ARRAY_BUFFER, sdl.vertBuffer);
                CheckGL;
                sdl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.idxBuffer);
                CheckGL;

                sdl.glEnableVertexAttribArray(sdl.fbPositionAddr);
                CheckGL;
                glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
                CheckGL;

/*
				// Debug code, show the source texture
				//
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                CheckGL;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                CheckGL;
                sdl.glUniform2f(sdl.fbTexcoordScaleAddr, (GLfloat)(sdl.draw.width + 2) / sdl.fboWidth, (GLfloat)(sdl.draw.height + 2) / sdl.fboHeight);
                CheckGL;
				glViewport(0, 0, sdl.draw.width + 2, sdl.draw.height + 2);
				CheckGL;
                glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
                CheckGL;
*/
				sdl.glDisableVertexAttribArray(sdl.fbPositionAddr);
                CheckGL;
			}
			else
			{
                sdl.glActiveTexture(GL_TEXTURE0);
                CheckGL;
			}

			ShowPerfStats();

			if (sdl.draw.bpp == 8)
			{
				GFX_SetMainShader(true);
			}
			else
			{
				GFX_SetMainShader(false);
			}

			SDL_GL_SwapWindow(sdl.window);

		break;
#endif	// C_OPENGL
	default:
		break;
	}
}

// static FILE *sg_palFile = NULL;
// static void *sg_palWriteThroughCache = NULL;

void GFX_SetPalette(Bitu start,Bitu count,GFX_PalEntry * entries) {
/*
    if (sg_palFile == NULL)
    {
        sg_palFile = fopen("/home/odroid/pal_dump.txt", "w");
    }

    if (sg_palFile)
    {
        fprintf(sg_palFile, "Frame %d received %d palette entries (%d - %d):\n", sg_GFXUpdateCounter, count, start, start + count);
        for (int i = 0; i < count; i++)
        {
            fprintf(sg_palFile, "\t%d: (%d, %d, %d)\n", i + start, entries[i].r, entries[i].g, entries[i].b);
            fflush(sg_palFile);
        }
    }
    if (sg_palWriteThroughCache == NULL)
    {
        if (sdl.use16bitTextures)
        {
            sg_palWriteThroughCache = malloc(256 * sizeof(GLushort));
        }
        else
        {
            sg_palWriteThroughCache = malloc(256 * sizeof(GFX_PalEntry));
        }
    }
*/

	// The palette is just a 1-dimensional texture.
	// It's the 2nd texture, stored at index 1
	//
	sdl.glActiveTexture(GL_TEXTURE1);
	CheckGL;
	glBindTexture(GL_TEXTURE_2D, sdl.texPalette);
	CheckGL;

	if (sdl.use16bitTextures)
	{
//		GLushort *pTempBuf = ((GLushort*)sg_palWriteThroughCache) + start;
		GLushort *pTempBuf = (GLushort*)alloca(count * sizeof(GLushort));

		for (unsigned i = 0; i < count; i++)
		{
			// We are losing one bit of Red & Blue color from native VGA.
			// Not great (since we only have 6 bits to start with)
			// but not the end of the world either.
			//
			pTempBuf[i] = ((GLushort)(entries[i].r >> 3) << 11) |
						  ((GLushort)(entries[i].g >> 2) << 5)  |
						  ((GLushort)(entries[i].b >> 3) << 0);
		}

		glTexSubImage2D(GL_TEXTURE_2D, 0, start, 0, count, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, pTempBuf);
//        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, sg_palWriteThroughCache);
	}
	else
	{
//        memcpy(((GLuint*)sg_palWriteThroughCache) + start, entries, count * sizeof(GFX_PalEntry));
//        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char*)sg_palWriteThroughCache);
		glTexSubImage2D(GL_TEXTURE_2D, 0, start, 0, count, 1, GL_RGBA, GL_UNSIGNED_BYTE, (unsigned char*)entries);
	}
	CheckGL;
}

/*
Bitu GFX_GetRGB(Bit8u red,Bit8u green,Bit8u blue) {
	switch (sdl.desktop.type) {
	case SCREEN_OPENGL:
		return ((red << 0) | (green << 8) | (blue << 16)) | (255 << 24);
		//USE BGRA
//		return ((blue << 0) | (green << 8) | (red << 16)) | (255 << 24);
	}
	return 0;
}
*/

void GFX_Stop() {
	if (sdl.updating)
		GFX_EndUpdate( 0 );
	sdl.active=false;
}

void GFX_Start() {
	sdl.active=true;

	if (sdl.window && sdl.context)
	{
        SDL_GL_MakeCurrent(sdl.window, sdl.context);
        glViewport(0, 0, sdl.clip.w, sdl.clip.h);
        CheckGL;
	}
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
		if (sdl.progFB)
        {
            sdl.glDeleteProgram(sdl.progFB);
        }
		if (sdl.progNoPalette)
        {
            sdl.glDeleteProgram(sdl.progNoPalette);
        }
        if (sdl.psFB)
        {
            sdl.glDeleteShader(sdl.psFB);
        }
        if (sdl.psNoPalette)
        {
            sdl.glDeleteShader(sdl.psNoPalette);
        }
        if (sdl.psPalette)
        {
            sdl.glDeleteShader(sdl.psPalette);
        }
        if (sdl.psSimple)
        {
            sdl.glDeleteShader(sdl.psSimple);
        }
        if (sdl.vsFB)
        {
            sdl.glDeleteShader(sdl.vsFB);
        }
        if (sdl.vsPalette)
        {
            sdl.glDeleteShader(sdl.vsPalette);
        }
        if (sdl.vsSimple)
        {
            sdl.glDeleteShader(sdl.vsSimple);
        }
		if (sdl.texPalette)
		{
			glDeleteTextures(1, &sdl.texPalette);
		}
		if (sdl.opengl.texture)
		{
			glDeleteTextures(1, &sdl.opengl.texture);
		}
		if (sdl.texBackbuffer)
        {
            glDeleteTextures(1, &sdl.texBackbuffer);
        }
        if (sg_PerfStatsTexture != -1)
        {
            glDeleteTextures(1, &sg_PerfStatsTexture);
        }
        if (sdl.fboBackbuffer)
        {
            sdl.glDeleteFramebuffers(1, &sdl.fboBackbuffer);
        }
        if (sdl.vertBuffer)
        {
            sdl.glDeleteBuffers(1, &sdl.vertBuffer);
        }
        if (sdl.idxBuffer)
        {
            sdl.glDeleteBuffers(1, &sdl.idxBuffer);
        }
		if (sdl.context)
		{
			SDL_GL_DeleteContext(sdl.context);
		}
		SDL_DestroyWindow(sdl.window);
	}

/*
    if (sg_palFile)
    {
        fclose(sg_palFile);
    }
*/
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
		LOG_MSG("Linking: %s", szLogBuffer);
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
		strcpy(progSummary, "Shaders linked successfully.\tAttributes found:\n");

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

		strcat(progSummary, "\t\t\t\tUniforms found:\n");
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


inline const char *os_separator()
{
#if defined _WIN32 || defined __CYGWIN__
    return "\\";
#else
    return "/";
#endif
}

#if defined (WIN32)
const char* sg_pathToShaders = ".\\Shaders\\";
#else
const char* sg_pathToShaders = "/usr/local/share/dosbox/";
#endif // defined


static GLuint LoadShader(const char *szShaderFile, GLenum shaderType)
{
	FILE *fp = fopen(szShaderFile, "r");
	if (!fp)
	{
		//perror(szBuffer);
		const char *szErr = strerror(errno);
		LOG_MSG("Could not open file \"%s\"\nError was: %s", szShaderFile, szErr);
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
		char *szUnused = fgets(lineBuffer, 64 * 1024, fp);

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
		LOG_MSG("Compiling \"%s\": %s", szShaderFile, szLogBuffer);
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

	if (compileResult == GL_TRUE)
	{
		return shaderProg;
	}
	else
	{
		return 0;
	}
}


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
//	sdl.desktop.doublebuf=section->Get_bool("fulldouble");
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
	sdl.use16bitTextures = section->Get_bool("use_16bit_textures");
	sdl.bShowPerf = section->Get_bool("show_perf_counters");
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
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_ACCELERATED_VISUAL, 1);
//	SDL_GL_SetAttribute(SDL_GL_SHAREDCONTEXT, 1);

	sdl.window = SDL_CreateWindow("DosBox", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, 640, 400, SDL_WINDOW_OPENGL);
	// SDL_CreateWindowAndRenderer(640, 480, SDL_WINDOW_OPENGL, &sdl.window, &sdl.renderer);

	sdl.context = SDL_GL_CreateContext(sdl.window);

	glGetIntegerv (GL_MAX_TEXTURE_SIZE, &sdl.opengl.max_texsize);
	if (!sdl.opengl.max_texsize)
	{
        // If the EGL driver didn't help us out by telling us,
        // then assume it's 1024. That should be the minimum, unless
        // the device is very old.
        //
        sdl.opengl.max_texsize = 1024;
	}
	CheckGL;

    LOG_MSG("OpenGL-ES\nVendor: %s\nRenderer: %s\nVersion: %s\nShader Version: %s", glGetString(GL_VENDOR), glGetString(GL_RENDERER), glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
//    fprintf(stdout, "Extentions: %s\r\n", glGetString(GL_EXTENSIONS));

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
	sdl.glDeleteBuffers = (PFNGLDELETEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteBuffers");
	sdl.glBindBuffer = (PFNGLBINDBUFFERPROC)SDL_GL_GetProcAddress("glBindBuffer");
	sdl.glBufferData = (PFNGLBUFFERDATAPROC)SDL_GL_GetProcAddress("glBufferData");
	sdl.glDeleteProgram = (PFNGLDELETEPROGRAMPROC)SDL_GL_GetProcAddress("glDeleteProgram");
	sdl.glDeleteShader = (PFNGLDELETESHADERPROC)SDL_GL_GetProcAddress("glDeleteShader");
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
//	sdl.glGenRenderBuffers = (PFNGLGENRENDERBUFFERSPROC)SDL_GL_GetProcAddress("glGenRenderBuffers");
//	sdl.glBindRenderBuffer = (PFNGLBINDRENDERBUFFERPROC)SDL_GL_GetProcAddress("glBindRenderBuffer");
//	sdl.glDeleteRenderBuffers = (PFNGLDELETERENDERBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteRenderBuffers");
    sdl.glGenFramebuffers = (PFNGLGENFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glGenFramebuffers");
    sdl.glDeleteFramebuffers = (PFNGLDELETEFRAMEBUFFERSPROC)SDL_GL_GetProcAddress("glDeleteFramebuffers");
    sdl.glBindFramebuffer = (PFNGLBINDFRAMEBUFFERPROC)SDL_GL_GetProcAddress("glBindFramebuffer");
    sdl.glFramebufferTexture2D = (PFNGLFRAMEBUFFERTEXTURE2DPROC)SDL_GL_GetProcAddress("glFramebufferTexture2D");
    sdl.glCheckFramebufferStatus = (PFNGLCHECKFRAMEBUFFERSTATUSPROC)SDL_GL_GetProcAddress("glCheckFramebufferStatus");
//    sdl.glReadPixels = (PFNGLREADPIXELSPROC)SDL_GL_GetProcAddress("glReadPixels");

	sdl.vertBuffer = CreateObjectBuffer(GL_ARRAY_BUFFER, gc_vertex_buffer_data, sizeof(gc_vertex_buffer_data));
	CheckGL;
	sdl.idxBuffer = CreateObjectBuffer(GL_ELEMENT_ARRAY_BUFFER, gc_element_buffer_data, sizeof(gc_element_buffer_data));
	CheckGL;

	char *shaderPathBuf = (char*)alloca(16 * 1024);
    strcpy(shaderPathBuf, sg_pathToShaders);
    strcat(shaderPathBuf, "logo.vs");

	sdl.vsSimple = LoadShader(shaderPathBuf, GL_VERTEX_SHADER);

    strcpy(shaderPathBuf, sg_pathToShaders);
    strcat(shaderPathBuf, "logo.ps");

	sdl.psSimple = LoadShader(shaderPathBuf, GL_FRAGMENT_SHADER);
	sdl.progSimple = CreateProgram(sdl.vsSimple, sdl.psSimple);

    if (sdl.progSimple < 0)
    {
        return;
    }

	sdl.vsPalette = LoadShader(section->Get_string("vertex_shader"), GL_VERTEX_SHADER);
	sdl.psPalette = LoadShader(section->Get_string("pixel_shader"), GL_FRAGMENT_SHADER);
	sdl.psNoPalette = LoadShader(section->Get_string("pixel_shader_no_palette"), GL_FRAGMENT_SHADER);
	sdl.progPalette = CreateProgram(sdl.vsPalette, sdl.psPalette);
	sdl.progNoPalette = CreateProgram(sdl.vsPalette, sdl.psNoPalette);
	sdl.progMainToUse = sdl.progPalette;

    if (sdl.progPalette < 0)
    {
        return;
    }

//	sdl.vsRect = LoadShader("solidRect.vs", GL_VERTEX_SHADER);
//	sdl.psRect = LoadShader("solidRect.ps", GL_FRAGMENT_SHADER);
//	sdl.progRect = CreateProgram(sdl.vsRect, sdl.psRect);

    sdl.vsFB = LoadShader(section->Get_string("vertex_shader_pass2"), GL_VERTEX_SHADER);
    sdl.psFB = LoadShader(section->Get_string("pixel_shader_pass2"), GL_FRAGMENT_SHADER);
    sdl.progFB = CreateProgram(sdl.vsFB, sdl.psFB);

    if (sdl.progFB < 0)
    {
        throw 0;
    }

	glGenTextures(1, &sdl.texPalette);
	CheckGL;

	sdl.glActiveTexture(GL_TEXTURE1);
	CheckGL;
	glBindTexture(GL_TEXTURE_2D, sdl.texPalette);
	CheckGL;
	Bit32u *rgBlackPal = (Bit32u*)alloca(256 * sizeof(Bit32u));
	memset(rgBlackPal, 0, 256 * sizeof(Bit32u));
	if (sdl.use16bitTextures)
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, 256, 1, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, rgBlackPal);
	}
	else
	{
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgBlackPal);
	}
	CheckGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	CheckGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	CheckGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
	CheckGL;
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
	CheckGL;

    sdl.texBackbuffer = -1;
    sdl.fboBackbuffer = -1;
    sdl.bSmooth2Pass = section->Get_bool("perform_2pass");
    if (sdl.bSmooth2Pass)
    {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &sdl.originalBackbufferBinding);
        CheckGL;
    }

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
		// of GPU resources for us (one would think.)
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

		sdl.glVertexAttribPointer(sdl.simplePositionAddr, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, (void*)0);
		CheckGL;

        /*
        sdl.glUseProgram(sdl.progRect);
        CheckGL;
        sdl.rectPositionAddr = sdl.glGetAttribLocation(sdl.progRect, "position");
        CheckGL;
        sdl.rectSolidColorAddr = sdl.glGetUniformLocation(sdl.progRect, "solidColor");
        CheckGL;
		sdl.glVertexAttribPointer(sdl.rectPositionAddr, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, (void*)0);
		CheckGL;
        */
        sdl.glUseProgram(sdl.progFB);
        CheckGL;

        sdl.fbPositionAddr = sdl.glGetAttribLocation(sdl.progFB, "position");
        CheckGL;
        sdl.fbTexcoordScaleAddr = sdl.glGetUniformLocation(sdl.progFB, "texcoordScale");
        CheckGL;
		sdl.fbAspectFixAddr = sdl.glGetUniformLocation(sdl.progFB, "aspectFix");
		CheckGL;
		sdl.fbSourceSizeAddr = sdl.glGetUniformLocation(sdl.progFB, "sourceSize");
		CheckGL;
		sdl.fbOneAddr = sdl.glGetUniformLocation(sdl.progFB, "one");
		CheckGL;
        sdl.fbTexUnitAddr = sdl.glGetUniformLocation(sdl.progFB, "texUnit");
        CheckGL;

        sdl.glVertexAttribPointer(sdl.fbPositionAddr, 2, GL_FLOAT, GL_FALSE, sizeof(GLfloat) * 2, (void*)0);
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
		sdl.glEnableVertexAttribArray(sdl.simplePositionAddr);
		CheckGL;

		sdl.glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sdl.idxBuffer);
		CheckGL;

		delete [] tmpbufp;


		bool exit_splash = false;
		float curFadeFactor = 1.0f;
		static Bitu max_splash_loop = 3000;
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

		// We don't need alpha blending anymore.
		// On older hardware it will still be expensive
		//
		glDisable(GL_BLEND);
		CheckGL;

		glDeleteTextures(1, &splashTex);
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

SDL_Window *GetInputWindow()
{
    return sdl.window;
}


void Config_Add_SDL() {
	Section_prop * sdl_sec=control->AddSection_prop("sdl",&GUI_StartUp);
	sdl_sec->AddInitFunction(&MAPPER_StartUp);
	Prop_bool* Pbool;
	Prop_string* Pstring;
	Prop_int* Pint;
	Prop_multival* Pmulti;

	Pbool = sdl_sec->Add_bool("fullscreen",Property::Changeable::DBoxAlways,false);
	Pbool->Set_help("Start dosbox directly in fullscreen. (Press ALT-Enter to go back)");

//	Pbool = sdl_sec->Add_bool("fulldouble",Property::Changeable::DBoxAlways,false);
//	Pbool->Set_help("Use double buffering in fullscreen. It can reduce screen flickering, but it can also result in a slow DOSBox.");

	Pstring = sdl_sec->Add_string("fullresolution",Property::Changeable::DBoxAlways,"original");
	Pstring->Set_help("What resolution to use for fullscreen: original or fixed size (e.g. 1024x768).\n"
	                  "  Using your monitor's native resolution with aspect=true might give the best results.\n"
			  "  If you end up with small window on a large screen, try an output different from surface.");

	Pstring = sdl_sec->Add_string("windowresolution",Property::Changeable::DBoxAlways,"original");
	Pstring->Set_help("Scale the window to this size IF the output device supports hardware scaling.\n"
	                  "  (output=surface does not!)");

	const char* outputs[] = {
#if C_OPENGL
		"opengl", "opengles",
#endif
		0 };
	Pstring = sdl_sec->Add_string("output",Property::Changeable::DBoxAlways,"opengles");
	Pstring->Set_help("What video system to use for output.");
	Pstring->Set_values(outputs);

	Pbool = sdl_sec->Add_bool("autolock",Property::Changeable::DBoxAlways,true);
	Pbool->Set_help("Mouse will automatically lock, if you click on the screen. (Press CTRL-F10 to unlock)");

	Pint = sdl_sec->Add_int("sensitivity",Property::Changeable::DBoxAlways,100);
	Pint->SetMinMax(1,1000);
	Pint->Set_help("Mouse sensitivity.");

	Pbool = sdl_sec->Add_bool("waitonerror",Property::Changeable::DBoxAlways, true);
	Pbool->Set_help("Wait before closing the console if dosbox has an error.");

	Pmulti = sdl_sec->Add_multi("priority", Property::Changeable::DBoxAlways, ",");
	Pmulti->SetValue("higher,normal");
	Pmulti->Set_help("Priority levels for dosbox. Second entry behind the comma is for when dosbox is not focused/minimized.\n"
	                 "  pause is only valid for the second entry.");

	const char* actt[] = { "lowest", "lower", "normal", "higher", "highest", "pause", 0};
	Pstring = Pmulti->GetSection()->Add_string("active",Property::Changeable::DBoxAlways,"higher");
	Pstring->Set_values(actt);

	const char* inactt[] = { "lowest", "lower", "normal", "higher", "highest", "pause", 0};
	Pstring = Pmulti->GetSection()->Add_string("inactive",Property::Changeable::DBoxAlways,"normal");
	Pstring->Set_values(inactt);

	Pstring = sdl_sec->Add_path("mapperfile",Property::Changeable::DBoxAlways,MAPPERFILE);
	Pstring->Set_help("File used to load/save the key/event mappings from. Resetmapper only works with the defaul value.");

    char *shaderPathBuf = (char*)alloca(16 * 1024);

    strcpy(shaderPathBuf, sg_pathToShaders);
    strcat(shaderPathBuf, "DosBoxMain.vs");

    Pstring = sdl_sec->Add_path("vertex_shader", Property::Changeable::OnlyAtStart, shaderPathBuf);
    Pstring->Set_help("File used as the vertex shader for the main renderer. Should be in the OpenGL-ES Shading Language. You shouldn't need to change this.");

    strcpy(shaderPathBuf, sg_pathToShaders);
    strcat(shaderPathBuf, "DosBoxMain.ps");

    Pstring = sdl_sec->Add_path("pixel_shader", Property::Changeable::OnlyAtStart, shaderPathBuf);
    Pstring->Set_help("File used as the fragment shader for the main renderer. Should be in the OpenGL-ES Shading Language. Change this to make it better.");

    strcpy(shaderPathBuf, sg_pathToShaders);
    strcat(shaderPathBuf, "DosBoxNoPal.ps");

    Pstring = sdl_sec->Add_path("pixel_shader_no_palette", Property::Changeable::OnlyAtStart, shaderPathBuf);
    Pstring->Set_help("File used as the fragment shader for the main renderer when DOS is in a 16- or 32-bit color mode (very rare...only program I know that uses it is FRACTINT.) In this mode, no palette is used which actually makes it slightly faster.");

    strcpy(shaderPathBuf, sg_pathToShaders);
    strcat(shaderPathBuf, "fb.vs");

    Pstring = sdl_sec->Add_path("vertex_shader_pass2", Property::Changeable::OnlyAtStart, shaderPathBuf);
    Pstring->Set_help("File used as the vertex shader for the 2nd-pass renderer. Only used when 2nd pass is enabled.");

    strcpy(shaderPathBuf, sg_pathToShaders);
    strcat(shaderPathBuf, "fb.ps");

    Pstring = sdl_sec->Add_path("pixel_shader_pass2", Property::Changeable::OnlyAtStart, shaderPathBuf);
    Pstring->Set_help("File used as the fragment shader for the 2nd-pass renderer. Only used when 2nd pass is enabled. Bilinear filtering is enabled during pass 2 (unlike during pass 1.)");

    Pbool = sdl_sec->Add_bool("perform_2pass", Property::Changeable::OnlyAtStart, true);
    Pbool->Set_help("Instead of rendering the native image directly to the framebuffer, render to a texture first. Then use bilinear interpolation to render that texture to the screen. If you aren't using a CRT shader, you probably want to set this to true. If you are using a CRT shader, you probably want to set this to false.");

	Pbool = sdl_sec->Add_bool("use_16bit_textures", Property::Changeable::OnlyAtStart, false);
	Pbool->Set_help("Use 16-bit textures (instead of 32-bit) where possible. Higher performance at the cost of slightly reduced color fidelity.");

	Pbool = sdl_sec->Add_bool("show_perf_counters", Property::Changeable::DBoxAlways, false);
	Pbool->Set_help("Show debug overlay with performance stats.");
/*
	Pbool = sdl_sec->Add_bool("usescancodes",Property::Changeable::DBoxAlways,true);
	Pbool->Set_help("Avoid usage of symkeys, might not work on all operating systems.");
*/
}


static void show_warning(char const * const message) {
	bool textonly = true;
#ifdef WIN32
	textonly = false;
	if ( !sdl.inited && SDL_Init(SDL_INIT_VIDEO|SDL_INIT_NOPARACHUTE) < 0 ) textonly = true;
	sdl.inited = true;
#endif
	puts(message);
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

SDL_GLContext GFX_GetGLContext()
{
    return sdl.context;
}


void GFX_DrawSimpleTexture(GLint texName)
{
    sdl.glActiveTexture(GL_TEXTURE0);
    CheckGL;
    glBindTexture(GL_TEXTURE_2D, texName);
    CheckGL;

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    CheckGL;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    CheckGL;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,     GL_CLAMP_TO_EDGE);
    CheckGL;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,     GL_CLAMP_TO_EDGE);
    CheckGL;
    glEnable(GL_BLEND);
    CheckGL;
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    CheckGL;
    sdl.glUseProgram(sdl.progSimple);
    CheckGL;

    sdl.glUniform1f(sdl.simpleFadeFactorAddr, 1.0);
    CheckGL;
    sdl.glUniform1i(sdl.simpleTexUnitAddr, 0);
    CheckGL;

    sdl.glEnableVertexAttribArray(sdl.simplePositionAddr);
    CheckGL;

    glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_BYTE, (void*)0);
    CheckGL;

    sdl.glDisableVertexAttribArray(sdl.simplePositionAddr);
    CheckGL;

    glDisable(GL_BLEND);
    CheckGL;
}
