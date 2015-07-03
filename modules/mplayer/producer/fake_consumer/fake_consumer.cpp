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

#include "stdafx.h"
#include "fake_consumer.h"
#include "mplayer_includes.h"

namespace caspar { namespace mplayer {

fake_mplayer_consumer::fake_mplayer_consumer(long unique_num, std::string resource_name, core::frame_producer* producer, double out_fps)
	: executor_(L"fake_mplayer_consumer-"+toWStr(resource_name) + L"-" + std::to_wstring((long long)unique_num)) 
	, producer_(producer)
	, time_per_frame_(1.0f / out_fps)
{
	active_ = true;
	threadId_ = 0;
	do_receive_calls = false;
}

fake_mplayer_consumer::~fake_mplayer_consumer()
{
	mdb("~fake_mplayer_consumer");
	stop();
}

void fake_mplayer_consumer::start_thread()
{
	mtr("fake_mplayer_consumer::start_thread");

	executor_.begin_invoke([this]
	{
			this->thread();
	});
}

void fake_mplayer_consumer::stop()
{
	mdb("fake_mplayer_consumer stop....");
	active_ = false; 
	executor_.clear();
	while (!executor_.empty());
	mdb("fake_mplayer_consumer stop OK");
}

void fake_mplayer_consumer::on_external_receive_call()
{
	if (do_receive_calls)
	{
		do_receive_calls = false;
		mlg("deactivate fake mplayer consumer");
	}
	tmr_.restart();
}


void fake_mplayer_consumer::thread()
{

	mdb("fake_mplayer_consumer::thread start");

	threadId_ = GetCurrentThreadId();
	tmr_.restart();

	while (active_)
	{
		double dt = time_per_frame_ - tmr_.elapsed();
		if (dt >= 0)
		{
			int idt = (int)(dt*1000);
			Sleep(idt);
		}

		{		
			if (do_receive_calls) 
			{
				tmr_.restart();
				producer_->receive(0);
			}
			else
			{
				if (tmr_.elapsed() >= 5 * time_per_frame_)
				{
					do_receive_calls = true;
					mlg("activate fake mplayer consumer");
				}
			}
		}
	}

	mdb("fake_mplayer_consumer::thread end");

}

}}
