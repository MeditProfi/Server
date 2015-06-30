#include "../StdAfx.h"

#include <common/env.h>
#include <core/parameters/parameters.h>

#include <boost/thread.hpp>
#include <boost/regex.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/locale.hpp>
#include <boost/range/adaptor/transformed.hpp>
#include <boost/algorithm/string/regex.hpp>

namespace caspar { namespace protocol {

void append_macro_lines(std::wstring &str, std::vector<std::wstring> &lines)
{
	std::vector<std::wstring> new_lines;
	boost::split(new_lines, str, boost::is_any_of(L"\n"));
	BOOST_FOREACH (auto l, new_lines)
	{
		boost::trim(l);
		if (!l.empty()) lines.push_back(l);
	}
}

std::map< std::wstring, std::vector<std::wstring> > get_macro_parts(std::wstring &str)
{
	std::map< std::wstring, std::vector<std::wstring> > parts;

	boost::wregex xRegEx(L"^\\s*Part:\\s*(?<event>.*?)\\s*\\{(?<data>.*?)\\}");
	for(auto it = boost::wsregex_iterator(str.begin(), str.end(), xRegEx); it != boost::wsregex_iterator(); ++it)
	{
		auto ev = boost::to_upper_copy((*it)["event"].str());
		auto data = (*it)["data"].str();

		auto part_it = parts.find(ev);
		if (part_it == parts.end())
		{
			parts[ev] = std::vector<std::wstring>();
			part_it = parts.find(ev);
		}

		std::vector<std::wstring> &lines = part_it->second;
		append_macro_lines(data, lines);
	}

	return parts;
}

std::map< std::wstring, std::wstring > get_macro_const(std::map< std::wstring, std::vector<std::wstring> > &parts)
{
	std::map< std::wstring, std::wstring > res;

	auto part = parts.find(L"CONST");
	if (part == parts.end()) return res;

	auto lines = part->second;

	BOOST_FOREACH(auto line, lines)
	{
		static boost::wregex expr(L"SET\\s+(?<KEY>\\w+)\\s*=\\s*(?<VALUE>.*)", boost::wregex::icase);
		
		boost::wsmatch what;
		if (boost::regex_match(line, what, expr))
		{
			auto key = what["KEY"].str();
			auto value = what["VALUE"].str();

			boost::trim(key);
			boost::trim(value);

			res[key] = value;
		}
	}

	return res;
}



//=======================================================
/*Following functions was copied from AMCPProtocolStrategy. TODO: make it common*/
std::size_t TokenizeMessage(const std::wstring& message, std::vector<std::wstring>* pTokenVector)
{
	//split on whitespace but keep strings within quotationmarks
	//treat \ as the start of an escape-sequence: the following char will indicate what to actually put in the string

	std::wstring currentToken;

	char inQuote = 0;
	bool getSpecialCode = false;

	for(unsigned int charIndex=0; charIndex<message.size(); ++charIndex)
	{
		if(getSpecialCode)
		{
			//insert code-handling here
			switch(message[charIndex])
			{
			case TEXT('\\'):
				currentToken += TEXT("\\");
				break;
			case TEXT('\"'):
				currentToken += TEXT("\"");
				break;
			case TEXT('n'):
				currentToken += TEXT("\n");
				break;
			default:
				break;
			};
			getSpecialCode = false;
			continue;
		}

		if(message[charIndex]==TEXT('\\'))
		{
			getSpecialCode = true;
			continue;
		}

		if(message[charIndex]==' ' && inQuote==false)
		{
			if(currentToken.size()>0)
			{
				pTokenVector->push_back(currentToken);
				currentToken.clear();
			}
			continue;
		}

		if(message[charIndex]==TEXT('\"'))
		{
			inQuote ^= 1;

			if(currentToken.size()>0)
			{
				pTokenVector->push_back(currentToken);
				currentToken.clear();
			}
			continue;
		}

		currentToken += message[charIndex];
	}

	if(currentToken.size()>0)
	{
		pTokenVector->push_back(currentToken);
		currentToken.clear();
	}

	return pTokenVector->size();
}

std::wstring read_utf8_file_tmp(const boost::filesystem::path& file)
{
	std::wstringstream result;
	boost::filesystem::wifstream filestream(file);

	if (filestream) 
	{
		// Consume BOM first
		filestream.get();
		// read all data
		result << filestream.rdbuf();
	}

	return result.str();
}

std::wstring read_latin1_file_tmp(const boost::filesystem::path& file)
{
	boost::locale::generator gen;
	gen.locale_cache_enabled(true);
	gen.categories(boost::locale::codepage_facet);

	std::stringstream result_stream;
	boost::filesystem::ifstream filestream(file);
	filestream.imbue(gen("en_US.ISO8859-1"));

	if (filestream)
	{
		// read all data
		result_stream << filestream.rdbuf();
	}

	std::string result = result_stream.str();
	std::wstring widened_result;

	// The first 255 codepoints in unicode is the same as in latin1
	auto from_signed_to_signed = std::function<unsigned char(char)>(
		[] (char c) { return static_cast<unsigned char>(c); }
	);
	boost::copy(
		result | boost::adaptors::transformed(from_signed_to_signed),
		std::back_inserter(widened_result));

	return widened_result;
}

std::wstring read_file_tmp(const boost::filesystem::path& file)
{
	static const uint8_t BOM[] = {0xef, 0xbb, 0xbf};

	if (!boost::filesystem::exists(file))
	{
		return L"";
	}

	if (boost::filesystem::file_size(file) >= 3)
	{
		boost::filesystem::ifstream bom_stream(file);

		char header[3];
		bom_stream.read(header, 3);
		bom_stream.close();

		if (std::memcmp(BOM, header, 3) == 0)
			return read_utf8_file_tmp(file);
	}

	return read_latin1_file_tmp(file);
}
//=======================================================

inline std::wstring macro_get_data(std::wstring path)
{
	return read_file_tmp(boost::filesystem::path(path));
}

inline bool macro_condition(std::wstring cond)
{
	std::vector<std::wstring> strs;
	boost::algorithm::split_regex(strs, cond, boost::wregex(L"=="));

	if (strs.size() == 2)
	{
		boost::trim(strs[0]);
		boost::trim(strs[1]);

		auto first =  boost::to_upper_copy( strs[0] );
		auto second = boost::to_upper_copy( strs[1] );

		return first == second;
	}

	return false;
}

void macro_replace_presets(std::wstring &line)
{
	boost::replace_all(line, L";quot;", L"\"");
}

void execute_macro(std::wstring filename, core::parameters &params, std::function<void(const std::wstring&)> listener, bool nested)
{
	for (auto it = boost::filesystem::directory_iterator(env::macros_folder()); it != boost::filesystem::directory_iterator(); ++it)
	{
		if (boost::starts_with(it->path().filename().wstring(), filename))
		{
			filename = it->path().wstring();
			break;
		}
	}

	std::wfstream file(filename, std::ios::in);
	if (!file.is_open()) BOOST_THROW_EXCEPTION(file_not_found() << msg_info("Could not open macro: " + narrow(filename)));
	std::wstringstream strStream;
	strStream << file.rdbuf();
	std::wstring str = strStream.str();
	file.close();

	auto parts = get_macro_parts(str);
	auto macro_const = get_macro_const(parts);
	std::vector<std::wstring> lines;
	if (parts.size())
	{
		if (params.size() < 2) BOOST_THROW_EXCEPTION(null_argument() << msg_info("missing part name: 2nd parameter for macro: " + narrow(filename)));	 
		auto part_name = boost::to_upper_copy(params[1]);
		auto part = parts.find(part_name);
		if (part == parts.end()) BOOST_THROW_EXCEPTION(null_argument() << msg_info("cannot find part " + narrow(part_name) + " in macro: " + narrow(filename)));
		lines = part->second;
	}
	else
	{
		append_macro_lines(str,lines);
	}

	CASPAR_LOG(info) << "Executing macro: " << widen(filename) << (nested ? " (nested -> answer will be send to parent macro)" : "");

	BOOST_FOREACH(auto line, lines)
	{
		static boost::wregex sleep_expr(L"(WAIT|SLEEP|DELAY) (?<MS>\\d+)", boost::wregex::icase);
		static boost::wregex single_line_comment_expr(L"^\\s*#.*");
		static boost::wregex invoke_expr(L"(INVOKE|EVAL|EXECUTE)\\s+(?<MACRO>\\w+)\\s+(?<PARAMS>.*)", boost::wregex::icase);
		static boost::wregex getdata_expr(L"GETDATA\\s*\\((?<PATH>.*?)\\)", boost::wregex::icase);
		static boost::wregex if_expr(L"IF\\s*\\((?<COND>.*?)\\)\\s*(?<ACTION>.*?)\\s*ELSE\\s*(?<ELSE>.*)", boost::wregex::icase);

		//commented lines
		line = boost::regex_replace(line, single_line_comment_expr, L"");

		//resolve params
		for (size_t i = 0; i < params.size(); i++)
			boost::replace_all(line, L"$" + std::to_wstring((long long)i), params.at_original(i));

		//resolve const
		for (auto it = macro_const.begin(); it != macro_const.end(); ++it)
			boost::replace_all(line, it->first, it->second);




		boost::wsmatch what;
		//placeholding data variables
		while (boost::regex_search(line, what, getdata_expr))
		{
			auto path = what["PATH"].str();
			auto data = macro_get_data(env::data_folder() + path + L".ftd");
			line = boost::regex_replace(line, getdata_expr, data, boost::format_first_only);	
		}

		//apply "if" statement if exists
		if (boost::regex_search(line, what, if_expr))
		{
			auto cond = what["COND"].str();
			auto action = what["ACTION"].str();
			auto el = what["ELSE"].str();

			line = macro_condition(cond) ? action : el;
		}

		//remove tabulations
		boost::replace_all(line, L"\t", L" ");

		//replace preset values
		if (line.find(L"USE_PRESETS") != std::wstring::npos)
		{
			boost::replace_all(line, L"USE_PRESETS", L"");
			macro_replace_presets(line);
		}
		
		
		if (boost::regex_match(line, what, sleep_expr))
			boost::this_thread::sleep(boost::posix_time::milliseconds(boost::lexical_cast<int>(what["MS"].str())));
		else
		if (boost::regex_match(line, what, invoke_expr))
		{
			auto fname = what["MACRO"].str();
			auto nparams_str = what["PARAMS"].str();

			core::parameters nested_params;
			nested_params.push_back(fname);

			std::vector<std::wstring> nparams_vector;
			TokenizeMessage(nparams_str, &nparams_vector);
			BOOST_FOREACH (auto s, nparams_vector)
			{
				nested_params.push_back(s);
			}

			execute_macro(fname, nested_params, listener, true);
		} 
		else
		if (listener && !line.empty()) 
		{
			CASPAR_LOG(info) << "MACRO Executing line: " << line;
			listener(line);
		}
	}
}

}}
