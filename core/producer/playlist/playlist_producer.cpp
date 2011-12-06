/*
* Copyright (c) 2011 Sveriges Television AB <info@casparcg.com>
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

#include "../../stdafx.h"

#include "playlist_producer.h"

#include <core/producer/frame_producer.h>
#include <core/producer/frame/basic_frame.h>

#include <boost/regex.hpp>
#include <boost/property_tree/ptree.hpp>

#include <deque>

namespace caspar { namespace core {	

struct playlist_producer : public frame_producer
{				
	safe_ptr<frame_factory>				factory_;
	safe_ptr<basic_frame>				last_frame_;
	safe_ptr<frame_producer>			current_;
	bool								loop_;

	std::deque<safe_ptr<frame_producer>> producers_;

	playlist_producer(const safe_ptr<frame_factory>& factory, bool loop) 
		: factory_(factory)
		, last_frame_(basic_frame::empty())
		, current_(frame_producer::empty())
		, loop_(loop)
	{
	}

	// frame_producer
	
	virtual safe_ptr<basic_frame> receive(int hints) override
	{
		if(current_ == frame_producer::empty() && !producers_.empty())
			next();

		auto frame = current_->receive(hints);
		if(frame == basic_frame::eof())
		{
			current_ = frame_producer::empty();
			return receive(hints);
		}

		return last_frame_ = frame;
	}

	virtual safe_ptr<core::basic_frame> last_frame() const override
	{
		return disable_audio(last_frame_);
	}

	virtual std::string print() const override
	{
		return "playlist[" + current_->print() + "]";
	}	

	virtual boost::property_tree::ptree info() const override
	{
		boost::property_tree::ptree info;
		info.add("type", "playlist-producer");
		return info;
	}

	virtual uint32_t nb_frames() const  override
	{
		return std::numeric_limits<uint32_t>::max();
	}
	
	virtual boost::unique_future<std::string> call(const std::string& param) override
	{
		boost::promise<std::string> promise;
		promise.set_value(do_call(param));
		return promise.get_future();
	}	

	// playlist_producer

	std::string do_call(const std::string& param)
	{		
		static const boost::regex push_front_exp	("PUSH_FRONT (?<PARAM>.+)");		
		static const boost::regex push_back_exp	("(PUSH_BACK|PUSH) (?<PARAM>.+)");
		static const boost::regex pop_front_exp	("POP_FRONT");		
		static const boost::regex pop_back_exp		("(POP_BACK|POP)");
		static const boost::regex clear_exp		("CLEAR");
		static const boost::regex next_exp			("NEXT");
		static const boost::regex insert_exp		("INSERT (?<POS>\\d+) (?<PARAM>.+)");	
		static const boost::regex remove_exp		("REMOVE (?<POS>\\d+) (?<PARAM>.+)");	
		static const boost::regex list_exp			("LIST");			
		static const boost::regex loop_exp			("LOOP\\s*(?<VALUE>\\d?)");
		
		boost::smatch what;

		if(boost::regex_match(param, what, push_front_exp))
			return push_front(what["PARAM"].str()); 
		else if(boost::regex_match(param, what, push_back_exp))
			return push_back(what["PARAM"].str()); 
		if(boost::regex_match(param, what, pop_front_exp))
			return pop_front(); 
		else if(boost::regex_match(param, what, pop_back_exp))
			return pop_back(); 
		else if(boost::regex_match(param, what, clear_exp))
			return clear();
		else if(boost::regex_match(param, what, next_exp))
			return next(); 
		else if(boost::regex_match(param, what, insert_exp))
			return insert(boost::lexical_cast<size_t>(what["POS"].str()), what["PARAM"].str());
		else if(boost::regex_match(param, what, remove_exp))
			return erase(boost::lexical_cast<size_t>(what["POS"].str()));
		else if(boost::regex_match(param, what, list_exp))
			return list();
		else if(boost::regex_match(param, what, loop_exp))
		{
			if(!what["VALUE"].str().empty())
				loop_ = boost::lexical_cast<bool>(what["VALUE"].str());
			return boost::lexical_cast<std::string>(loop_);
		}

		BOOST_THROW_EXCEPTION(invalid_argument());
	}
	
	std::string push_front(const std::string& str)
	{
		producers_.push_front(create_producer(factory_, str)); 
		return "";
	}

	std::string  push_back(const std::string& str)
	{
		producers_.push_back(create_producer(factory_, str)); 
		return "";
	}

	std::string pop_front()
	{
		producers_.pop_front();
		return "";
	}

	std::string pop_back()
	{
		producers_.pop_back();
		return "";
	}
	
	std::string clear()
	{
		producers_.clear();
		return "";
	}

	std::string next()
	{
		if(!producers_.empty())
		{
			current_ = producers_.front();
			producers_.pop_front();
			//if(loop_)
			//	producers_.push_back(current_);
		}
		return "";
	}
	
	std::string  insert(size_t pos, const std::string& str)
	{
		if(pos >= producers_.size())
			BOOST_THROW_EXCEPTION(out_of_range());
		producers_.insert(std::begin(producers_) + pos, create_producer(factory_, str));
		return "";
	}

	std::string  erase(size_t pos)
	{
		if(pos >= producers_.size())
			BOOST_THROW_EXCEPTION(out_of_range());
		producers_.erase(std::begin(producers_) + pos);
		return "";
	}

	std::string list() const
	{
		std::string result = "<playlist>\n";
		BOOST_FOREACH(auto& producer, producers_)		
			result += "\t<producer>" + producer->print() + "</producer>\n";
		return result + "</playlist>";
	}
};

safe_ptr<frame_producer> create_playlist_producer(const safe_ptr<core::frame_factory>& frame_factory, const std::vector<std::string>& params)
{
	if(boost::range::find(params, "[PLAYLIST]") == params.end())
		return core::frame_producer::empty();

	bool loop = boost::range::find(params, "LOOP") != params.end();

	return make_safe<playlist_producer>(frame_factory, loop);
}

}}
