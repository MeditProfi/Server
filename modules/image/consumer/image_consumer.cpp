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

#include "image_consumer.h"

#include <common/exception/win32_exception.h>
#include <common/exception/exceptions.h>
#include <common/env.h>
#include <common/log/log.h>
#include <common/utility/string.h>
#include <common/concurrency/future_util.h>

#include <core/parameters/parameters.h>
#include <core/consumer/frame_consumer.h>
#include <core/video_format.h>
#include <core/mixer/read_frame.h>

#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>

#include <FreeImage.h>
#include <vector>
#include <algorithm>

#include "../util/image_view.h"
#include <tbb/atomic.h>
#include <common/utility/base64.h>

namespace caspar { namespace image {

void write_cropped_png(
		const safe_ptr<core::read_frame>& frame,
		const core::video_format_desc& format_desc,
		const boost::filesystem::path& output_file,
		int width,
		int height)
{
	auto bitmap = std::shared_ptr<FIBITMAP>(FreeImage_Allocate(width, height, 32), FreeImage_Unload);
	image_view<bgra_pixel> destination_view(FreeImage_GetBits(bitmap.get()), width, height);
	image_view<bgra_pixel> complete_frame(const_cast<uint8_t*>(frame->image_data().begin()), format_desc.width, format_desc.height);
	auto thumbnail_view = complete_frame.subview(0, 0, width, height);

	std::copy(thumbnail_view.begin(), thumbnail_view.end(), destination_view.begin());
	FreeImage_FlipVertical(bitmap.get());
	FreeImage_SaveU(FIF_PNG, bitmap.get(), output_file.wstring().c_str(), 0);
}

struct image_consumer : public core::frame_consumer
{
	core::video_format_desc	format_desc_;
	std::wstring			filename_;
public:

	// frame_consumer

	image_consumer(const std::wstring& filename)
		: filename_(filename)
	{
	}

	virtual void initialize(const core::video_format_desc& format_desc, const core::channel_layout&, int) override
	{
		format_desc_ = format_desc;
	}

	virtual int64_t presentation_frame_age_millis() const override
	{
		return 0;
	}
	
	virtual boost::unique_future<bool> send(const safe_ptr<core::read_frame>& frame) override
	{				
		auto format_desc = format_desc_;
		auto filename = filename_;

		boost::thread async([format_desc, frame, filename]
		{
			win32_exception::ensure_handler_installed_for_thread("image-consumer-thread");

			try
			{
				auto filename2 = filename;

				if (filename2.empty())
					filename2 = env::media_folder() + widen(boost::posix_time::to_iso_string(boost::posix_time::second_clock::local_time())) + L".png";
				else
					filename2 = env::media_folder() + filename2 + L".png";

				auto bitmap = std::shared_ptr<FIBITMAP>(FreeImage_Allocate(format_desc.width, format_desc.height, 32), FreeImage_Unload);
				memcpy(FreeImage_GetBits(bitmap.get()), frame->image_data().begin(), frame->image_size());
				FreeImage_FlipVertical(bitmap.get());
				FreeImage_SaveU(FIF_PNG, bitmap.get(), filename2.c_str(), 0);
			}
			catch(...)
			{
				CASPAR_LOG_CURRENT_EXCEPTION();
			}
		});
		async.detach();

		return wrap_as_future(false);
	}

	virtual std::wstring print() const override
	{
		return L"image[]";
	}

	virtual boost::property_tree::wptree info() const override
	{
		boost::property_tree::wptree info;
		info.add(L"type", L"image-consumer");
		return info;
	}

	virtual int buffer_depth() const override
	{
		return -1;
	}

	virtual int index() const override
	{
		return 100;
	}
};

struct image_base64_consumer : public core::frame_consumer
{
	core::video_format_desc		format_desc_;
	const int					out_width_;
	const int					out_height_;
	const int					timeout_;
	bool						launched_;
	tbb::atomic<bool>			finished_;
	tbb::atomic<bool>			allow_delete_;
	boost::condition_variable	finish_cond_;
	boost::mutex				finish_mutex_;
	std::vector<char>			result_;

	image_base64_consumer(int out_width, int out_height, int timeout)
		: out_width_(out_width)
		, out_height_(out_height)
		, timeout_(timeout)
		, launched_(false)
	{
		finished_ = false;
		allow_delete_ = false;
	}

	virtual void initialize(const core::video_format_desc& format_desc, const core::channel_layout&, int) override
	{
		format_desc_ = format_desc;
	}

	virtual int64_t presentation_frame_age_millis() const override
	{
		return 0;
	}

	virtual std::wstring print() const override
	{
		return L"image-base64[" + std::to_wstring((long long)out_width_) + L":" + std::to_wstring((long long)out_height_) + L"]";
	}

	virtual boost::property_tree::wptree info() const override
	{
		boost::property_tree::wptree info;
		info.add(L"type", L"image-base64-consumer");
		return info;
	}

	virtual int buffer_depth() const override
	{
		return -1;
	}

	virtual int index() const override
	{
		return 100;
	}

	virtual boost::unique_future<bool> send(const safe_ptr<core::read_frame>& frame) override
	{		
		if (!launched_)
		{
			launched_ = true;

			//TODO: what if destroying of this consumer will be invoked earlier? This code should be refactored using boost futures!
			boost::thread async([this, frame]
			{
				auto bitmap = std::shared_ptr<FIBITMAP>(FreeImage_Allocate(this->format_desc_.width, this->format_desc_.height, 32), FreeImage_Unload);
				memcpy(FreeImage_GetBits(bitmap.get()), frame->image_data().begin(), frame->image_size());
				FreeImage_FlipVertical(bitmap.get());		

				auto out_width = this->out_width_ ? this->out_width_ : this->format_desc_.width;
				auto out_height = this->out_height_ ? this->out_height_ : this->format_desc_.height;
				auto scaled = std::shared_ptr<FIBITMAP>(FreeImage_Rescale(bitmap.get(), out_width, out_height, FILTER_LANCZOS3), FreeImage_Unload);

				auto hmem = std::shared_ptr<FIMEMORY>(FreeImage_OpenMemory(), FreeImage_CloseMemory); 
				FreeImage_SaveToMemory(FIF_PNG, scaled.get(), hmem.get(), 0);
				BYTE *mem_buffer = NULL;
				DWORD size_in_bytes = 0;
				FreeImage_AcquireMemory(hmem.get(), &mem_buffer, &size_in_bytes);

				this->result_ = to_base64_alternative(mem_buffer, size_in_bytes);
		
				this->finished_ = true;
				this->finish_cond_.notify_one();
				this->allow_delete_ = true;
			});
			async.detach();
		}

		return wrap_as_future(!allow_delete_);
	}

	virtual std::vector<char> get_custom_data() override
	{
		if (!finished_)
		{
			boost::mutex::scoped_lock cond_lock(finish_mutex_);
			auto tm = boost::get_system_time()+ boost::posix_time::milliseconds(timeout_);
			while (!finished_)
			{
				if (!finish_cond_.timed_wait(cond_lock, tm))
					BOOST_THROW_EXCEPTION(timed_out() << msg_info("image_base64_consumer: timeout error"));
			}
		}

		return result_;
	}
};


safe_ptr<core::frame_consumer> create_consumer(const core::parameters& params)
{
	if (params.size() < 1)
	{
		return core::frame_consumer::empty();
	}
	else if (params.at(0) == L"IMAGE")
	{
		std::wstring filename;

		if (params.size() > 1)
			filename = params.at(1);

		return make_safe<image_consumer>(filename);
	}
	else if (params.at(0) == L"IMAGE_BASE64")
	{
		int w = 0;
		int h = 0;

		if (params.size() > 2)
		{
			w = boost::lexical_cast<int, std::wstring>(params.at(1));
			h = boost::lexical_cast<int, std::wstring>(params.at(2));
		}

		return make_safe<image_base64_consumer>(w, h, 3000);
	}
	else return core::frame_consumer::empty();
}

}}
