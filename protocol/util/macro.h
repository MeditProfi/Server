#pragma once

#include <core/parameters/parameters.h>

namespace caspar { namespace protocol {

void execute_macro(std::wstring filename, core::parameters &params, std::function<void(const std::wstring&)> listener, bool nested = false);

}}
