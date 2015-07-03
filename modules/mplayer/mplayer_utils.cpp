#include "stdafx.h"
#include "mplayer_utils.h"
#include "input_audio.h"
#include "input_video.h"
#include "mplayer_includes.h"

namespace caspar { namespace mplayer {

std::string toStdStr(const std::wstring s)
{
	return std::string(s.begin(), s.end());
}

std::wstring toWStr(const std::string s)
{
	return std::wstring(s.begin(), s.end());
}

void free_picture(AVPicture *pic)
{
	avpicture_free(pic);
	delete pic;
}


void common_input_data::checkForDesync(int sync_frames)
{
	if (video_input_->empty_frames() || audio_input_->empty_frames()) return;

	float vs = video_input_->output_size();
	float as = audio_input_->output_size();

	if ( (vs > 1) && (as > 1) )
	{
		if (vs - as >= sync_frames) 
		{
			mdb("mplayer unsync! Delete excess video");
			video_buffer dummy;
			for (int i = 0; i < sync_frames - 2; i++) video_input_->try_pop(dummy);
		} 
		else if (as - vs >= sync_frames) 
		{
			mdb("mplayer unsync! Delete excess audio");
			core::audio_buffer dummy;
			for (int i = 0; i < sync_frames - 2; i++) audio_input_->try_pop(dummy);
		}
	}
}

bool common_input_data::input_initialized()
{
	if (audio_input_->empty_frames()) return video_input_->initialized();
	else if (video_input_->empty_frames()) return audio_input_->initialized(); 
	else return video_input_->initialized() && audio_input_->initialized(); 
}

void common_input_data::stopAll()
{
	video_input_->stop();
	audio_input_->stop();
}

common_input_data::common_input_data(std::string resource_name, core::video_format_desc video_format_desc, int buff_time_max, int buff_time_enough, int w, int h, bool vfps, std::vector<double> vfps_vals_list)
	: resource_name_(resource_name)
	, out_fps_(video_format_desc.fps)
	, out_audio_cadence_(video_format_desc.audio_cadence)
	, frames_max( (int)(((double)buff_time_max * out_fps_) / 1000) )
	, frames_enough( (int)(((double)buff_time_enough * out_fps_) / 1000) )
	, width(w)
	, height(h)
	, variable_in_fps(vfps)
	, vfps_vals_list_(vfps_vals_list)
{
	in_fps_ = 0;
	buff_ready = false;
	//copied from ffmpeg producer:
	// Note: Uses 1 step rotated cadence for 1001 modes (1602, 1602, 1601, 1602, 1601)
	// This cadence fills the audio mixer most optimally.
	boost::range::rotate(out_audio_cadence_, std::end(out_audio_cadence_)-1);
}


void free_av_context(AVFormatContext *ctx)
{
	mdb("av_close_input_file..."); 
	av_close_input_file(ctx);
}

bool enable_debug_log = false;
bool enable_trace_log = false;
bool enable_fps_log = false;

}}
