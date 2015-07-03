#pragma once

#include "mplayer_includes.h"
#include "mplayerproc.h"

using namespace std;
namespace caspar { namespace mplayer {



struct common_input_data;
struct input_video : boost::noncopyable
{
public:
	input_video(long unique_num, mplayer_process* mplayer, common_input_data* cmn, bool empty_mode);


	void start_thread();
	bool try_pop(video_buffer &video);

	bool initialized();
	float output_size();
	float fps();

	void stop();
	void drop();
	bool empty_frames();

private:
	struct implementation;
	std::shared_ptr<implementation> impl_;
};

}}