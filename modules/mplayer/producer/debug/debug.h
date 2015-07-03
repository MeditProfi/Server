#pragma once

#include "mplayer_includes.h"
#include <string>



namespace caspar { namespace mplayer {



enum
{
	AUDIO_IN_TMR = 0,
	AUDIO_OUT_TMR,
	VIDEO_IN_TMR,
	VIDEO_OUT_TMR
};

void TIMER_CLEAR(int tid);
DWORD TIMER_GET(int tid);

struct MDEBUG
{	
	float ivCtr;
	float ovCtr;
	float iaCtr;
	float iarCtr;
	float oaCtr;

	float ivFps;
	float ovFps;
	float iaFps;
	float iarFps;
	float oaFps;


	MDEBUG() {memset(this, 0, sizeof(MDEBUG));}
};
extern MDEBUG mdebug;

bool RUN_MPLAYER_SEPARATELY();

}}












