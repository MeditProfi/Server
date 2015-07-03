#pragma once

#include "mplayer_includes.h"


namespace caspar { namespace mplayer {

using namespace std;

int readPipe(void* opaque, uint8_t* buf, int buf_size);
string makePipeName();

class mplayer_pipe
{
private:		
	tbb::atomic<HANDLE>			hpipe_;	
	string						pipe_name_;
	HANDLE						hConnectEvent_;
	HANDLE						hReadEvent_;
	HANDLE						hStopEvent_;
	char						type_;

public:
	tbb::atomic<bool>			cant_read_;
	tbb::atomic<int>			use_counter;

	HANDLE		hpipe()			{return hpipe_;}
	string		pipe_name()		{return pipe_name_;}
	HANDLE		hConnectEvent() {return hConnectEvent_;}
	HANDLE		hReadEvent()	{return hReadEvent_;}
	HANDLE		hStopEvent()	{return hStopEvent_;}
	char		type()			{return type_;}

	mplayer_pipe(char type);
	~mplayer_pipe();
	void createPipe(std::string pipeName);
	void killPipe();
	void stop();
};

}}