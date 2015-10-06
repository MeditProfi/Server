/*
* Copyright 2013 Sveriges Television AB http://casparcg.com/
*
* This file is part of CasparCG (www.casparcg.com).
*
* CasparCG is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* CasparCG is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with CasparCG. If not, see <http://www.gnu.org/licenses/>.
*
* Author: Robert Nagy, ronag89@gmail.com
*/

#include "../stdafx.h"
#include "mplayer_includes.h"
#include "pipe.h"
#include "mplayerproc.h"
#include "input_video.h"
#include "input_audio.h"
#include "AudioDroper.h"
#include "fake_consumer.h"
#include "muxer.h"
#include <boost/algorithm/string.hpp>
#include <tbb/mutex.h>

extern tbb::atomic<long> mplayer_producer_unique_num;

namespace caspar { namespace mplayer {

class DebugDestroyLiveConfirm
{
public:
	~DebugDestroyLiveConfirm() {CASPAR_LOG(info) << "Destruction mplayer producer OK";}
};

struct mplayer_producer_params
{
	std::wstring			resource_name; 
	int						buff_time_max;
	int						buff_time_enough;
	int						width;
	int						height;
	double					infps;
	bool					vfps;
	std::vector<double>		vfps_vals_list;
	std::string				opt_params;
	double					bot_max;
	bool					noaudio;
	bool					novideo;
	int						delay;
	int						syncfr;
	int						vfps_jt;
	double					drop_frames_interval;
	AudioDroperCtorParams   audioDroperParams;
	long					unique_num;
	bool					enable_video_dup;
	int						unsync_patience_frames;
	bool					do_not_audio_click;
};

struct mplayer_producer : public core::frame_producer
{
	DebugDestroyLiveConfirm					destroy_confirm;

	core::monitor::subject					monitor_;
	const safe_ptr<core::frame_factory>		frame_factory_;
	mplayer::muxer							*muxer_;
	safe_ptr<core::basic_frame>				last_frame_;
	
	common_input_data						*cmn_;
	mplayer_process							*mplayer_process_;
	input_audio								*audio_input_;
	input_video								*video_input_;
	fake_mplayer_consumer					*fake_mplayer_consumer_;
	double									temptime_;
	double									tempstep_;
	video_buffer							last_video_buffer_;

	std::wstring							resource_name_;
	tbb::mutex								receive_mutex_;
	int										receive_counter_;
	DWORD									tmp_time_;
	tbb::atomic<int>						receive_fps_int_;
	int 									debug_counter_;
	double 									buffer_overflow_time;
	double 									buffer_overflow_time_max;
	const int								sync_frames_;
	const double							drop_frames_interval_;
	double									drop_ctr_;
	bool									do_drop_;
	const bool								enable_video_dup_;


		
public:

	explicit mplayer_producer(
									const safe_ptr<core::frame_factory>& frame_factory, 
									mplayer_producer_params &params
							  )
		: frame_factory_(frame_factory)
		, muxer_(makeMuxer(params.width, params.height, params.delay))
		, last_frame_(core::basic_frame::empty())
		, cmn_(new common_input_data(toStdStr(params.resource_name), frame_factory->get_video_format_desc(), params.buff_time_max, params.buff_time_enough, params.width, params.height, params.vfps, params.vfps_vals_list, params.unsync_patience_frames, params.do_not_audio_click))
		, mplayer_process_( new mplayer_process(params.unique_num, toStdStr(params.resource_name), SCacheSize, cmn_, params.infps, params.opt_params, params.vfps_jt, params.enable_video_dup))
		, audio_input_( new input_audio(params.unique_num, mplayer_process_, cmn_, params.audioDroperParams, params.noaudio ) )
		, video_input_( new input_video(params.unique_num, mplayer_process_, cmn_, params.novideo ) )
		, fake_mplayer_consumer_( new fake_mplayer_consumer(params.unique_num, toStdStr(params.resource_name), this , cmn_->out_fps_) )
		, temptime_(0)
		, tempstep_(1000.0f / cmn_->out_fps_)
		, resource_name_(params.resource_name)
		, receive_counter_(0)
		, tmp_time_(0)
		, buffer_overflow_time(0)
		, buffer_overflow_time_max(params.bot_max)
		, sync_frames_(params.syncfr)
		, drop_frames_interval_(params.drop_frames_interval)
		, enable_video_dup_(params.enable_video_dup)
	{
		cmn_->video_input_ = video_input_;
		cmn_->audio_input_ = audio_input_;

		last_video_buffer_ = nullptr;
		mplayer_process_->start_thread();
		audio_input_->start_thread();
		video_input_->start_thread();
		fake_mplayer_consumer_->start_thread();
		receive_fps_int_ = 0;
		debug_counter_ = 0;
		drop_ctr_ = 0;
		do_drop_ = false;

	
		std::string vfps_vals_desc;
		if ((params.vfps) && (!params.vfps_vals_list.empty()))
		{
			vfps_vals_desc = "variable fps values forced to be one of:  ";
			for (unsigned int i = 0; i < params.vfps_vals_list.size(); i++) vfps_vals_desc += std::to_string((long double)params.vfps_vals_list[i]) + "  ";
		}

		mlg("mplayer_producer created. Params:\n" << 
														"resource_name = " << params.resource_name.c_str() << "\n" <<
														"buff time max = " << params.buff_time_max << " ms\n" <<
														"buff time enough = " << params.buff_time_enough << " ms\n" <<
														"buffer_overflow_time_max = " << params.bot_max << " sec\n" <<
														"mplayer pict size = " << params.width << "x" << params.height << "\n" <<
														"input fps = " << params.infps << " (if 0 -> get from stream)" << "\n" <<
														"output fps = " << cmn_->out_fps_ << "\n" <<
														"frames_max = " << cmn_->frames_max << "\n" <<
														"frames_enough = " << cmn_->frames_enough << "\n" <<
														"use variable fps = " << (int)params.vfps << "\n" <<
														"sync frames = " << sync_frames_ << "\n" <<
														"drop min interval ms = " << drop_frames_interval_ << "\n" <<
														vfps_vals_desc.c_str() << "\n" <<
														"optional mplayer params = \"" << params.opt_params.c_str() << "\"\n" <<
														"delay frames " << params.delay << "\n"  <<
														"enable video dup " << params.enable_video_dup << "\n"	
														);


		
	
	}

	~mplayer_producer()
	{
		try 
		{
			mdb("~mplayer_producer");
			cmn_->stopAll();
			mplayer_process_->stop();
			fake_mplayer_consumer_->stop();

			mdb("destruction objects");

			mdb("delete fake_mplayer_consumer_");
			if (fake_mplayer_consumer_) delete fake_mplayer_consumer_;
			
			mdb("delete video_input_");
			if (video_input_) delete video_input_;

			mdb("delete audio_input_");
			if (audio_input_) delete audio_input_;

			mdb("delete mplayer_process_");
			if (mplayer_process_) delete mplayer_process_;

			mdb("delete cmn_");
			if (cmn_) delete cmn_;

			mdb("delete muxer_");
			if (muxer_) delete muxer_;

			mdb("~mplayer_producer OK");
		}
		catch (...)
		{
			CASPAR_LOG_CURRENT_EXCEPTION();
		}
	}

	mplayer::muxer *makeMuxer(int w, int h, int delay)
	{
		auto audio_channel_layout = core::default_channel_layout_repository().get_by_name(L"STEREO");
		return new mplayer::muxer(frame_factory_, audio_channel_layout, w, h, delay);
	}

	shared_ptr<core::basic_frame> get_last_video()
	{
		return muxer_->get_last();
	}

	shared_ptr<core::basic_frame> pollFrame(bool dropMode = false)
	{
		if (!cmn_->input_initialized()) return muxer_->get_last(); 

		if (video_input_->fps() == 0.0f)
		{
			mwarn("mplayer receive - unknown fps");
			return muxer_->get_last();
		}

		if (dropMode) mdb("drop frame!");
			 
		video_buffer video;
		core::audio_buffer audio;
		bool videook = true;
		bool audiook = true;

		temptime_ += tempstep_;		
		if (!dropMode) drop_ctr_ += tempstep_;
		while (temptime_ >= tempstep_)
		{
			mtr("mplayer receive - pop video...");
			
			bool dopop = true;
			if ( enable_video_dup_ && (last_video_buffer_ != nullptr) )
			{ 
				if (cmn_->get_video_late_frames() > 0) 
				{
					mdb("             duplicate video frame");
					dopop = false;
					cmn_->decrease_video_late_frames();
					cmn_->cancel_wait_unsync();
				}
			}
		
			if (dopop) 
			{
				videook = video_input_->try_pop(video);
				if (videook) last_video_buffer_ = video;
			}
			temptime_ -= (1000.0f / video_input_->fps());
		}

		mtr("mplayer receive - pop audio...");
		if (!dropMode) audiook = audio_input_->try_pop(audio);
		else 
		{
			audio_input_->drop_frame();
			audiook = true;
		}
		

		if (dropMode) 
		{
			drop_ctr_ = 0;
			return muxer_->get_last();
		}


		if (videook) 
		{
			if (last_video_buffer_ == nullptr) CASPAR_LOG(info) << "PUT nullptr";
			muxer_->push( last_video_buffer_ );
		}
		else if ( audio_input_->output_size() >= sync_frames_ ) 
		{
			mdb("mplayer unsync - no video. Play only audio frame");
			muxer_->push( last_video_buffer_ );
		} 

		if (!videook) CASPAR_LOG(error) << ("video is not ready");



		if (audiook)
		{
			muxer_->push( audio );
		} 
		else if ( video_input_->output_size() >= sync_frames_ ) 
		{
			mdb("mplayer unsync - no audio. Play only video frame");
			core::audio_buffer a;
			muxer_->push( a );
		} 
		else mtr("mplayer receive - audio is not ready");

		if (!audiook) CASPAR_LOG(error) << ("audio is not ready");

		auto f = muxer_->poll();
		if (f != nullptr)
		{
			mtr("mplayer receive - poll OK");	
		}
		else
		{
			mdb("mplayer receive - frame is not available");
		}

		return f;
	}

	void monitor_params()
	{
		receive_counter_++;
		DWORD t = GetTickCount();
		if (t - tmp_time_ >= 1000)
		{
			tmp_time_ = t;
			receive_fps_int_ = receive_counter_;
			mdb("recv_fps = " << receive_fps_int_);
			receive_counter_ = 0;
		}

		if (++debug_counter_ >= 5)
		{
			mdb("\nBUFS: V:" << video_input_->output_size() <<"   A:" << audio_input_->output_size() <<"   ov_time=" << buffer_overflow_time);				
			debug_counter_ = 0;
		}
	}

	inline bool buffer_overflow()
	{
		return ((video_input_->output_size() > cmn_->frames_max) || (audio_input_->output_size() > cmn_->frames_max));
	}

	inline bool buffer_over_enough()
	{
		return ((video_input_->output_size() > cmn_->frames_enough) || (audio_input_->output_size() > cmn_->frames_enough));
	}

	virtual safe_ptr<core::basic_frame> receive(int hints) override
	{		
		tbb::mutex::scoped_lock lock(receive_mutex_);

		monitor_params();

		try
		{
			mtr("mplayer receive...");

			//if receive() called from internal fake_mplayer_consumer_
			if (GetCurrentThreadId() == fake_mplayer_consumer_->threadId())
			{
				if (!fake_mplayer_consumer_->do_receive_calls) return core::basic_frame::empty();
			}
			else
			{
				fake_mplayer_consumer_->on_external_receive_call();
			}


			//refresh threads
			if (!mplayer_process_->working()) mplayer_process_->start_thread();
			video_input_->start_thread();
			audio_input_->start_thread();
		

			//set buffer ready
			if ((video_input_->output_size() >= cmn_->frames_enough) || (audio_input_->output_size() >= cmn_->frames_enough))
				cmn_->buff_ready = true;


			//check sync
			cmn_->checkForDesync(sync_frames_);


			//poll/drop frame
			if (cmn_->buff_ready)
			{
				if (buffer_overflow())
				{
					buffer_overflow_time += (1 / cmn_->out_fps_);
				} else buffer_overflow_time = 0;

				if (buffer_overflow_time >= buffer_overflow_time_max)
				{
					if (buffer_overflow()) do_drop_ = true;
					buffer_overflow_time = 0;
				}

				if (do_drop_)
				{
					if (buffer_over_enough())
					{
						if (drop_ctr_ >= drop_frames_interval_)
						{
							pollFrame(true); // drop frame
						}
					} else do_drop_ = false;
				}

				auto f = pollFrame();
				last_frame_ = make_safe_ptr( f );
				return last_frame_;
			}
			else
			{
				auto f = get_last_video();
				last_frame_ = make_safe_ptr( f );
				return last_frame_;
			}
		}
		catch (...)
		{
			CASPAR_LOG_CURRENT_EXCEPTION();
			return core::basic_frame::empty();
		}
	}

	virtual safe_ptr<core::basic_frame> last_frame() const override
	{
		return last_frame_;
	}

	virtual safe_ptr<core::basic_frame> create_thumbnail_frame() override
	{
		return core::basic_frame::empty();
	}

	virtual uint32_t nb_frames() const override
	{
		return std::numeric_limits<uint32_t>::max();
	}

	virtual boost::unique_future<std::wstring> call(const std::wstring& param) override
	{
		boost::promise<std::wstring> promise;
		promise.set_value(L"");
		return promise.get_future();
	}
				
	virtual std::wstring print() const override
	{
		return L"mplayer producer";
	}

	boost::property_tree::wptree info() const override
	{
		boost::property_tree::wptree info;
		info.add(L"type",				L"mplayer-raw-producer");
		info.add(L"filename",			resource_name_);
		info.add(L"width",				cmn_->width);
		info.add(L"height",				cmn_->height);
		info.add(L"input fps",			video_input_->fps());
		info.add(L"mplayer-working",	mplayer_process_->working());
		info.add(L"receive-fps-int",	receive_fps_int_);	
		info.add(L"audio-buffer-frames",  audio_input_->output_size());
		info.add(L"video-buffer-frames",  video_input_->output_size());
		info.add(L"missed-rtp-packets", mplayer_process_->missed_packets());

		return info;
	}

	core::monitor::subject& monitor_output()
	{
		return monitor_;
	}
};

safe_ptr<core::frame_producer> create_producer(
		const safe_ptr<core::frame_factory>& frame_factory,
		const core::parameters& params)
{		
	auto resource_name = params.at_original(0);


	if ((!is_url_address(resource_name)) || (env::mplayer_bin_path() == L"")) return core::frame_producer::empty(); 	

	if (resource_name.find(L"_A") == resource_name.size() - 2) 
	{
		mlg("could not create mplayer producer '_A'");
		return core::frame_producer::empty(); 
	}

	if (resource_name.find(L"_ALPHA") == resource_name.size() - 6) 
	{
		mlg("could not create mplayer producer '_ALPHA'");
		return core::frame_producer::empty(); 
	}

	auto infps = params.get(L"FPS", (double)0);
	auto w = params.get(L"WIDTH", (int)SWidth);
	auto h = params.get(L"HEIGHT", (int)SHeight);
	auto buff_time_max = params.get(L"BUFF_TIME_MAX", (int)2000);
	auto buff_time_enough = params.get(L"BUFF_TIME_ENOUGH", (int)1000);
	bool vfps = params.has(L"VFPS");
	std::string opt_params = toStdStr(params.get(L"PARAMS", L""));
	double botmax = params.get(L"BOTMAX", (double)0.0f);
	bool noaudio = params.has(L"NOAUDIO");
	bool novideo = params.has(L"NOVIDEO");
	int syncfr = params.get(L"SYNC_FRAMES", (int)10);
	int vfps_jt = params.get(L"JT", (int)3); 
	int delay = params.get(L"DELAY", (int)0); 

	std::string vfps_vals_param = toStdStr(params.get(L"VFPS_VALUES", L""));
	std::vector<double> vfps_vals_list;	
	if (!vfps_vals_param.empty())
	{
		vector< std::string > list;	
		boost::algorithm::split(list, vfps_vals_param, boost::algorithm::is_any_of(","));
		for (unsigned int i = 0; i < list.size(); i++)
		{	
			double val = atof(list[i].c_str());
			if (val != 0) vfps_vals_list.push_back( val );
		}
	}


	std::wstring d = env::mplayer_debug();
	enable_debug_log = (d.find(L"db") != std::wstring::npos);
	enable_trace_log = (d.find(L"tr") != std::wstring::npos);
	enable_fps_log = (d.find(L"fps") != std::wstring::npos);
	enable_mplayer_log = (d.find(L"stdoutlog") != std::wstring::npos);

	mplayer_producer_params prm;
	prm.resource_name = resource_name;
	prm.buff_time_max = buff_time_max;
	prm.buff_time_enough = buff_time_enough;
	prm.width = w;
	prm.height = h;
	prm.infps = infps;
	prm.vfps = vfps;
	prm.vfps_vals_list = vfps_vals_list;
	prm.opt_params = opt_params;
	prm.bot_max = botmax;
	prm.noaudio = noaudio;
	prm.novideo = novideo;
	prm.syncfr = syncfr;
	prm.vfps_jt = vfps_jt;
	prm.delay = delay;
	prm.drop_frames_interval = params.get(L"DROP_INTERVAL", 500.0f);
	prm.enable_video_dup = params.has(L"VIDEO_DUP");
	prm.do_not_audio_click = params.has(L"DONT_CLICK");
	prm.unsync_patience_frames = params.get(L"UNSYNC_PATIENCE", (int)30);
	prm.audioDroperParams.borderLatencyMs = params.get(L"AD_BORDERLATENCYMS", AD_DEFAULT_borderLatencyMs);
	prm.audioDroperParams.channels = SAudioChannels;
	prm.audioDroperParams.frameSamples = params.get(L"AD_FRAMESAMPLES", AD_DEFAULT_frameSamples);
	prm.audioDroperParams.frameTimeNominalMs = params.get(L"AD_FRAMETIMENOMINALMS", AD_DEFAULT_frameTimeNominalMs);
	prm.audioDroperParams.IgnoreDropRestSamples = params.get(L"AD_IGNOREDROPRESTSAMPLES", AD_DEFAULT_IgnoreDropRestSamples);
	prm.audioDroperParams.maxDropTimeMs = params.get(L"AD_MAXDROPTIMEMS", AD_DEFAULT_maxDropTimeMs);
	prm.audioDroperParams.minDropIntervalMs = params.get(L"AD_MINDROPINTERVALMS", AD_DEFAULT_minDropIntervalMs);
	prm.audioDroperParams.noDropMode = params.has(L"AD_NODROPMODE");
	prm.audioDroperParams.samplingRate = SAudioSamplingRate;
	prm.audioDroperParams.enable = !params.has(L"AD_DISABLE");
	prm.audioDroperParams.debugLevel = params.get(L"AD_DEBUGLEVEL", 0);

	mplayer_producer_unique_num += 1;
	prm.unique_num = mplayer_producer_unique_num;

	CASPAR_LOG(info) << "Create mplayer producer num=" << prm.unique_num;

	return create_producer_destroy_proxy(make_safe<mplayer_producer>(frame_factory, prm));
}

safe_ptr<core::frame_producer> create_thumbnail_producer(
		const safe_ptr<core::frame_factory>& frame_factory,
		const core::parameters& params)
{		
	return core::frame_producer::empty();
}


}}
