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
* Author: Nicklas P Andersson
*/

#pragma once

#include <memory>
#include <string>
#include <iostream>

#include <common/log/log.h>
#include <boost/algorithm/string/replace.hpp>

namespace caspar { namespace IO {

class ClientInfo 
{
protected:
	ClientInfo(){}

public:
	virtual ~ClientInfo(){}

	virtual void Send(const std::wstring& data) = 0;
	virtual void Disconnect() = 0;
	virtual std::wstring print() const = 0;

	std::wstring		currentMessage_;
};
typedef std::shared_ptr<ClientInfo> ClientInfoPtr;

struct ConsoleClientInfo : public caspar::IO::ClientInfo 
{
	void Send(const std::wstring& data)
	{
		std::wcout << (L"#" + caspar::log::replace_nonprintable_copy(data, L'?'));
	}
	void Disconnect(){}
	virtual std::wstring print() const {return L"Console";}
};

struct MacroClientInfo : public caspar::IO::ClientInfo 
{
	const std::wstring filename_;
	MacroClientInfo(std::wstring filename) : filename_(filename) {}

	void Send(const std::wstring& data)
	{
		auto esc_data = data;
		boost::replace_all(esc_data, "\r", "\\r");
		boost::replace_all(esc_data, "\n", "\\n");

		if (esc_data.size() <= 512)
		{
			CASPAR_LOG(info) << "Received answer to macro " << filename_ << ": " << esc_data;
		}
		else
		{
			CASPAR_LOG(info) << "Received answer to macro " << filename_ << ": more than 512 characters";
		}
	}
	void Disconnect(){}
	virtual std::wstring print() const {return L"Macro " + filename_;}
};

struct StartupClientInfo : public caspar::IO::ClientInfo 
{
	void Send(const std::wstring& data)
	{
		std::wcout << L"Executing startup command: " << data.c_str();
	}
	void Disconnect(){}
	virtual std::wstring print() const {return L"Startup";}
};

}}
