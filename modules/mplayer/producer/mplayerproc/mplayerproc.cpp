#include "../../stdafx.h"
#include "mplayerproc.h"
#include "input_video.h"
#include "input_audio.h"
#include<boost/algorithm/string/split.hpp>  
#include<boost/algorithm/string.hpp> 
#include<boost/algorithm/string/classification.hpp> 
#include "mplayer_utils.h"
#include "debug.h"
#include <boost/timer.hpp>



using namespace std;
namespace caspar { namespace mplayer {

struct mplayer_process::implementation : boost::noncopyable
{
	executor							executor_;
	executor							executor_stdout_;
	string								resource_name_;
	std::shared_ptr<mplayer_pipe>		video_pipe_;
	std::shared_ptr<mplayer_pipe>		audio_pipe_;

	tbb::atomic<bool>					mplayer_working_;
	tbb::atomic<bool>					thread_started;
	tbb::atomic<bool>					stdout_thread_started;
	PROCESS_INFORMATION					process_;
	int									cache_size_;
	tbb::atomic<int>					cache_fullness_;
	tbb::atomic<bool>					active_;
	common_input_data*					cmn_;
	double								prev_frame_ts_;
	double								prev_frame_dt_;
	double								force_in_fps_;
	std::string							opt_params_;
	ofstream							mplayer_log_file;
	DWORD								last_mplayer_log_flush_time;
	DWORD								mplayer_log_flush_period;


	HANDLE stdout_read_;
	HANDLE stdout_write_;
	HANDLE read_stdout_event_;
	HANDLE stop_stdout_event_;
	HANDLE cp_stop_event_;
	string outbuf_;
	const int vjt;
	tbb::atomic<bool> allow_read_stdout_;

	double maybe_newfps;
	int newfps_trys;
	unsigned long						missed_packets_;


	explicit implementation(long unique_num, string resource_name, int cache_size, common_input_data* cmn, double infps, std::string opt_params, int vfps_jt) 
		: executor_(L"mplayer-control-" + toWStr(resource_name) + L"-" + std::to_wstring((long long)unique_num))
		, executor_stdout_(L"mplayer-control-stdout-" + toWStr(resource_name) + L"-" + std::to_wstring((long long)unique_num))
		, resource_name_(resource_name)
		, video_pipe_( new mplayer_pipe('v') )
		, audio_pipe_( new mplayer_pipe('a') )
		, cache_size_(cache_size)
		, cmn_(cmn)
		, prev_frame_ts_(-1)
		, prev_frame_dt_(-1)
		, force_in_fps_(infps)
		, opt_params_(opt_params)
		, stdout_read_(0)
		, stdout_write_(0)
		, read_stdout_event_(0)
		, vjt(vfps_jt)
		, missed_packets_(0)
	{
		mdb("vjt = " << vjt);

		allow_read_stdout_ = false;
		newfps_trys = 0;
		maybe_newfps = 0;

		

		stdout_thread_started = false;

		active_ = true;
		mplayer_working_ = false;
		thread_started = false;
		cache_fullness_ = 0;
		memset(&process_,0,sizeof(PROCESS_INFORMATION));

		stop_stdout_event_ = CreateEvent( NULL, FALSE, FALSE, NULL );
		cp_stop_event_ = CreateEvent( NULL, TRUE, FALSE, NULL ); 

		init_stdout_handles();

		debugTimeCntr = 0;

#ifdef READ_MPLAYER_STDOUT
	if (!RUN_MPLAYER_SEPARATELY())
	{
		executor_stdout_.begin_invoke([this]
		{				
			this->thread_stdout();
		});	
	}
#endif
		if (enable_mplayer_log) open_mplayer_log();
		mplayer_log_flush_period = env::properties().get(L"configuration.paths.mplayer_log_flush_period", 1000);
	}

	void thread_stdout()
	{
		stdout_thread_started = true;
		mdb("mplayer request info cycle");
		while ((active_))
		{
			if (allow_read_stdout_) read_mplayer_params();
			Sleep(100);

			if (enable_fps_log)
			{
				print_mplayer_params();
			}
		}
		mdb("thread_stdout finished " << resource_name_.c_str());

		stdout_thread_started = false;
	}

	~implementation()
	{
		mdb("~mplayer_proc " << resource_name_.c_str());

		mdb("stop executor_stdout_...");
		executor_stdout_.stop();

		mdb("clear executor_stdout_...");
		executor_stdout_.clear();

		mdb("wait executor_stdout_...");
		while (!executor_stdout_.empty());

		mdb("deinit_stdout_handles...");
		deinit_stdout_handles();

		mdb("Waiting for mplayer thread");
		while(thread_started);

		mdb("Waiting for mplayer stdout thread");
		while(stdout_thread_started);

		mdb("CloseHandle(stop_stdout_event_)...");
		CloseHandle(stop_stdout_event_);

		mdb("CloseHandle(cp_stop_event_)...");
		CloseHandle(cp_stop_event_);

		mdb("~mplayer_proc OK");
	}

	void init_mplayer()
	{
		mlg("init_mplayer...");
		cmn_->buff_ready = false;
		allow_read_stdout_ = true;

		string pName = makePipeName();
		video_pipe_->createPipe( pName + "-video" );
		audio_pipe_->createPipe( pName + "-audio" );

		start_process();

		if (connect_pipes()) 
		{
			mplayer_working_ = true;
			mlg("init_mplayer OK");
		} else mwarn("init_mplayer .failed");
	}

	std::string get_af_name(AVSampleFormat fmt)
	{
		switch (fmt) 
		{
			case AV_SAMPLE_FMT_U8: return "u8le";     
			case AV_SAMPLE_FMT_S16: return "s16le";             
			case AV_SAMPLE_FMT_S32: return "s32le";            
		}
		return "unknown";
	}

	void start_process()
	{ 
		mdb("mplayer start_process...");

		std::string cmd_template = toStdStr(env::mplayer_command());

		std::string optional = "";
		if (force_in_fps_ != 0) optional += "-fps " + std::to_string((long double)force_in_fps_) + " ";
		optional += opt_params_ + " ";

		const std::string cmdstr = boost::str(
										boost::format(cmd_template)
										% toStdStr(env::mplayer_bin_path())
										% video_pipe_->pipe_name()
										% cmn_->width
										% cmn_->height
										% audio_pipe_->pipe_name()
										% SAudioSamplingRate
										% SAudioChannels
										% cache_size_
										% resource_name_
										% get_af_name(SAudioSampleFmt)
										% optional);


		const LPSTR cmdline = (LPSTR)cmdstr.c_str();
		mlg("cmdline = " << cmdline);

		BOOL bSuccess = FALSE; 
	if (!RUN_MPLAYER_SEPARATELY())
	{
		STARTUPINFOA siStartInfo;
		
 
		ZeroMemory( &process_, sizeof(PROCESS_INFORMATION) );
		ZeroMemory( &siStartInfo, sizeof(STARTUPINFO) );
		siStartInfo.cb = sizeof(STARTUPINFO); 
#ifdef READ_MPLAYER_STDOUT
		siStartInfo.hStdError = stdout_write_;
		siStartInfo.hStdOutput = stdout_write_;
#endif
		siStartInfo.dwFlags |= STARTF_USESTDHANDLES;
 
   		bSuccess = CreateProcessA(NULL, 
			cmdline, 
			NULL,         
			NULL,         
			TRUE,          
			0,     
			NULL,        
			NULL,        
			&siStartInfo,  
			&process_); 

	} 
	else
	{
	
		std::ofstream fs("mm.bat",std::ios_base::out);
		fs << cmdline;
		fs.close();
		system("start mm.bat");
		bSuccess = TRUE;
	}

		if ( bSuccess ) 
		{
			mdb("mplayer start_process OK");
		}
		else
		{
			merr("mplayer start_process failed (make sure you correctly set mplayer path)");
		} 
	}

	bool connect_pipes()
	{
		mdb("connect_pipes...");

		ResetEvent(cp_stop_event_);

		ResetEvent(video_pipe_->hConnectEvent());
		ResetEvent(audio_pipe_->hConnectEvent());

		OVERLAPPED ov; 
		memset(&ov, 0, sizeof(OVERLAPPED));
		ov.hEvent = video_pipe_->hConnectEvent();

		OVERLAPPED oa; 
		memset(&oa, 0, sizeof(OVERLAPPED));
		oa.hEvent = audio_pipe_->hConnectEvent();

		HANDLE handles[2];
		int npipes;

		if (cmn_->video_input_->empty_frames())
		{
			handles[0] = audio_pipe_->hConnectEvent();
			ConnectNamedPipe(audio_pipe_->hpipe(), &oa);
			npipes = 1;
		}
		else if (cmn_->audio_input_->empty_frames())
		{
			handles[0] = video_pipe_->hConnectEvent();
			ConnectNamedPipe(video_pipe_->hpipe(), &ov);
			npipes = 1;
		}
		else
		{
			handles[0] = video_pipe_->hConnectEvent();
			handles[1] = audio_pipe_->hConnectEvent();
			npipes = 2;

			ConnectNamedPipe(video_pipe_->hpipe(), &ov);
			ConnectNamedPipe(audio_pipe_->hpipe(), &oa);
		}

		mdb("connect pipes - waiting...");

		bool ok = false;
		if (active_)
		{
			if (npipes == 1)
			{
				HANDLE events[2];
				events[0] = handles[0];
				events[1] = cp_stop_event_;
				mdb("wait pipe 1...");
				DWORD waitres = WaitForMultipleObjects(2, events, FALSE, CONNECT_PIPE_TOUT);
				if (waitres == WAIT_OBJECT_0) ok = true;
			}
			else if (npipes == 2)
			{
				boost::timer t;
				t.restart();

				HANDLE events[2];
				events[0] = handles[0];
				events[1] = cp_stop_event_;
				mdb("wait pipe 1...");
				DWORD waitres1 = WaitForMultipleObjects(2, events, FALSE, CONNECT_PIPE_TOUT);
				if (waitres1 == WAIT_OBJECT_0)
				{
					double elapsed = t.elapsed();
					elapsed *= 1000;
					int wtime = CONNECT_PIPE_TOUT - (int)elapsed;
					if (wtime < 0) wtime = 0;

					events[0] = handles[1];
					events[1] = cp_stop_event_;
					mdb("wait pipe 2...");
					DWORD waitres2 = WaitForMultipleObjects(2, events, FALSE, wtime);
					if (waitres2 == WAIT_OBJECT_0) ok = true; 
				}
			}
		}

		if (ok)
		{
			mdb("connect_pipes OK");
			return true; 
		} 
		else
		{
			mwarn("No response from mplayer. Try use RUN_MPLAYER_SEPARATELY or increase CONNECT_PIPE_TOUT");
			return false;
		}
	}

	void stop_reading_stdout()
	{
		SetEvent(stop_stdout_event_);
	}

	void deinit_mplayer()
	{
		mlg("deinit_mplayer...");
		//while (cmn_->input_initialized());

		mplayer_working_ = false;
		allow_read_stdout_ = false;

		SetEvent(cp_stop_event_);
		stop_reading_stdout();
		video_pipe_->killPipe();
		audio_pipe_->killPipe();

		if ((process_.hProcess != INVALID_HANDLE_VALUE) && (process_.hProcess != 0))
		{
			mlg("terminate mplayer...");
			TerminateProcess(process_.hProcess, 0);
			memset(&process_,0,sizeof(PROCESS_INFORMATION));
		} 

		mlg("deinit_mplayer OK");
	}


	DWORD debugTimeCntr;
	void print_mplayer_params()
	{
		if (GetTickCount() - debugTimeCntr >= 1000)
		{
			debugTimeCntr = GetTickCount();

			mfps("mplayer cache fullness = " << cache_fullness_ << "%");
			mfps("input fps = " << cmn_->in_fps_);
		}
	}

	
	void thread()
	{
		
		

		try
		{
			mdb("mplayer wait for input uninit...");
			while ((active_) && (cmn_->input_initialized()))
			{
				Sleep(100);
			}

			mdb("mplayer check for start");
			while ((active_) && (!mplayer_working_))
			{
				mdb("mplayer is not working");
				deinit_mplayer(); 

				cmn_->video_input_->drop();
				cmn_->audio_input_->drop();

				init_mplayer();

				Sleep(100);
			}
		}
		catch (...)
		{
			CASPAR_LOG_CURRENT_EXCEPTION();
		}
	}

	void start_thread()
	{
		mtr("mplayer start_thread...");

		if ((active_ ) && (executor_.empty()))
		{
			if (!this->thread_started)
			{
				this->thread_started = true;
			
				executor_.begin_invoke([this]
				{				
					this->thread();
					this->thread_started = false;
				});
			} else mtr("mplayer thread is already started");
		}
	}


	void problem()
	{
		mwarn("mplayer problem!");
		prev_frame_ts_ = -1;
		prev_frame_dt_ = -1;
		mplayer_working_ = false;
		start_thread();
	}

	void stop()
	{
		mdb("mplayer stop....");
		SetEvent(cp_stop_event_);
		active_ = false;	
		mdb("mplayer stop: waiting for thread started");
		while (thread_started);
		executor_.clear();
		deinit_mplayer();
		while (!executor_.empty());
		mdb("mplayer stop OK");
	}



	#define MPLAYER_STDOUT_BUFSZ 4096
	void init_stdout_handles()
	{
		SECURITY_ATTRIBUTES saAttr;
		saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
		saAttr.bInheritHandle = TRUE;
		saAttr.lpSecurityDescriptor = NULL;

		CreatePipe(&stdout_read_, &stdout_write_,&saAttr, MPLAYER_STDOUT_BUFSZ);

		if (!SetHandleInformation(stdout_read_, HANDLE_FLAG_INHERIT, 0) )
		{
			merr("error create mplayer stdout pipe");
		}

		read_stdout_event_ = CreateEvent(NULL, FALSE, FALSE, NULL);
	}

	void deinit_stdout_handles()
	{
		mdb("CloseHandle(stdout_write_);");
		CloseHandle(stdout_write_);

		mdb("CloseHandle(stdout_read_);");
		CloseHandle(stdout_read_);

		mdb("CloseHandle(read_stdout_event_);");
		CloseHandle(read_stdout_event_);
	}

	char parse_mplayer_string(string s, string &outval)
	{
		boost::algorithm::trim(s);
		if (s.size() < 6) return 0;

		//frame timestamp
		if (
			(s[0] == 'P') &&
			(s[1] == 'T') &&
			(s[2] == 'S') &&
			(s[3] == ':') &&
			(s[4] == ' ')
			)
		{
			outval = s.erase(0, 5);
			return 't';
		}

		//cache fullness
		if (s.at(s.size() - 1) == '%')
		{
			vector< std::string > list;		
			boost::algorithm::split(list, s, boost::algorithm::is_any_of(" "));

			if (list.size() > 3)
			{
				outval = list.at(list.size() - 1);
				std::remove(outval.begin(), outval.end(), '%');
				return 'f';
			}
		}

		//missed packet (RTP: missed %d packets) 
		if (s.find("RTP: missed ") != s.npos)
		{
			unsigned int pos = (unsigned int)s.find("RTP: missed ");
			unsigned int miss;	

			sscanf(s.c_str() + pos + 12, "%d", &miss);
			outval = std::to_string((long long)miss);
			return 'm';
		}

		//unrecognized data
		return 0;
	}

	string current_time()
	{
		const boost::posix_time::ptime now = boost::posix_time::microsec_clock::local_time();
		const boost::posix_time::time_duration td = now.time_of_day();

		const int64_t hours        = td.hours();
		const int64_t minutes      = td.minutes();
		const int64_t seconds      = td.seconds();
		
		
		const int64_t milliseconds = td.total_milliseconds() - ((hours * 3600 + minutes * 60 + seconds) * 1000);

		return  boost::str(
							boost::format("%02d.%02d.%04d   %02d:%02d:%02d.%03d")
							% now.date().day()
							% now.date().month()
							% now.date().year()
							% hours
							% minutes
							% seconds
							% milliseconds);
	}

	void open_mplayer_log()
	{
		string now_allowed_symbols = "\\/:*?\"<>| \r\n";

		string curtime = current_time();
		curtime = boost::replace_if(curtime, boost::is_any_of(now_allowed_symbols), '.');

		string url = resource_name_;
		url = boost::replace_if(url, boost::is_any_of(now_allowed_symbols), '_');

		string logname = narrow(env::log_folder()) + "\\mplayer_" + curtime + "_" + url + ".log";
		mlg("mplayer stdout log file = " << logname);

		mplayer_log_file.open(logname);
		last_mplayer_log_flush_time = GetTickCount();
	}

	void append_to_mplayer_log(string data)
	{
		boost::trim(data);
		if (data.size() > 0)
		{
			mplayer_log_file << "[" << current_time() << "] " << data << "\n";
			if (GetTickCount() - last_mplayer_log_flush_time >= mplayer_log_flush_period) mplayer_log_file.flush();
		}
	}

	void read_mplayer_params()
	{
		char buf[MPLAYER_STDOUT_BUFSZ];
		DWORD rb = 0;

		OVERLAPPED o;
		memset(&o, 0, sizeof(o));
		o.hEvent = read_stdout_event_;
		if (!ReadFile(stdout_read_, buf, MPLAYER_STDOUT_BUFSZ, &rb, &o)) return;

		HANDLE events[2];
		events[0] = read_stdout_event_;
		events[1] = stop_stdout_event_; 
		DWORD waitres = WaitForMultipleObjects(2, events, FALSE, 500);
		if (waitres == READ_PIPE_WAIT_TOUT) return;
		if (waitres == WAIT_OBJECT_0 + 1) 
		{
			mdb("Stop reading mplayer's stdout");
			return;
		}
		if (!GetOverlappedResult(stdout_read_, &o, &rb, FALSE)) return;		
		if (rb == 0) return;
		
		for (unsigned int i = 0; (i < rb) && (i < MPLAYER_STDOUT_BUFSZ); i++)
		{
			if (buf[i] != '\n')
			{
				outbuf_ += buf[i];
			}
			else
			{
				if (enable_mplayer_log)	append_to_mplayer_log(outbuf_);

				string val; 
				char type = parse_mplayer_string(outbuf_, val);

				switch (type)
				{
				case 'f':	//cache fullness	
					try {cache_fullness_ = stoi(val);}
					catch (...) {}
					break;

				case 't':	//frame timestamp
					if (cmn_->variable_in_fps)
					{
						try 
						{
							double frame_ts_ = stod(val);
							//mdb("frame_ts_=" << frame_ts_);
							if (prev_frame_ts_ > 0)
							{
								double dt = frame_ts_ - prev_frame_ts_;

								if ((dt >= MIN_FRAME_TIME) && (dt <= MAX_FRAME_TIME))
								{
									if (fabs(dt - prev_frame_dt_) > 0.0000001)
									{
										double newfps = 1.0 / dt;

										//round new fps if there is forced values
										if (!cmn_->vfps_vals_list_.empty())
										{
											double minR = 0;
											int minI = -1;
											for (unsigned int i = 0; i < cmn_->vfps_vals_list_.size(); i++)
											{
												double r = fabs(newfps - cmn_->vfps_vals_list_[i]);
												if ((minI == -1) || (r < minR))
												{
													minR = r;
													minI = i;
												}
											}
											newfps = cmn_->vfps_vals_list_[minI];
										}

										if (newfps != cmn_->in_fps_)
										{
											if (maybe_newfps == 0) maybe_newfps = newfps;

											if (maybe_newfps == newfps)
											{
												newfps_trys++;
												if (newfps_trys >= vjt)
												{
													cmn_->in_fps_ = newfps;
													mdb("input fps from stream -> " << cmn_->in_fps_);

													maybe_newfps = 0;
													newfps_trys = 0;
												}
											}
											else
											{
												newfps_trys = 0;
												maybe_newfps = 0;
											}
										}
										else
										{
											newfps_trys = 0;
											maybe_newfps = 0;
										}
										prev_frame_dt_ = dt;
									}
								}
							}
							prev_frame_ts_ = frame_ts_;
						}
						catch (...) {}
					}
					break;


				case 'm': //missed RTP packets
					CASPAR_LOG(info) << "\t\tMISS PACKETS: " << val.c_str();
					missed_packets_ += boost::lexical_cast<int, std::string>(val);
					break;
				}

				outbuf_.clear();
			}
		}
	}
};






mplayer_process::mplayer_process(long unique_num, std::string resource_name, int cache_size, common_input_data* cmn, double infps, std::string opt_params, int vfps_jt) : impl_( new implementation(unique_num, resource_name, cache_size, cmn, infps, opt_params, vfps_jt)) {}
void mplayer_process::start_thread() {impl_->start_thread();}
bool mplayer_process::working() {return impl_->mplayer_working_;}
shared_ptr<mplayer_pipe> mplayer_process::video_pipe() {return impl_->video_pipe_;}
shared_ptr<mplayer_pipe> mplayer_process::audio_pipe() {return impl_->audio_pipe_;}
void mplayer_process::problem() {impl_->problem();}
void mplayer_process::stop() {impl_->stop();}
int mplayer_process::cache_fullness() {return impl_->cache_fullness_;}
unsigned long mplayer_process::missed_packets() {return impl_->missed_packets_;}
	
}}