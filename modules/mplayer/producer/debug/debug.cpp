#include "../../stdafx.h"
#include "debug.h"

using namespace std;
namespace caspar { namespace mplayer {
#define TCNT 10

MDEBUG mdebug;


struct DEBUG_TIMERS
{
	DWORD timers[TCNT];
	DEBUG_TIMERS() 
	{
		for (int i = 0; i < TCNT; i++) timers[i] = GetTickCount();
	}

	DWORD& operator[](int i)
	{
		return timers[i];
	}
};
DEBUG_TIMERS dbgTimers;

void TIMER_CLEAR(int tid)
{
	if (tid < TCNT)
		dbgTimers[tid] = GetTickCount();
}

DWORD TIMER_GET(int tid)
{
	if (tid < TCNT) return GetTickCount() - dbgTimers[tid];
	else return 0;
}

bool RUN_MPLAYER_SEPARATELY()
{
	std::wstring d = env::mplayer_debug();
	return d.find(L"sep") != std::wstring::npos;
}

}}