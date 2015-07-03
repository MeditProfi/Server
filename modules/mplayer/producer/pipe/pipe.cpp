#include "../../stdafx.h"
#include "pipe.h"

namespace caspar { namespace mplayer {


mplayer_pipe::mplayer_pipe(char type)
	: type_(type)
{

	hpipe_ = 0;
	use_counter = 0;
	cant_read_ = false;
	hConnectEvent_ = CreateEvent( NULL, TRUE, FALSE, NULL );
	hReadEvent_ = CreateEvent( NULL, TRUE, FALSE, NULL );
	hStopEvent_ = CreateEvent( NULL, TRUE, FALSE, NULL );
}

mplayer_pipe::~mplayer_pipe()
{
	mdb("~mplayer_pipe " << type_);

	killPipe();
	CloseHandle(hConnectEvent_);
	CloseHandle(hReadEvent_);
	CloseHandle(hStopEvent_);
}


void mplayer_pipe::killPipe()
{
	if ((hpipe_ != INVALID_HANDLE_VALUE) && (hpipe_ != 0))
	{
		mdb("killPipe " << pipe_name_.c_str());
		HANDLE h = hpipe_;

		mdb("do...");
		CloseHandle(h);
		hpipe_ = 0;
	} else mdb("killPipe: there is no pipe");

	this->pipe_name_ = "";

	mdb("kill pipe OK");
}

void mplayer_pipe::stop()
{
	SetEvent(hStopEvent_);
}

void mplayer_pipe::createPipe(std::string pipeName)
{
	mdb("createPipe " << pipeName.c_str() << "...");

	cant_read_ = false;
	this->pipe_name_ = pipeName;

	int size = 0;
	if (type_ == 'v') size = VIDEO_PIPE_SIZE;
	else if (type_ == 'a') size = AUDIO_PIPE_SIZE;

	hpipe_ = CreateNamedPipeA(
								pipeName.c_str(), 
								FILE_FLAG_FIRST_PIPE_INSTANCE | PIPE_ACCESS_INBOUND | FILE_FLAG_OVERLAPPED, 
								PIPE_TYPE_BYTE | PIPE_READMODE_BYTE, 
								1,
								0,
								size, 
								0, 
								0);

	if (hpipe_ != INVALID_HANDLE_VALUE) 
	{
		mdb("createPipe OK");
	}
	else
	{
		mdb("createPipe failed");
	}
}

string makePipeName()
{
	stringstream ss;
	ss << "\\\\.\\pipe\\CasparMPlayer-";

	boost::uuids::uuid uuid = boost::uuids::random_generator()();
	ss << uuid;

	return ss.str();
}

int readPipe(void* opaque, uint8_t* buf, int buf_size)
{
	try
	{
		mplayer_pipe *pipe = (mplayer_pipe*)opaque;
		if (pipe->cant_read_) return 0;

		pipe->use_counter++;
		if (pipe->use_counter >= 50)
		{
			mwarn("read pipe " << pipe->type() << ": suspend in readPipe!");	
			pipe->cant_read_ = true;
			pipe->use_counter = 0;
			return 0;
		}

		DWORD rdcnt = 0; 
		bool readOK = false;

		OVERLAPPED o;
		memset(&o, 0, sizeof(OVERLAPPED));
		o.hEvent = pipe->hReadEvent();

		ResetEvent(o.hEvent);
		HANDLE hPipe = pipe->hpipe();
		ReadFile(hPipe, buf, buf_size, &rdcnt, &o);
	
		HANDLE events[2];
		events[0] = o.hEvent;
		events[1] = pipe->hStopEvent();

		DWORD waitres = WaitForMultipleObjects(2, events, FALSE, READ_PIPE_WAIT_TOUT);
		if (waitres == WAIT_OBJECT_0)
		{
			if (GetOverlappedResult(hPipe, &o, &rdcnt, FALSE))
			{
				if (rdcnt != 0)
				{
					readOK = true;
				} else mwarn("read pipe " << pipe->type() << ": rdcnt == 0!");	
			} else mwarn("read pipe " << pipe->type() << ": GetOverlappedResult failed");			 
		} 
		else if (waitres == WAIT_OBJECT_0 + 1)
		{
			mlg("pipe " << pipe->type() << " stopped");
		}
		else if (waitres == WAIT_TIMEOUT)
		{
			mwarn("read pipe " << pipe->type() << ": WaitForSingleObject failed (timeout error)");
		}
		else 
		{
			mwarn("read pipe " << pipe->type() << ": unknown error");
		}


		if (!readOK)
		{
			pipe->cant_read_ = true;
			return 0;
		}

		return rdcnt;
	}
	catch(...)
	{
		mwarn("read pipe unknown error!");
		return 0;
	}
}


}}