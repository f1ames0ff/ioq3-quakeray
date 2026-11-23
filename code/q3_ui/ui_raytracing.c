/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
//
/*
=======================================================================

RAY TRACING OPTIONS MENU

=======================================================================
*/

#include "ui_local.h"

#define ART_FRAMEL			"menu/art/frame2_l"
#define ART_FRAMER			"menu/art/frame1_r"
#define ART_BACK0			"menu/art/back_0"
#define ART_BACK1			"menu/art/back_1"
#define ART_ACCEPT0			"menu/art/accept_0"
#define ART_ACCEPT1			"menu/art/accept_1"

#define RAYTRACING_X_POS	400

#define ID_GRAPHICS			10
#define ID_RAYTRACING		11
#define ID_DISPLAY			12
#define ID_SOUND			13
#define ID_NETWORK			14
#define ID_RENDERER			15
#define ID_UPSCALE			16
#define ID_TRUELIGHT		17
#define ID_GI_LEVEL			18
#define ID_DENOISER			19
#define ID_PHYSICAL_SKY		20
#define ID_CLOUDS			21
#define ID_GOD_RAYS			22
#define ID_TONEMAP			23
#define ID_BACK				24
#define ID_APPLY			25

typedef struct {
	menuframework_s		menu;

	menutext_s			banner;
	menubitmap_s		framel;
	menubitmap_s		framer;

	menutext_s			graphics;
	menutext_s			raytracing;
	menutext_s			display;
	menutext_s			sound;
	menutext_s			network;

	menulist_s			renderer;
	menulist_s			upscale;
	menulist_s			truelight;
	menulist_s			gilevel;
	menuradiobutton_s	denoiser;
	menuradiobutton_s	physicalSky;
	menuradiobutton_s	clouds;
	menuradiobutton_s	godRays;
	menulist_s			tonemap;

	menubitmap_s		back;
	menubitmap_s		apply;

	int					initialRenderer;
} raytracingOptions_t;

static raytracingOptions_t s_raytracing;

static const char *renderer_names[] = {
	"QuakeRay",
	"OpenGL2",
	"OpenGL1",
	NULL
};

static const char *renderer_cvars[] = {
	"qray",
	"opengl2",
	"opengl1",
	NULL
};

static const char *upscale_names[] = {
	"Off",
	"FSR Native AA",
	"FSR Quality",
	"FSR Balanced",
	"FSR Performance",
	NULL
};

static const char *truelight_names[] = {
	"Legacy",
	"TrueLight",
	"TrueLight Pure",
	NULL
};

static const char *gilevel_names[] = {
	"Off",
	"1 Bounce",
	"2 Bounces",
	NULL
};

static const char *tonemap_names[] = {
	"None",
	"Reinhard",
	"Ext Reinhard",
	"ACES",
	"AgX",
	NULL
};

/*
=================
RayTracing_GetCvarInt
=================
*/
static int RayTracing_GetCvarInt( const char *name, int fallback, int minVal, int maxVal ) {
	const char *val;

	val = UI_Cvar_VariableString( name );
	if ( !val || !val[0] ) {
		return fallback;
	}
	return (int)Com_Clamp( (float)minVal, (float)maxVal, trap_Cvar_VariableValue( name ) );
}

/*
=================
RayTracing_UpdateMenuItems
=================
*/
static void RayTracing_UpdateMenuItems( void ) {
	if ( s_raytracing.renderer.curvalue != s_raytracing.initialRenderer ) {
		s_raytracing.apply.generic.flags &= ~( QMF_HIDDEN | QMF_INACTIVE );
	} else {
		s_raytracing.apply.generic.flags |= ( QMF_HIDDEN | QMF_INACTIVE );
	}
}

/*
=================
RayTracing_SetMenuItems
=================
*/
static void RayTracing_SetMenuItems( void ) {
	const char *curRenderer;
	int i;

	curRenderer = UI_Cvar_VariableString( "cl_renderer" );
	s_raytracing.initialRenderer = 0;
	for ( i = 0; renderer_cvars[i] != NULL; i++ ) {
		if ( Q_stricmp( curRenderer, renderer_cvars[i] ) == 0 ) {
			s_raytracing.initialRenderer = i;
			break;
		}
	}
	s_raytracing.renderer.curvalue		= s_raytracing.initialRenderer;

	s_raytracing.upscale.curvalue		= RayTracing_GetCvarInt( "qray_upscale", 1, 0, 4 );
	s_raytracing.truelight.curvalue		= RayTracing_GetCvarInt( "qray_truelight", 0, 0, 2 );
	s_raytracing.gilevel.curvalue		= RayTracing_GetCvarInt( "qray_giLevel", 1, 0, 2 );
	s_raytracing.denoiser.curvalue		= RayTracing_GetCvarInt( "qray_denoiser", 1, 0, 1 );
	s_raytracing.physicalSky.curvalue	= RayTracing_GetCvarInt( "qray_physicalSky", 1, 0, 1 );
	s_raytracing.clouds.curvalue		= RayTracing_GetCvarInt( "qray_clouds", 1, 0, 1 );
	s_raytracing.godRays.curvalue		= RayTracing_GetCvarInt( "qray_godRays", 1, 0, 1 );
	s_raytracing.tonemap.curvalue		= RayTracing_GetCvarInt( "qray_tonemapType", 4, 0, 4 );

	RayTracing_UpdateMenuItems();
}

/*
=================
RayTracing_Event
=================
*/
static void RayTracing_Event( void *ptr, int notification ) {
	if ( notification != QM_ACTIVATED ) {
		return;
	}

	switch ( ((menucommon_s *)ptr)->id ) {
	case ID_GRAPHICS:
		UI_PopMenu();
		UI_GraphicsOptionsMenu();
		break;

	case ID_RAYTRACING:
		break;

	case ID_DISPLAY:
		UI_PopMenu();
		UI_DisplayOptionsMenu();
		break;

	case ID_SOUND:
		UI_PopMenu();
		UI_SoundOptionsMenu();
		break;

	case ID_NETWORK:
		UI_PopMenu();
		UI_NetworkOptionsMenu();
		break;

	case ID_RENDERER:
		RayTracing_UpdateMenuItems();
		break;

	case ID_UPSCALE:
		trap_Cvar_SetValue( "qray_upscale", s_raytracing.upscale.curvalue );
		break;

	case ID_TRUELIGHT:
		trap_Cvar_SetValue( "qray_truelight", s_raytracing.truelight.curvalue );
		break;

	case ID_GI_LEVEL:
		trap_Cvar_SetValue( "qray_giLevel", s_raytracing.gilevel.curvalue );
		break;

	case ID_DENOISER:
		trap_Cvar_SetValue( "qray_denoiser", s_raytracing.denoiser.curvalue );
		break;

	case ID_PHYSICAL_SKY:
		trap_Cvar_SetValue( "qray_physicalSky", s_raytracing.physicalSky.curvalue );
		break;

	case ID_CLOUDS:
		trap_Cvar_SetValue( "qray_clouds", s_raytracing.clouds.curvalue );
		break;

	case ID_GOD_RAYS:
		trap_Cvar_SetValue( "qray_godRays", s_raytracing.godRays.curvalue );
		break;

	case ID_TONEMAP:
		trap_Cvar_SetValue( "qray_tonemapType", s_raytracing.tonemap.curvalue );
		break;

	case ID_APPLY:
		trap_Cvar_Set( "cl_renderer", renderer_cvars[s_raytracing.renderer.curvalue] );
		trap_Cmd_ExecuteText( EXEC_APPEND, "vid_restart\n" );
		break;

	case ID_BACK:
		UI_PopMenu();
		break;
	}
}

/*
=================
RayTracing_MenuInit
=================
*/
static void RayTracing_MenuInit( void ) {
	int y;

	memset( &s_raytracing, 0, sizeof(raytracingOptions_t) );

	UI_RayTracingOptionsMenu_Cache();

	s_raytracing.menu.wrapAround = qtrue;
	s_raytracing.menu.fullscreen = qtrue;

	s_raytracing.banner.generic.type	= MTYPE_BTEXT;
	s_raytracing.banner.generic.flags	= QMF_CENTER_JUSTIFY;
	s_raytracing.banner.generic.x		= 320;
	s_raytracing.banner.generic.y		= 16;
	s_raytracing.banner.string			= "SYSTEM SETUP";
	s_raytracing.banner.color			= color_white;
	s_raytracing.banner.style			= UI_CENTER;

	s_raytracing.framel.generic.type	= MTYPE_BITMAP;
	s_raytracing.framel.generic.name	= ART_FRAMEL;
	s_raytracing.framel.generic.flags	= QMF_INACTIVE;
	s_raytracing.framel.generic.x		= 0;
	s_raytracing.framel.generic.y		= 78;
	s_raytracing.framel.width			= 256;
	s_raytracing.framel.height			= 329;

	s_raytracing.framer.generic.type	= MTYPE_BITMAP;
	s_raytracing.framer.generic.name	= ART_FRAMER;
	s_raytracing.framer.generic.flags	= QMF_INACTIVE;
	s_raytracing.framer.generic.x		= 376;
	s_raytracing.framer.generic.y		= 76;
	s_raytracing.framer.width			= 256;
	s_raytracing.framer.height			= 334;

	s_raytracing.graphics.generic.type		= MTYPE_PTEXT;
	s_raytracing.graphics.generic.flags		= QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS;
	s_raytracing.graphics.generic.id		= ID_GRAPHICS;
	s_raytracing.graphics.generic.callback	= RayTracing_Event;
	s_raytracing.graphics.generic.x			= 216;
	s_raytracing.graphics.generic.y			= 240 - 2 * PROP_HEIGHT;
	s_raytracing.graphics.string			= "GRAPHICS";
	s_raytracing.graphics.style				= UI_RIGHT;
	s_raytracing.graphics.color				= color_red;

	s_raytracing.raytracing.generic.type		= MTYPE_PTEXT;
	s_raytracing.raytracing.generic.flags		= QMF_RIGHT_JUSTIFY;
	s_raytracing.raytracing.generic.id			= ID_RAYTRACING;
	s_raytracing.raytracing.generic.callback	= RayTracing_Event;
	s_raytracing.raytracing.generic.x			= 216;
	s_raytracing.raytracing.generic.y			= 240 - PROP_HEIGHT;
	s_raytracing.raytracing.string				= "RAY TRACING";
	s_raytracing.raytracing.style				= UI_RIGHT;
	s_raytracing.raytracing.color				= color_red;

	s_raytracing.display.generic.type		= MTYPE_PTEXT;
	s_raytracing.display.generic.flags		= QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS;
	s_raytracing.display.generic.id			= ID_DISPLAY;
	s_raytracing.display.generic.callback	= RayTracing_Event;
	s_raytracing.display.generic.x			= 216;
	s_raytracing.display.generic.y			= 240;
	s_raytracing.display.string				= "DISPLAY";
	s_raytracing.display.style				= UI_RIGHT;
	s_raytracing.display.color				= color_red;

	s_raytracing.sound.generic.type		= MTYPE_PTEXT;
	s_raytracing.sound.generic.flags	= QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS;
	s_raytracing.sound.generic.id		= ID_SOUND;
	s_raytracing.sound.generic.callback	= RayTracing_Event;
	s_raytracing.sound.generic.x		= 216;
	s_raytracing.sound.generic.y		= 240 + PROP_HEIGHT;
	s_raytracing.sound.string			= "SOUND";
	s_raytracing.sound.style			= UI_RIGHT;
	s_raytracing.sound.color			= color_red;

	s_raytracing.network.generic.type		= MTYPE_PTEXT;
	s_raytracing.network.generic.flags		= QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS;
	s_raytracing.network.generic.id			= ID_NETWORK;
	s_raytracing.network.generic.callback	= RayTracing_Event;
	s_raytracing.network.generic.x			= 216;
	s_raytracing.network.generic.y			= 240 + 2 * PROP_HEIGHT;
	s_raytracing.network.string				= "NETWORK";
	s_raytracing.network.style				= UI_RIGHT;
	s_raytracing.network.color				= color_red;

	y = 160;
	s_raytracing.renderer.generic.type		= MTYPE_SPINCONTROL;
	s_raytracing.renderer.generic.name		= "Renderer:";
	s_raytracing.renderer.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.renderer.generic.callback	= RayTracing_Event;
	s_raytracing.renderer.generic.id		= ID_RENDERER;
	s_raytracing.renderer.generic.x			= RAYTRACING_X_POS;
	s_raytracing.renderer.generic.y			= y;
	s_raytracing.renderer.itemnames			= renderer_names;

	y += BIGCHAR_HEIGHT + 6;
	s_raytracing.upscale.generic.type		= MTYPE_SPINCONTROL;
	s_raytracing.upscale.generic.name		= "Upscaler:";
	s_raytracing.upscale.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.upscale.generic.callback	= RayTracing_Event;
	s_raytracing.upscale.generic.id			= ID_UPSCALE;
	s_raytracing.upscale.generic.x			= RAYTRACING_X_POS;
	s_raytracing.upscale.generic.y			= y;
	s_raytracing.upscale.itemnames			= upscale_names;

	y += BIGCHAR_HEIGHT + 2;
	s_raytracing.truelight.generic.type		= MTYPE_SPINCONTROL;
	s_raytracing.truelight.generic.name		= "Light System:";
	s_raytracing.truelight.generic.flags	= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.truelight.generic.callback	= RayTracing_Event;
	s_raytracing.truelight.generic.id		= ID_TRUELIGHT;
	s_raytracing.truelight.generic.x		= RAYTRACING_X_POS;
	s_raytracing.truelight.generic.y		= y;
	s_raytracing.truelight.itemnames		= truelight_names;

	y += BIGCHAR_HEIGHT + 2;
	s_raytracing.gilevel.generic.type		= MTYPE_SPINCONTROL;
	s_raytracing.gilevel.generic.name		= "Indirect Light:";
	s_raytracing.gilevel.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.gilevel.generic.callback	= RayTracing_Event;
	s_raytracing.gilevel.generic.id			= ID_GI_LEVEL;
	s_raytracing.gilevel.generic.x			= RAYTRACING_X_POS;
	s_raytracing.gilevel.generic.y			= y;
	s_raytracing.gilevel.itemnames			= gilevel_names;

	y += BIGCHAR_HEIGHT + 2;
	s_raytracing.denoiser.generic.type		= MTYPE_RADIOBUTTON;
	s_raytracing.denoiser.generic.name		= "Denoiser:";
	s_raytracing.denoiser.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.denoiser.generic.callback	= RayTracing_Event;
	s_raytracing.denoiser.generic.id		= ID_DENOISER;
	s_raytracing.denoiser.generic.x			= RAYTRACING_X_POS;
	s_raytracing.denoiser.generic.y			= y;

	y += BIGCHAR_HEIGHT + 2;
	s_raytracing.physicalSky.generic.type		= MTYPE_RADIOBUTTON;
	s_raytracing.physicalSky.generic.name		= "Physical Sky:";
	s_raytracing.physicalSky.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.physicalSky.generic.callback	= RayTracing_Event;
	s_raytracing.physicalSky.generic.id			= ID_PHYSICAL_SKY;
	s_raytracing.physicalSky.generic.x			= RAYTRACING_X_POS;
	s_raytracing.physicalSky.generic.y			= y;

	y += BIGCHAR_HEIGHT + 2;
	s_raytracing.clouds.generic.type		= MTYPE_RADIOBUTTON;
	s_raytracing.clouds.generic.name		= "Volumetric Clouds:";
	s_raytracing.clouds.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.clouds.generic.callback	= RayTracing_Event;
	s_raytracing.clouds.generic.id			= ID_CLOUDS;
	s_raytracing.clouds.generic.x			= RAYTRACING_X_POS;
	s_raytracing.clouds.generic.y			= y;

	y += BIGCHAR_HEIGHT + 2;
	s_raytracing.godRays.generic.type		= MTYPE_RADIOBUTTON;
	s_raytracing.godRays.generic.name		= "God Rays:";
	s_raytracing.godRays.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.godRays.generic.callback	= RayTracing_Event;
	s_raytracing.godRays.generic.id			= ID_GOD_RAYS;
	s_raytracing.godRays.generic.x			= RAYTRACING_X_POS;
	s_raytracing.godRays.generic.y			= y;

	y += BIGCHAR_HEIGHT + 2;
	s_raytracing.tonemap.generic.type		= MTYPE_SPINCONTROL;
	s_raytracing.tonemap.generic.name		= "Tone Mapping:";
	s_raytracing.tonemap.generic.flags		= QMF_PULSEIFFOCUS | QMF_SMALLFONT;
	s_raytracing.tonemap.generic.callback	= RayTracing_Event;
	s_raytracing.tonemap.generic.id			= ID_TONEMAP;
	s_raytracing.tonemap.generic.x			= RAYTRACING_X_POS;
	s_raytracing.tonemap.generic.y			= y;
	s_raytracing.tonemap.itemnames			= tonemap_names;

	s_raytracing.back.generic.type		= MTYPE_BITMAP;
	s_raytracing.back.generic.name		= ART_BACK0;
	s_raytracing.back.generic.flags		= QMF_LEFT_JUSTIFY | QMF_PULSEIFFOCUS;
	s_raytracing.back.generic.callback	= RayTracing_Event;
	s_raytracing.back.generic.id		= ID_BACK;
	s_raytracing.back.generic.x			= 0;
	s_raytracing.back.generic.y			= 480 - 64;
	s_raytracing.back.width				= 128;
	s_raytracing.back.height			= 64;
	s_raytracing.back.focuspic			= ART_BACK1;

	s_raytracing.apply.generic.type		= MTYPE_BITMAP;
	s_raytracing.apply.generic.name		= ART_ACCEPT0;
	s_raytracing.apply.generic.flags	= QMF_RIGHT_JUSTIFY | QMF_PULSEIFFOCUS | QMF_HIDDEN | QMF_INACTIVE;
	s_raytracing.apply.generic.callback	= RayTracing_Event;
	s_raytracing.apply.generic.id		= ID_APPLY;
	s_raytracing.apply.generic.x		= 640;
	s_raytracing.apply.generic.y		= 480 - 64;
	s_raytracing.apply.width			= 128;
	s_raytracing.apply.height			= 64;
	s_raytracing.apply.focuspic			= ART_ACCEPT1;

	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.banner );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.framel );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.framer );

	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.graphics );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.raytracing );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.display );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.sound );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.network );

	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.renderer );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.upscale );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.truelight );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.gilevel );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.denoiser );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.physicalSky );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.clouds );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.godRays );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.tonemap );

	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.back );
	Menu_AddItem( &s_raytracing.menu, (void *)&s_raytracing.apply );

	RayTracing_SetMenuItems();
}

/*
=================
UI_RayTracingOptionsMenu_Cache
=================
*/
void UI_RayTracingOptionsMenu_Cache( void ) {
	trap_R_RegisterShaderNoMip( ART_FRAMEL );
	trap_R_RegisterShaderNoMip( ART_FRAMER );
	trap_R_RegisterShaderNoMip( ART_BACK0 );
	trap_R_RegisterShaderNoMip( ART_BACK1 );
	trap_R_RegisterShaderNoMip( ART_ACCEPT0 );
	trap_R_RegisterShaderNoMip( ART_ACCEPT1 );
}

/*
=================
UI_RayTracingOptionsMenu
=================
*/
void UI_RayTracingOptionsMenu( void ) {
	RayTracing_MenuInit();
	UI_PushMenu( &s_raytracing.menu );
	Menu_SetCursorToItem( &s_raytracing.menu, &s_raytracing.raytracing );
}
