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

#include <common/memory/safe_ptr.h>
#include "mplayer_includes.h"
#include <boost/timer.hpp>

namespace caspar { namespace mplayer {

	class fake_mplayer_consumer
	{
	public:
		fake_mplayer_consumer(long unique_num, std::string resource_name, core::frame_producer *producer, double out_fps);
		~fake_mplayer_consumer();

		void			start_thread();
		void			stop();
					
		void			on_external_receive_call();
		inline DWORD	threadId() {return threadId_;}

		tbb::atomic<bool>					do_receive_calls;

	private:
		executor							executor_;
		core::frame_producer*				producer_;

		void								thread();
		tbb::atomic<bool>					active_;
		tbb::atomic<DWORD>					threadId_;

		boost::timer						tmr_;
		const double						time_per_frame_;
	};

}}