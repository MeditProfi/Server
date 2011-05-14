/*
* copyright (c) 2010 Sveriges Television AB <info@casparcg.com>
*
*  This file is part of CasparCG.
*
*    CasparCG is free software: you can redistribute it and/or modify
*    it under the terms of the GNU General Public License as published by
*    the Free Software Foundation, either version 3 of the License, or
*    (at your option) any later version.
*
*    CasparCG is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.

*    You should have received a copy of the GNU General Public License
*    along with CasparCG.  If not, see <http://www.gnu.org/licenses/>.
*
*/
#pragma once

#include "../gpu/host_buffer.h"	

#include <core/consumer/frame/read_frame.h>

#include <boost/noncopyable.hpp>
#include <boost/range/iterator_range.hpp>

#include <memory>
#include <vector>

#include <common/memory/safe_ptr.h>

namespace caspar { namespace mixer {
	
class gpu_read_frame : public core::read_frame
{
public:
	gpu_read_frame(safe_ptr<const host_buffer>&& image_data, std::vector<short>&& audio_data);

	const boost::iterator_range<const unsigned char*> image_data() const;
	const boost::iterator_range<const short*> audio_data() const;
	
private:
	struct implementation;
	std::shared_ptr<implementation> impl_;
};

}}