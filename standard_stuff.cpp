#include "standard_stuff.h"
#include <windows.h>

//// TThread

unsigned long __stdcall TThread::ThreadFunc(void* pointer)
{
	return ((TThread*) pointer)->VirtThreadFunc();
}

TThread::TThread()
{
	running=true;
	threadhandle=CreateThread(0,0,&ThreadFunc,(void*) this,0,0);
}

TThread::~TThread()
{
	running=false;
	while(IsRunning())// nem vagyunk hajland�k destru�l�dni amig fut a sz�l.
		Sleep(1);
}

bool TThread::IsRunning()
{
	DWORD exitcode;
	return GetExitCodeThread(threadhandle,&exitcode) && exitcode==STILL_ACTIVE;
}


/////////TTimer();
int TTimer::VirtThreadFunc()
{
	int legutobbi=GetTickCount();
	while(running)
	{
		int gtc=GetTickCount();
		if (legutobbi<gtc)
		{
			legutobbi+=interval;
			OnTimer();
		}
		if (interval>10)
			Sleep(interval/10);
	}
	return 0;
}