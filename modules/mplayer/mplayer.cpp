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
 
#include "StdAfx.h"

#include "producer/mplayer_producer.h"

#include <common/log/log.h>
#include <common/exception/win32_exception.h>
#include <common/env.h>

#include <core/parameters/parameters.h>
#include <core/producer/frame_producer.h>
#include <core/producer/media_info/media_info.h>
#include <core/producer/media_info/media_info_repository.h>



#if defined(_MSC_VER)
#pragma warning (disable : 4244)
#pragma warning (disable : 4603)
#pragma warning (disable : 4996)
#endif

extern "C" 
{
	#define __STDC_CONSTANT_MACROS
	#define __STDC_LIMIT_MACROS
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/avutil.h>
	#include <libavfilter/avfilter.h>
	#include <libavdevice/avdevice.h>
}

tbb::atomic<long> mplayer_producer_unique_num;

namespace caspar {
namespace mplayer {

	
void init(void)
{
	caspar::core::register_producer_factory(create_producer);
	mplayer_producer_unique_num = 0;
}


bool is_url_address(std::wstring str)
{
	boost::algorithm::to_lower(str);
	const std::vector<std::wstring> prefixes = env::mplayer_res_prefixes();

	for (unsigned int i = 0; i < prefixes.size(); i++)
		if (str.find(prefixes[i]) == 0) return true;

	return false;
}

}}