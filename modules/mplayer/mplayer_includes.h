#pragma once 

#include <common/env.h>
#include <common/utility/assert.h>
#include <common/diagnostics/graph.h>

#include <core/monitor/monitor.h>
#include <core/video_format.h>
#include <core/parameters/parameters.h>
#include <core/producer/frame_producer.h>
#include <core/producer/frame/frame_factory.h>
#include <core/producer/frame/basic_frame.h>
#include <core/producer/frame/frame_transform.h>
#include <core/mixer/write_frame.h>

#include <common/concurrency/executor.h>

#include <tbb/concurrent_queue.h>
#include <tbb/atomic.h>

#include "../../ffmpeg/producer/filter/filter.h"
#include "../../ffmpeg/producer/util/util.h"
#include "../../ffmpeg/producer/muxer/frame_muxer.h"
#include "../../ffmpeg/producer/muxer/display_mode.h"

#include <boost/uuid/uuid.hpp>           
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp> 
#include <boost/format.hpp>
#include <common/env.h>

#include <boost/range/algorithm_ext/push_back.hpp>
#include "debug.h"

#include <string>
#include <memory>
#include <mplayer.h>
#include <mplayer_utils.h>


using namespace tbb;
using namespace std;