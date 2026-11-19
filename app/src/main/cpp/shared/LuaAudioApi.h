#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct lua_State;

namespace WorldRPG::Script {

/// Installs all 57 client Audio & Voice System functions (CLuaAudioDefs).
void InstallAudioLua(lua_State* L);

/// Sound element query and lifecycle hooks for LuaVMStub and ElementTree.
bool AudioSoundAlive(uint32_t id);
void AudioNoteSoundEnded(uint32_t id);
void AudioNoteSoundLength(uint32_t id, double seconds);
void AudioResetSession();
void AudioStopOwnedSounds(const std::string* owner = nullptr, uint16_t netId = 0, uint32_t generation = 0);
void AudioCollectEndedSounds(std::vector<uint32_t>& out);

}  // namespace WorldRPG::Script
