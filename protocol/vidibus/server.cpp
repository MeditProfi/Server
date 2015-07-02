/*
*[VidiBus] client by Mediaitprofi.
*VidiBus feature make possible to receive commands from master devise via RS-485, 
*casparCG acts as slave in this interaction. ACMP command to receive is packed into 
*VidiBus protocol packet which contain address of slave device and LRC check sum. 
*Address of particular slave CasparCG should be in Serial section of config. 
*In this section also should be other serial connection properties.
*01.07.2015
*/
#include "../stdafx.h"

#include "server.h"

#include <common/log/log.h>
#include <common/env.h>
#include <common/utility/assert.h>
#include <common/exception/exceptions.h>
#include <common/exception/win32_exception.h>

#include <boost/array.hpp>
#include <boost/asio.hpp>
#include <boost/assign.hpp>
#include <boost/foreach.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/regex.hpp>
#include <boost/thread.hpp>
#include <boost/algorithm/string/predicate.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <vector>
#include <sstream>

namespace caspar { namespace protocol { namespace vidibus {

struct server::impl
{
	_declspec(align(64)) boost::array<char, 1024>	input_buffer;
	int												vidibus_address;
	boost::asio::io_service							io;
    boost::asio::serial_port						serial_port;
	const std::function<void(const std::string&)>   listener;
	std::vector<char>								storage_buffer;
	boost::thread									thread;

	impl(const std::string&						 port,
			int									 address,
		 std::function<void(const std::string&)> listener)
		: vidibus_address(address)
		, io()
		, serial_port(io, port)
		, listener(std::move(listener))
		, thread(boost::bind(&impl::run, this))
	{
		input_buffer.fill('\r');
		storage_buffer.resize(1024, '\r');
		storage_buffer.resize(0);

		read_next_command();

		CASPAR_LOG(info) << L"[VIDIBus] Initialized with address: " << vidibus_address;
	}

	~impl()
	{
		io.stop();
	}

	void run()
	{
		win32_exception::install_handler();

		try
		{				
			io.run();
		}
		catch(...)
		{
			CASPAR_LOG_CURRENT_EXCEPTION();
		}
	}

	void read_next_command()
	{
		boost::asio::async_read(serial_port, 
								boost::asio::buffer(input_buffer),
								boost::bind(&impl::completion_condition, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred),
								boost::bind(&impl::handler, this, boost::asio::placeholders::error, boost::asio::placeholders::bytes_transferred));
	}

	bool completion_condition(const boost::system::error_code& error, 
							  std::size_t					   bytes_transferred)
	{
		CASPAR_VERIFY(bytes_transferred < input_buffer.size());

		const auto begin = boost::begin(input_buffer);
		const auto end   = boost::begin(input_buffer) + bytes_transferred;
		
		return error || std::find(begin, end, '\r') != end;
	}

	void handler(const boost::system::error_code& error,
				 std::size_t					  bytes_transferred)
	{		
		CASPAR_VERIFY(bytes_transferred < input_buffer.size());

		if(error)
		{
			CASPAR_LOG(info) << "[VIDIBus] Bad data in com port.";
			read_next_command();
			return;
		}

		std::wstringstream data_str;
		for (unsigned n = 0; n < bytes_transferred; ++n)
			data_str << std::hex << input_buffer[n];

		CASPAR_LOG(trace) << "[VIDIBus] Received " << data_str.str();

		storage_buffer.insert(boost::end(storage_buffer), boost::begin(input_buffer), boost::begin(input_buffer) + bytes_transferred);
		storage_buffer.erase(std::remove_if(boost::begin(storage_buffer), boost::end(storage_buffer), [](char c) -> bool
		{
			return c == '\n' || c == '\t'; //stop chars
		}), boost::end(storage_buffer));
		std::fill_n(boost::begin(input_buffer), bytes_transferred, '\r');

		while (true)
		{
			auto endMsg = std::find(boost::begin(storage_buffer), boost::end(storage_buffer), '\r');

			if (endMsg==boost::end(storage_buffer))
			{
				//CASPAR_LOG(trace) << "[VIDIBus] Reached end of proccessing";
				break;
			}

			endMsg +=1;

			//processing Message example ==0100MATRIX OFF45\r\n
			//try to find beginning of the message
			auto beginMsg = std::find(boost::begin(storage_buffer), endMsg, '=');// find == in the begining of the message
			if (beginMsg != boost::end(storage_buffer)) 
				if (beginMsg[1] != '=')
					beginMsg = endMsg;

			if (beginMsg == endMsg)
			{
				CASPAR_LOG(warning) << "[VIDIBus] Received incorrect message";
				break;
			}
	
			std::vector<char> message_buffer(beginMsg, endMsg - 3); // ignoring last \r and check summ
			int lrc = (endMsg[-3] - '0') * 16 + endMsg[-2] - '0';
			CASPAR_LOG(trace) << "[VIDIBus] Received LRC " << lrc; 
			storage_buffer.erase(boost::begin(storage_buffer), endMsg);

			if (message_buffer.size() < 6) //mininum size of a message 10 bytes ==01000
			{
				CASPAR_LOG(warning) << "[VIDIBus] Received message is too small";
				continue;
			} 

			int this_lrc = GetLRC(message_buffer);
			CASPAR_LOG(info) << "[VIDIBus] Message LRC " << this_lrc;

			if (lrc != this_lrc)
			{
				CASPAR_LOG(warning) << "[VIDIBus] Received message is damaged (failed LRC check)";
				continue;	
			}

			int addr = (message_buffer[2] - '0')* 16 + message_buffer[3] - '0';
			if (addr != vidibus_address && addr !=0)
			{
				CASPAR_LOG(warning) << "[VIDIBus] Received message addressed to another slave("<< addr <<"): ignoring";
				continue;
			}

			int commandType = message_buffer[5] - '0'; // 0 - do, 1 - talk

			if (commandType == 0)
			{
				CASPAR_LOG(info) << "[VIDIBus] Processing command.";
				std::string command = std::string(boost::begin(message_buffer) + 6, boost::end(message_buffer));
				//process comand string
				listener(command);
			}
			else
			{
				//send some response to master
				CASPAR_LOG(info) << "[VIDIBus] Sending response.";
			}
		}
		
		read_next_command();
	}

	BYTE GetLRC(std::vector<char> vcData)
	{
		BYTE chLRC = 0;
		BOOST_FOREACH(char el, vcData)
		{
			chLRC ^= el;
		}
		return chLRC;
	}

	template<typename T>
	unsigned int hex_string_to_decimal(const T& str)
	{
		std::stringstream ss;
		ss << std::hex << std::string(boost::begin(str), boost::end(str));

		unsigned int value;
		ss >> value;
		return value;	
	}

};
	
server::server(const std::string&					   port,
			   int										address,
		       std::function<void(const std::string&)> listener)
	: impl_(new impl(port, address,
					 listener))
{	
}

server::server(server&& other)
	: impl_(std::move(other.impl_))
{

}

server::~server()
{
}

server& server::operator=(server&& other)
{
	impl_ = std::move(other.impl_);
	return *this;
}

void server::set_baud_rate(unsigned int value)
{
	impl_->serial_port.set_option(boost::asio::serial_port_base::baud_rate(value));
}

void server::set_flow_control(flow_control::type value)
{
	if(value == flow_control::none)
		impl_->serial_port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::none));
	else if(value == flow_control::software)
		impl_->serial_port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::software));
	else if(value == flow_control::hardware)
		impl_->serial_port.set_option(boost::asio::serial_port_base::flow_control(boost::asio::serial_port_base::flow_control::hardware));
}

void server::set_parity(parity::type value)
{
	if(value == parity::none)
		impl_->serial_port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
	else if(value == parity::even)
		impl_->serial_port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::even));
	else if(value == parity::odd)
		impl_->serial_port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::odd));
}

void server::set_stop_bits(stop_bits::type value)
{
	if(value == stop_bits::one)
		impl_->serial_port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));
	else if(value == stop_bits::onepointfive)
		impl_->serial_port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::onepointfive));
	else if(value == stop_bits::two)
		impl_->serial_port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::two));
}

void server::set_character_size(unsigned int value)
{
	impl_->serial_port.set_option(boost::asio::serial_port_base::character_size(value));
}


}}}