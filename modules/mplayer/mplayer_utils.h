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

#pragma once

#include "stdafx.h"
#include "mplayer_includes.h"


namespace caspar { namespace mplayer {


typedef std::shared_ptr<AVPicture>						video_buffer;
typedef tbb::concurrent_bounded_queue<video_buffer>		rawdata_video_queue;
typedef std::queue< core::audio_buffer >				rawdata_audio_queue;


struct input_video;
struct input_audio;
struct common_input_data
{
	const std::string resource_name_;
	const double out_fps_;
	std::vector<size_t> out_audio_cadence_;
	tbb::atomic<double> in_fps_;	
	const int frames_max;
	const int frames_enough;
	const int width;
	const int height;
	const bool variable_in_fps;
	const std::vector<double> vfps_vals_list_;

	input_video *video_input_;
	input_audio *audio_input_;

	common_input_data(std::string resource_name, core::video_format_desc video_format_desc, int buff_time_max, int buff_time_enough, int w, int h, bool vfps, std::vector<double> vfps_vals_list);

	bool input_full();
	bool input_initialized();
	void checkForDesync(int sync_frames);
	void stopAll();

	tbb::atomic<bool> buff_ready;
};


std::string toStdStr(const std::wstring s);
std::wstring toWStr(const std::string s);
void free_picture(AVPicture *pic);
void free_av_context(AVFormatContext *ctx);

struct mplayer_exception : virtual caspar_exception {};
#define mplayer_fatal_error(msg)	{BOOST_THROW_EXCEPTION(mplayer_exception() << msg_info("rtmp: ") << msg_info(msg));}
#define mlg(...)					{CASPAR_LOG(info) << "rtmp: " << __VA_ARGS__;}
#define mwarn(...)					{CASPAR_LOG(warning) << "rtmp: " << __VA_ARGS__;}
#define merr(...)					{CASPAR_LOG(error) << "rtmp: " << __VA_ARGS__;}

extern bool enable_debug_log;
extern bool enable_trace_log;
extern bool enable_fps_log;

#define mdb(...)	{ if (enable_debug_log) CASPAR_LOG(info) << "rtmp: " << __VA_ARGS__; } 
#define mdb2 mdb
#define mtr(...)	{ if (enable_trace_log) CASPAR_LOG(info) << "rtmp: " << __VA_ARGS__; } 
#define mfps(...)	{ if (enable_fps_log) CASPAR_LOG(info) << "rtmp: " << __VA_ARGS__; } 
}}
