#pragma once

#include "mplayer_includes.h"
#include "pipe.h"


using namespace std;
namespace caspar { namespace mplayer {

struct mplayer_process
{
public: 
	mplayer_process(long unique_num, std::string resource_name, int cache_size, common_input_data* cmn, double infps, std::string opt_params, int vfps_jt, bool enable_video_dup);
	void start_thread();
	bool working();
	void problem();
	shared_ptr<mplayer_pipe> video_pipe();
	shared_ptr<mplayer_pipe> audio_pipe();
	int cache_fullness();
	void stop();
	unsigned long missed_packets();

private:
	struct implementation;
	std::shared_ptr<implementation> impl_;
};

}}