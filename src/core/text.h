// UTF-8 <-> UTF-16 helpers. Owner: A.
#pragma once

#include <string>
#include <string_view>

namespace velora {

std::string WideToUtf8(std::wstring_view w);
std::wstring Utf8ToWide(std::string_view s);

}  // namespace velora
