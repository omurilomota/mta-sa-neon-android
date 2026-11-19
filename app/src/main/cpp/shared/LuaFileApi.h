#pragma once
#include <filesystem>
#include <string>

struct lua_State;
namespace WorldRPG::Script {
void InstallFileLua(lua_State* L);
bool ResolveFilePath(const char* text, std::filesystem::path& out);
}
