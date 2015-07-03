#pragma once

#include "mplayer_includes.h"
#include "mplayerproc.h"
#include "AudioDroper.h"

using namespace std;
namespace caspar { namespace mplayer {


struct common_input_data;
struct input_audio : boost::noncopyable
{
public:
	input_audio(long unique_num, mplayer_process* mplayer, common_input_data* cmn, AudioDroperCtorParams audioDroperParams, bool empty_mode);
	void start_thread();
	bool try_pop(core::audio_buffer &audio);
	bool initialized();
	float output_size();
	void stop();
	void drop();
	bool empty_frames();
	void drop_frame();

private:
	struct implementation;
	std::shared_ptr<implementation> impl_;
};



}}