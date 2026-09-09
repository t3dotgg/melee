#ifndef _DOLPHIN_DEMOPUTS_H_
#define _DOLPHIN_DEMOPUTS_H_

#include <dolphin/types.h>

typedef enum
{
    DMTF_POINTSAMPLE,
    DMTF_BILERP,
} DMTexFlt; 

typedef enum
{
    DM_FT_OPQ,
    DM_FT_RVS,
    DM_FT_XLU
} DMFontType;

void DEMOSetFontType(DMFontType attr);
void DEMOLoadFont(enum _GXTexMapID texMap, enum _GXTexMtx texMtx, DMTexFlt texFlt);
void DEMOSetupScrnSpc(s32 width, s32 height, float depth);
void DEMOInitCaption(s32 font_type, s32 width, s32 height);
void DEMOPuts(s16 x, s16 y, s16 z, char * string);
void DEMOPrintf(s16 x, s16 y, s16 z, char * fmt, ...);
struct OSFontHeader * DEMOInitROMFont();
void DEMOSetROMFontSize(s16 size, s16 space);
int DEMORFPuts(s16 x, s16 y, s16 z, char * string);
int DEMORFPutsEx(s16 x, s16 y, s16 z, char * string, s16 maxWidth, int length);
int DEMORFPrintf(s16 x, s16 y, s16 z, char * fmt, ...);
char * DEMODumpROMFont(char * string);
int DEMOGetRFTextWidth(char * string);

#endif
