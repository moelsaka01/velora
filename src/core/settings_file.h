// Read-only access to the shared settings.json (the Settings app writes it, modules read what they use). Owner: A.
// Dotted keys ("gaming.mode"); a missing file, key or type mismatch gives the default. Never throws.
#pragma once

#include <string>

namespace velora {

bool SettingBool(const char* dottedKey, bool def);
std::string SettingString(const char* dottedKey, const std::string& def);

}  // namespace velora
