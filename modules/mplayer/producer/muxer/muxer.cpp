#include "../../stdafx.h"
#include "muxer.h"


using namespace std;
namespace caspar { namespace mplayer {

muxer::muxer(const safe_ptr<core::frame_factory>& ff, core::channel_layout &acl, int w, int h, unsigned int delay_frames):
	frame_factory_(ff),
	audio_channel_layout_(acl),
	width(w),
	height(h),
	last_video_inited_(false),
	last_audio_inited_(false),
	delay_frames_(delay_frames)
{ 

}

muxer::~muxer()
{
	mdb("~muxer");
}

void muxer::push(video_buffer &video)
{
	video_buffer_.push(video);
}

void muxer::push(core::audio_buffer &audio)
{
	audio_buffer_.push(audio);
}

shared_ptr<core::basic_frame> muxer::poll()
{
	bool v = pollVideoFrame(last_video_);
	bool a = pollAudioFrame(last_audio_);
	if (v) last_video_inited_ = true;
	if (a) last_audio_inited_ = true;
		
	if (last_video_inited_ && last_audio_inited_)
	{
		return makeFrame(last_video_, last_audio_, true, a); 
	}
	else if (last_video_inited_)
	{
		core::audio_buffer empty_audio;
		return makeFrame(last_video_, empty_audio, true, false); 
	}
	else
	{
		return core::basic_frame::empty();
	}
}


shared_ptr<core::basic_frame> muxer::get_last()
{
	if (last_video_inited_)
	{
		core::audio_buffer empty_audio;
		return makeFrame(last_video_, empty_audio, true, false); 
	}
	else
	{
		return core::basic_frame::empty();
	}
}


std::shared_ptr<core::write_frame> muxer::makeFrame(video_buffer &video, core::audio_buffer &audio, bool hasVideo, bool hasAudio)
{
	core::pixel_format_desc desc;
	desc.pix_fmt = core::pixel_format::bgra;
	desc.planes.push_back(core::pixel_format_desc::plane(width, height, 4));

	std::shared_ptr<core::write_frame> frame = frame_factory_->create_frame(this, desc, audio_channel_layout_);

	if ((video != nullptr) && (hasVideo))
	{
		std::copy_n(video->data[0], frame->image_data().size(), frame->image_data().begin());
	} else memset( frame->image_data().begin(), 0, frame->image_data().size() );

	if (hasAudio) frame->audio_data() = audio;
	frame->commit();

	return frame;
}



bool muxer::pollVideoFrame(video_buffer &out)
{
	
	if (video_buffer_.size() > delay_frames_)
	{
		out = video_buffer_.front();
		video_buffer_.pop();
		return true;
	} 
	else return false; 
}


bool muxer::pollAudioFrame(core::audio_buffer &out)
{
	if (audio_buffer_.size() > delay_frames_)
	{
		out = audio_buffer_.front();
		audio_buffer_.pop();
		return true;
	} else return false;
}


}}