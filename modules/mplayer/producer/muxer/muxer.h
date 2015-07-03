#pragma once

#include "mplayer_includes.h"
#include "input_video.h"




namespace caspar { namespace mplayer {
using namespace std;

class muxer
{
public:
	muxer(const safe_ptr<core::frame_factory>& ff, core::channel_layout &acl, int w, int h, unsigned int delay);
	~muxer();
	void push(video_buffer &video);
	void push(core::audio_buffer &audio);
	shared_ptr<core::basic_frame> poll();
	shared_ptr<core::basic_frame> get_last();

	const int width;
	const int height;

private:
	const safe_ptr<core::frame_factory>				frame_factory_;
	core::channel_layout							audio_channel_layout_;
	std::queue<video_buffer>						video_buffer_;
	std::queue<core::audio_buffer>					audio_buffer_;

	video_buffer									last_video_;
	core::audio_buffer								last_audio_;
	bool											last_video_inited_;
	bool											last_audio_inited_;
	const unsigned  int								delay_frames_;

	std::shared_ptr<core::write_frame> muxer::makeFrame(video_buffer &video, core::audio_buffer &audio, bool hasVideo, bool hasAudio);
	bool pollVideoFrame(video_buffer &out);
	bool pollAudioFrame(core::audio_buffer &out);
};


}}