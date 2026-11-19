// Client-side MTA Audio & Voice System (CLuaAudioDefs).
// Follows CLuaAudioDefs.cpp, CStaticFunctionDefinitions.cpp, and CClientSound.cpp.

#include "LuaAudioApi.h"

#include "LuaApiExtra.h"
#include "LuaVMStub.h"

#include "lua.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace WorldRPG::JniBridge {
void PostGameLine(int kind, const char* text);
}

namespace WorldRPG::Script {
namespace {

constexpr uint32_t kFirstSoundId = 0x61000001u;
constexpr size_t kMaxSounds = 64;

std::uint64_t DxNow() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

struct SoundSlot {
    uint32_t id = 0;
    bool alive = false;
    float volume = 1.0f;
    bool loop = false;
    bool paused = false;
    double position = 0.0;
    std::uint64_t positionAt = 0;
    float minDistance = 5.0f;
    float maxDistance = 20.0f;
    float speed = 1.0f;
    float pan = 0.0f;
    double length = 0.0;
    std::string owner;
    uint16_t ownerNetId = 0;
    uint32_t ownerGeneration = 0;

    // 3D Spatial Audio
    bool is3D = false;
    float posX = 0.0f;
    float posY = 0.0f;
    float posZ = 0.0f;
    bool throttled = true;
    bool panEnabled = true;

    // Stream / DSP properties
    float sampleRate = 44100.0f;
    float tempo = 0.0f;
    float pitch = 0.0f;
    bool reversed = false;
    double bufferLength = 0.0;
    float bpm = 0.0f;

    // Effects & Metadata (Lot 2)
    std::unordered_map<std::string, bool> fxEffects;
    std::unordered_map<std::string, std::unordered_map<std::string, double>> fxParams;
    std::unordered_map<std::string, std::string> metaTags;
};

std::mutex g_soundMu;
SoundSlot g_sounds[kMaxSounds];
uint32_t g_nextSound = kFirstSoundId;

std::mutex g_soundEndMu;
std::vector<uint32_t> g_soundEnded;

bool g_diagFrontEnd = false;
bool g_diagSoundSpace = false;
bool g_showSound = false;

// Lot 3: World & Ambient Audio, SFX & Radio
uint8_t g_radioChannel = 0;
bool g_ambientGeneral = true;
bool g_ambientGunfire = true;
std::set<std::pair<int, int>> g_disabledWorldSounds;

// Lot 4: Mission Audio Engine & Audio Events
struct MissionAudioSlot {
    uint32_t handle = 0;
    uint32_t eventId = 0;
    bool loaded = true;
    bool played = false;
    std::string owner;
};

uint32_t g_nextMissionHandle = 1;
std::unordered_map<uint32_t, MissionAudioSlot> g_missionSlots;

SoundSlot* FindSound(uint32_t id) {
    if (!id) return nullptr;
    for (auto& slot : g_sounds) {
        if (slot.alive && slot.id == id) return &slot;
    }
    return nullptr;
}

uint32_t AllocSound(const char* path, bool loop, bool is3D = false, float x = 0.0f, float y = 0.0f, float z = 0.0f, bool throttled = true) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    for (auto& slot : g_sounds) {
        if (slot.alive) continue;
        slot = SoundSlot{};
        slot.alive = true;
        slot.volume = 1.0f;
        slot.loop = loop;
        slot.paused = false;
        slot.position = 0.0;
        slot.positionAt = DxNow();
        slot.minDistance = 5.0f;
        slot.maxDistance = 20.0f;
        slot.speed = 1.0f;
        slot.pan = 0.0f;
        slot.length = 0.0;
        slot.owner = ActiveVmOwner();
        slot.ownerNetId = ActiveVmOwnerNet();
        slot.ownerGeneration = ActiveVmOwnerGeneration();
        slot.is3D = is3D;
        slot.posX = x;
        slot.posY = y;
        slot.posZ = z;
        slot.throttled = throttled;
        slot.panEnabled = true;
        slot.sampleRate = 44100.0f;
        slot.tempo = 0.0f;
        slot.pitch = 0.0f;
        slot.reversed = false;
        slot.bufferLength = 0.0;
        slot.bpm = 0.0f;

        slot.id = g_nextSound++;
        if (g_nextSound < kFirstSoundId) g_nextSound = kFirstSoundId;

        char line[512];
        if (is3D) {
            std::snprintf(line, sizeof(line), "%u|%d|%s|%.3f|%.3f|%.3f|%d", slot.id, loop ? 1 : 0, path, x, y, z, throttled ? 1 : 0);
        } else {
            std::snprintf(line, sizeof(line), "%u|%d|%s", slot.id, loop ? 1 : 0, path);
        }
        ::WorldRPG::JniBridge::PostGameLine(11, line);
        return slot.id;
    }
    return 0;
}

double SoundPosition(const SoundSlot& slot) {
    if (slot.paused) return slot.position;
    return slot.position + static_cast<double>(DxNow() - slot.positionAt) / 1000.0;
}

int SetSoundNumber(lua_State* L, float SoundSlot::*field, float minValue, float maxValue) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float value = static_cast<float>(lua_tonumber(L, 2));
    if (!std::isfinite(value) || value < minValue || value > maxValue) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->*field = value;
    if (!g_diagSoundSpace) {
        g_diagSoundSpace = true;
        LUA_LOGE("DIAG sound distance/speed/pan: the MediaPlayer has no 3D mixer; the value is stored");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int GetSoundNumber(lua_State* L, float SoundSlot::*field) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, slot->*field);
    return 1;
}

// --- Basic Sound Handlers ---

int l_playSound(lua_State* L) {
    const char* path = lua_tostring(L, 1);
    if (!path || path[0] == '\0') {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool loop = lua_toboolean(L, 2) != 0;
    uint32_t id = AllocSound(path, loop, false);
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

int l_stopSound(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!AudioSoundAlive(id)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        for (auto& slot : g_sounds) {
            if (slot.id == id) slot.alive = false;
        }
    }
    char line[32];
    std::snprintf(line, sizeof(line), "%u", id);
    ::WorldRPG::JniBridge::PostGameLine(12, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_setSoundVolume(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!AudioSoundAlive(id) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    float volume = static_cast<float>(lua_tonumber(L, 2));
    if (!std::isfinite(volume)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        for (auto& slot : g_sounds) {
            if (slot.id == id) slot.volume = volume;
        }
    }
    char line[64];
    std::snprintf(line, sizeof(line), "%u|%.3f", id, volume);
    ::WorldRPG::JniBridge::PostGameLine(13, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundVolume(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    for (const auto& slot : g_sounds) {
        if (slot.alive && slot.id == id) {
            lua_pushnumber(L, slot.volume);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_setSoundLooped(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->loop = lua_toboolean(L, 2) != 0;
    char line[32];
    std::snprintf(line, sizeof(line), "%u|%d", id, slot->loop ? 1 : 0);
    ::WorldRPG::JniBridge::PostGameLine(14, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_isSoundLooped(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, slot->loop ? 1 : 0);
    return 1;
}

int l_setSoundPaused(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool pause = lua_toboolean(L, 2) != 0;
    const auto now = DxNow();
    if (pause && !slot->paused)
        slot->position += static_cast<double>(now - slot->positionAt) / 1000.0;
    else if (!pause && slot->paused)
        slot->positionAt = now;
    slot->paused = pause;
    char line[32];
    std::snprintf(line, sizeof(line), "%u|%d", id, slot->paused ? 1 : 0);
    ::WorldRPG::JniBridge::PostGameLine(15, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_isSoundPaused(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, slot->paused ? 1 : 0);
    return 1;
}

int l_getSoundPosition(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, SoundPosition(*slot));
    return 1;
}

int l_setSoundPosition(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    double seconds = lua_tonumber(L, 2);
    if (!std::isfinite(seconds) || seconds < 0) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->position = seconds;
    slot->positionAt = DxNow();
    char line[64];
    std::snprintf(line, sizeof(line), "%u|%.3f", id, seconds);
    ::WorldRPG::JniBridge::PostGameLine(16, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundLength(lua_State* L) {
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(ElementIdOf(L, 1));
    if (!slot) return 0;
    lua_pushnumber(L, slot->length);
    return 1;
}

int l_setSoundSpeed(lua_State* L) { return SetSoundNumber(L, &SoundSlot::speed, 0.0001f, 100.0f); }
int l_getSoundSpeed(lua_State* L) { return GetSoundNumber(L, &SoundSlot::speed); }

int l_setSoundPan(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) { lua_pushboolean(L, 0); return 1; }
    const float pan = static_cast<float>(lua_tonumber(L, 2));
    if (!std::isfinite(pan) || pan < -1.0f || pan > 1.0f) { lua_pushboolean(L, 0); return 1; }
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        SoundSlot* slot = FindSound(id);
        if (!slot) { lua_pushboolean(L, 0); return 1; }
        slot->pan = pan;
    }
    char line[64];
    std::snprintf(line, sizeof(line), "%u|%.6f", id, pan);
    ::WorldRPG::JniBridge::PostGameLine(18, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundPan(lua_State* L) { return GetSoundNumber(L, &SoundSlot::pan); }

int l_setSoundMaxDistance(lua_State* L) { return SetSoundNumber(L, &SoundSlot::maxDistance, 0.0f, 1.0e6f); }
int l_getSoundMaxDistance(lua_State* L) { return GetSoundNumber(L, &SoundSlot::maxDistance); }
int l_setSoundMinDistance(lua_State* L) { return SetSoundNumber(L, &SoundSlot::minDistance, 0.0f, 1.0e6f); }
int l_getSoundMinDistance(lua_State* L) { return GetSoundNumber(L, &SoundSlot::minDistance); }

int l_playSoundFrontEnd(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int id = static_cast<int>(lua_tointeger(L, 1));
    if (id < 0 || id > 101) {
        LUA_LOGE("playSoundFrontEnd: invalid sound ID. Valid sound IDs are 0 - 101.");
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!g_diagFrontEnd) {
        g_diagFrontEnd = true;
        LUA_LOGE("DIAG playSoundFrontEnd: CAudioEngine::PlayFrontEndSound is not hooked");
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_getRadioChannelName(lua_State* L) {
    static const char* stations[] = {
        "Radio off", "Playback FM", "K-Rose", "K-DST", "Bounce FM", "SF-UR",
        "Radio Los Santos", "Radio X", "CSR 103.9", "K-Jah West", "Master Sounds 98.3", "WCTR", "User Track Player"
    };
    if (lua_isnumber(L, 1)) {
        const double value = lua_tonumber(L, 1);
        if (std::isfinite(value) && value > -1.0 && value < 13.0) {
            lua_pushstring(L, stations[static_cast<int>(value)]);
            return 1;
        }
    }
    lua_pushboolean(L, 0);
    return 1;
}

// --- Lot 1: 3D Spatial Audio & Core Streams ---

// CLuaAudioDefs::PlaySound3D: path, x, y, z, [loop = false], [throttled = true]
int l_playSound3D(lua_State* L) {
    const char* path = lua_tostring(L, 1);
    if (!path || path[0] == '\0' || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float x = static_cast<float>(lua_tonumber(L, 2));
    const float y = static_cast<float>(lua_tonumber(L, 3));
    const float z = static_cast<float>(lua_tonumber(L, 4));
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool loop = lua_isboolean(L, 5) ? (lua_toboolean(L, 5) != 0) : false;
    const bool throttled = lua_isboolean(L, 6) ? (lua_toboolean(L, 6) != 0) : true;

    uint32_t id = AllocSound(path, loop, true, x, y, z, throttled);
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

// CLuaAudioDefs::SetSoundPanEnabled / IsSoundPanEnabled
int l_setSoundPanningEnabled(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (lua_type(L, 2) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->panEnabled = (lua_toboolean(L, 2) != 0);
    lua_pushboolean(L, 1);
    return 1;
}

int l_isSoundPanningEnabled(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, slot->panEnabled ? 1 : 0);
    return 1;
}

// CLuaAudioDefs::GetSoundProperties / SetSoundProperties
int l_getSoundProperties(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, slot->sampleRate);
    lua_pushnumber(L, slot->tempo);
    lua_pushnumber(L, slot->pitch);
    lua_pushboolean(L, slot->reversed ? 1 : 0);
    return 4;
}

int l_setSoundProperties(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2) || !lua_isnumber(L, 3) || !lua_isnumber(L, 4)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float sampleRate = static_cast<float>(lua_tonumber(L, 2));
    const float tempo = static_cast<float>(lua_tonumber(L, 3));
    const float pitch = static_cast<float>(lua_tonumber(L, 4));
    const bool reversed = lua_isboolean(L, 5) ? (lua_toboolean(L, 5) != 0) : false;
    if (!std::isfinite(sampleRate) || !std::isfinite(tempo) || !std::isfinite(pitch)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->sampleRate = sampleRate;
    slot->tempo = tempo;
    slot->pitch = pitch;
    slot->reversed = reversed;
    lua_pushboolean(L, 1);
    return 1;
}

// CLuaAudioDefs::GetSoundBufferLength
int l_getSoundBufferLength(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushnumber(L, slot->bufferLength);
    return 1;
}

// CLuaAudioDefs::GetSoundBPM
int l_getSoundBPM(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (slot->bpm > 0.0f) {
        lua_pushnumber(L, slot->bpm);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

// CLuaAudioDefs::GetSoundLevelData (dwLeft, dwRight in range 0 - 32768)
int l_getSoundLevelData(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot || slot->paused) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const float vol = std::clamp(slot->volume, 0.0f, 1.0f);
    const int left = static_cast<int>(std::clamp(vol * (1.0f - std::max(0.0f, slot->pan)), 0.0f, 1.0f) * 32768.0f);
    const int right = static_cast<int>(std::clamp(vol * (1.0f + std::min(0.0f, slot->pan)), 0.0f, 1.0f) * 32768.0f);
    lua_pushinteger(L, left);
    lua_pushinteger(L, right);
    return 2;
}

// --- Lot 2: Sound Analysis, DSP Effects & Debug Visualizer ---

static const char* kKnownEffects[] = {
    "chorus", "compressor", "distortion", "echo", "flanger", "gargle", "i3dl2reverb", "parameq", "reverb"
};

static std::string ToLower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

static bool IsKnownEffect(const std::string& name) {
    for (const char* fx : kKnownEffects) {
        if (name == fx) return true;
    }
    return false;
}

int l_getSoundFFTData(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int length = static_cast<int>(lua_tointeger(L, 2));
    const int bands = lua_isnumber(L, 3) ? static_cast<int>(lua_tointeger(L, 3)) : 0;
    if (length < 2 || length > 16384) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot || slot->paused) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int count = (bands > 0) ? bands : (length / 2 + 1);
    lua_createtable(L, count, 0);
    for (int i = 0; i < count; ++i) {
        lua_pushinteger(L, i);
        lua_pushnumber(L, 0.0);
        lua_settable(L, -3);
    }
    return 1;
}

int l_getSoundWaveData(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int length = static_cast<int>(lua_tointeger(L, 2));
    if (length <= 0 || length > 16384) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot || slot->paused) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_createtable(L, length, 0);
    for (int i = 0; i < length; ++i) {
        lua_pushinteger(L, i);
        lua_pushnumber(L, 0.0);
        lua_settable(L, -3);
    }
    return 1;
}

int l_getSoundMetaTags(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    const char* format = lua_isstring(L, 2) ? lua_tostring(L, 2) : "";
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (format && format[0] != '\0') {
        const std::string key = ToLower(format);
        auto it = slot->metaTags.find(key);
        if (it != slot->metaTags.end()) {
            lua_pushstring(L, it->second.c_str());
            return 1;
        }
        lua_pushstring(L, "");
        return 1;
    }
    static const char* kStandardFields[] = {
        "title", "artist", "album", "genre", "year", "comment",
        "track", "composer", "copyright", "subtitle", "album_artist",
        "stream_name", "stream_title"
    };
    lua_newtable(L);
    for (const char* field : kStandardFields) {
        auto it = slot->metaTags.find(field);
        if (it != slot->metaTags.end() && !it->second.empty()) {
            lua_pushstring(L, it->second.c_str());
            lua_setfield(L, -2, field);
        }
    }
    return 1;
}

int l_setSoundEffectEnabled(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isstring(L, 2) || lua_type(L, 3) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string effect = ToLower(lua_tostring(L, 2));
    if (!IsKnownEffect(effect)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool enable = lua_toboolean(L, 3) != 0;
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->fxEffects[effect] = enable;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundEffects(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    for (const char* fx : kKnownEffects) {
        auto it = slot->fxEffects.find(fx);
        const bool enabled = (it != slot->fxEffects.end()) ? it->second : false;
        lua_pushboolean(L, enabled ? 1 : 0);
        lua_setfield(L, -2, fx);
    }
    return 1;
}

int l_setSoundEffectParameter(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isstring(L, 2) || !lua_isstring(L, 3) || !lua_isnumber(L, 4)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string effect = ToLower(lua_tostring(L, 2));
    const std::string param = lua_tostring(L, 3);
    const double val = lua_tonumber(L, 4);
    if (!std::isfinite(val) || !IsKnownEffect(effect)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    auto fxIt = slot->fxEffects.find(effect);
    if (fxIt == slot->fxEffects.end() || !fxIt->second) {
        lua_pushboolean(L, 0);
        return 1;
    }
    slot->fxParams[effect][param] = val;
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSoundEffectParameters(lua_State* L) {
    const uint32_t id = ElementIdOf(L, 1);
    if (!lua_isstring(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string effect = ToLower(lua_tostring(L, 2));
    if (!IsKnownEffect(effect)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    const SoundSlot* slot = FindSound(id);
    if (!slot) {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_newtable(L);
    auto it = slot->fxParams.find(effect);
    if (it != slot->fxParams.end()) {
        for (const auto& [k, v] : it->second) {
            lua_pushnumber(L, v);
            lua_setfield(L, -2, k.c_str());
        }
    }
    return 1;
}

int l_showSound(lua_State* L) {
    if (lua_type(L, 1) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_showSound = lua_toboolean(L, 1) != 0;
    lua_pushboolean(L, 1);
    return 1;
}

int l_isShowSoundEnabled(lua_State* L) {
    lua_pushboolean(L, g_showSound ? 1 : 0);
    return 1;
}

// --- Lot 3: World & Ambient Audio, SFX & Radio Channels ---

static bool IsValidAudioContainer(const std::string& name) {
    static const char* kContainers[] = {
        "feet", "genrl", "pain_a", "script", "spc_ea", "spc_fa", "spc_ga", "spc_na", "spc_pa", "radio"
    };
    for (const char* c : kContainers) {
        if (name == c) return true;
    }
    return false;
}

int l_setRadioChannel(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int channel = static_cast<int>(lua_tointeger(L, 1));
    if (channel < 0 || channel > 12) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_radioChannel = static_cast<uint8_t>(channel);
    lua_pushboolean(L, 1);
    return 1;
}

int l_getRadioChannel(lua_State* L) {
    lua_pushinteger(L, g_radioChannel);
    return 1;
}

int l_setAmbientSoundEnabled(lua_State* L) {
    const char* typeStr = lua_isstring(L, 1) ? lua_tostring(L, 1) : "general";
    const int boolIdx = lua_isstring(L, 1) ? 2 : 1;
    if (lua_type(L, boolIdx) != LUA_TBOOLEAN) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool enabled = lua_toboolean(L, boolIdx) != 0;
    const std::string type = ToLower(typeStr);
    if (type == "general") {
        g_ambientGeneral = enabled;
    } else if (type == "gunfire") {
        g_ambientGunfire = enabled;
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_isAmbientSoundEnabled(lua_State* L) {
    const char* typeStr = lua_isstring(L, 1) ? lua_tostring(L, 1) : "general";
    const std::string type = ToLower(typeStr);
    if (type == "general") {
        lua_pushboolean(L, g_ambientGeneral ? 1 : 0);
    } else if (type == "gunfire") {
        lua_pushboolean(L, g_ambientGunfire ? 1 : 0);
    } else {
        lua_pushboolean(L, 0);
    }
    return 1;
}

int l_resetAmbientSounds(lua_State* L) {
    g_ambientGeneral = true;
    g_ambientGunfire = true;
    lua_pushboolean(L, 1);
    return 1;
}

int l_setWorldSoundEnabled(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int group = static_cast<int>(lua_tointeger(L, 1));
    int index = -1;
    bool enabled = false;
    if (lua_type(L, 2) == LUA_TBOOLEAN) {
        enabled = lua_toboolean(L, 2) != 0;
    } else if (lua_isnumber(L, 2) && lua_type(L, 3) == LUA_TBOOLEAN) {
        index = static_cast<int>(lua_tointeger(L, 2));
        enabled = lua_toboolean(L, 3) != 0;
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (group < 0 || group > 49) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (enabled) {
        g_disabledWorldSounds.erase({group, index});
        if (index == -1) {
            for (auto it = g_disabledWorldSounds.begin(); it != g_disabledWorldSounds.end(); ) {
                if (it->first == group) it = g_disabledWorldSounds.erase(it);
                else ++it;
            }
        }
    } else {
        g_disabledWorldSounds.insert({group, index});
    }
    lua_pushboolean(L, 1);
    return 1;
}

int l_isWorldSoundEnabled(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int group = static_cast<int>(lua_tointeger(L, 1));
    const int index = lua_isnumber(L, 2) ? static_cast<int>(lua_tointeger(L, 2)) : -1;
    if (group < 0 || group > 49) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (g_disabledWorldSounds.count({group, -1}) > 0 || g_disabledWorldSounds.count({group, index}) > 0) {
        lua_pushboolean(L, 0);
    } else {
        lua_pushboolean(L, 1);
    }
    return 1;
}

int l_resetWorldSounds(lua_State* L) {
    g_disabledWorldSounds.clear();
    lua_pushboolean(L, 1);
    return 1;
}

int l_getSFXStatus(lua_State* L) {
    if (!lua_isstring(L, 1)) {
        lua_pushnil(L);
        return 1;
    }
    const std::string container = ToLower(lua_tostring(L, 1));
    if (IsValidAudioContainer(container)) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushnil(L);
    return 1;
}

int l_playSFX(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string container = ToLower(lua_tostring(L, 1));
    if (!IsValidAudioContainer(container)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int bank = static_cast<int>(lua_tointeger(L, 2));
    const int audio = static_cast<int>(lua_tointeger(L, 3));
    const bool loop = lua_isboolean(L, 4) ? (lua_toboolean(L, 4) != 0) : false;
    char path[128];
    std::snprintf(path, sizeof(path), "sfx://%s/%d/%d", container.c_str(), bank, audio);
    uint32_t id = AllocSound(path, loop, false);
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

int l_playSFX3D(lua_State* L) {
    if (!lua_isstring(L, 1) || !lua_isnumber(L, 2) || !lua_isnumber(L, 3) ||
        !lua_isnumber(L, 4) || !lua_isnumber(L, 5) || !lua_isnumber(L, 6)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const std::string container = ToLower(lua_tostring(L, 1));
    if (!IsValidAudioContainer(container)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int bank = static_cast<int>(lua_tointeger(L, 2));
    const int audio = static_cast<int>(lua_tointeger(L, 3));
    const float x = static_cast<float>(lua_tonumber(L, 4));
    const float y = static_cast<float>(lua_tonumber(L, 5));
    const float z = static_cast<float>(lua_tonumber(L, 6));
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const bool loop = lua_isboolean(L, 7) ? (lua_toboolean(L, 7) != 0) : false;
    char path[128];
    std::snprintf(path, sizeof(path), "sfx://%s/%d/%d", container.c_str(), bank, audio);
    uint32_t id = AllocSound(path, loop, true, x, y, z, true);
    if (!id) {
        lua_pushboolean(L, 0);
        return 1;
    }
    PushElement(L, id);
    return 1;
}

// --- Lot 4: Mission Audio Engine & Audio Events ---

int l_playMissionPassedTune(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const int tune = static_cast<int>(lua_tointeger(L, 1));
    if (tune < 1 || tune > 2) {
        lua_pushboolean(L, 0);
        return 1;
    }
    char line[32];
    std::snprintf(line, sizeof(line), "%d", tune);
    ::WorldRPG::JniBridge::PostGameLine(14, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_requestMissionAudio(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t eventId = static_cast<uint32_t>(lua_tointeger(L, 1));
    if (!((eventId >= 1800 && eventId <= 1829) || (eventId >= 2000 && eventId <= 45400))) {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (lua_isnumber(L, 2)) {
        const int preferredSlot = static_cast<int>(lua_tointeger(L, 2));
        if (preferredSlot < 0 || preferredSlot >= 4) {
            lua_pushboolean(L, 0);
            return 1;
        }
    }
    std::lock_guard<std::mutex> lock(g_soundMu);
    if (g_missionSlots.size() >= 4) {
        lua_pushboolean(L, 0);
        return 1;
    }
    uint32_t handle = g_nextMissionHandle++;
    if (handle == 0) handle = g_nextMissionHandle++;
    MissionAudioSlot slot;
    slot.handle = handle;
    slot.eventId = eventId;
    slot.loaded = true;
    slot.played = false;
    slot.owner = ActiveVmOwner();
    g_missionSlots[handle] = slot;
    lua_pushinteger(L, handle);
    return 1;
}

int l_isMissionAudioLoaded(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    std::lock_guard<std::mutex> lock(g_soundMu);
    auto it = g_missionSlots.find(handle);
    if (it != g_missionSlots.end()) {
        lua_pushboolean(L, it->second.loaded ? 1 : 0);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_playMissionAudio(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    std::lock_guard<std::mutex> lock(g_soundMu);
    auto it = g_missionSlots.find(handle);
    if (it == g_missionSlots.end() || it->second.played || !it->second.loaded) {
        lua_pushboolean(L, 0);
        return 1;
    }
    it->second.played = true;
    char line[32];
    std::snprintf(line, sizeof(line), "%u", handle);
    ::WorldRPG::JniBridge::PostGameLine(15, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_isMissionAudioFinished(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    std::lock_guard<std::mutex> lock(g_soundMu);
    auto it = g_missionSlots.find(handle);
    if (it != g_missionSlots.end() && it->second.played) {
        lua_pushboolean(L, 1);
        return 1;
    }
    lua_pushboolean(L, 0);
    return 1;
}

int l_releaseMissionAudio(lua_State* L) {
    if (!lua_isnumber(L, 1)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t handle = static_cast<uint32_t>(lua_tointeger(L, 1));
    std::lock_guard<std::mutex> lock(g_soundMu);
    auto it = g_missionSlots.find(handle);
    if (it == g_missionSlots.end()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    g_missionSlots.erase(it);
    lua_pushboolean(L, 1);
    return 1;
}

int l_reportPositionMissionAudioEvent(lua_State* L) {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    uint32_t eventId = 0;
    if (lua_isnumber(L, 1) && lua_isnumber(L, 2) && lua_isnumber(L, 3) && lua_isnumber(L, 4)) {
        x = static_cast<float>(lua_tonumber(L, 1));
        y = static_cast<float>(lua_tonumber(L, 2));
        z = static_cast<float>(lua_tonumber(L, 3));
        eventId = static_cast<uint32_t>(lua_tointeger(L, 4));
    } else if (lua_istable(L, 1) && lua_isnumber(L, 2)) {
        lua_rawgeti(L, 1, 1);
        if (lua_isnumber(L, -1)) x = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        lua_rawgeti(L, 1, 2);
        if (lua_isnumber(L, -1)) y = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        lua_rawgeti(L, 1, 3);
        if (lua_isnumber(L, -1)) z = static_cast<float>(lua_tonumber(L, -1));
        lua_pop(L, 1);
        eventId = static_cast<uint32_t>(lua_tointeger(L, 2));
    } else {
        lua_pushboolean(L, 0);
        return 1;
    }
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || eventId < 1000 || eventId > 1190) {
        lua_pushboolean(L, 0);
        return 1;
    }
    char line[128];
    std::snprintf(line, sizeof(line), "%.3f|%.3f|%.3f|%u", x, y, z, eventId);
    ::WorldRPG::JniBridge::PostGameLine(16, line);
    lua_pushboolean(L, 1);
    return 1;
}

int l_reportVehicleMissionAudioEvent(lua_State* L) {
    if ((ElementIdOf(L, 1) == 0 && !lua_isuserdata(L, 1)) || !lua_isnumber(L, 2)) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t eventId = static_cast<uint32_t>(lua_tointeger(L, 2));
    if (eventId < 1000 || eventId > 1190) {
        lua_pushboolean(L, 0);
        return 1;
    }
    const uint32_t vehId = ElementIdOf(L, 1);
    char line[64];
    std::snprintf(line, sizeof(line), "%u|%u", vehId, eventId);
    ::WorldRPG::JniBridge::PostGameLine(17, line);
    lua_pushboolean(L, 1);
    return 1;
}

}  // namespace

bool AudioSoundAlive(uint32_t id) {
    if (!id) return false;
    std::lock_guard<std::mutex> lock(g_soundMu);
    for (const auto& slot : g_sounds) {
        if (slot.alive && slot.id == id) return true;
    }
    return false;
}

void AudioNoteSoundEnded(uint32_t id) {
    std::lock_guard<std::mutex> lock(g_soundEndMu);
    g_soundEnded.push_back(id);
}

void AudioCollectEndedSounds(std::vector<uint32_t>& out) {
    std::lock_guard<std::mutex> lock(g_soundEndMu);
    out.swap(g_soundEnded);
}

void AudioNoteSoundLength(uint32_t id, double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) return;
    std::lock_guard<std::mutex> lock(g_soundMu);
    if (SoundSlot* slot = FindSound(id)) {
        slot->length = seconds;
    }
}

void AudioResetSession() {
    std::lock_guard<std::mutex> lock(g_soundMu);
    for (auto& slot : g_sounds) slot = SoundSlot{};
    g_nextSound = kFirstSoundId;
    g_showSound = false;
    g_radioChannel = 0;
    g_ambientGeneral = true;
    g_ambientGunfire = true;
    g_disabledWorldSounds.clear();
    g_missionSlots.clear();
    g_nextMissionHandle = 1;
}

void AudioStopOwnedSounds(const std::string* owner, uint16_t netId, uint32_t generation) {
    std::vector<uint32_t> stopped;
    {
        std::lock_guard<std::mutex> lock(g_soundMu);
        for (auto& slot : g_sounds) {
            if (!slot.alive) continue;
            if (owner && (slot.owner != *owner || (netId && slot.ownerNetId != netId) ||
                          (generation && slot.ownerGeneration != generation))) continue;
            slot.alive = false;
            stopped.push_back(slot.id);
        }
        if (owner) {
            for (auto it = g_missionSlots.begin(); it != g_missionSlots.end(); ) {
                if (it->second.owner == *owner) it = g_missionSlots.erase(it);
                else ++it;
            }
        }
    }
    for (uint32_t id : stopped) {
        char line[32];
        std::snprintf(line, sizeof(line), "%u", id);
        ::WorldRPG::JniBridge::PostGameLine(12, line);
    }
}

void InstallAudioLua(lua_State* L) {
    const struct { const char* name; lua_CFunction fn; } binds[] = {
        {"playSound", l_playSound},
        {"playSound3D", l_playSound3D},
        {"stopSound", l_stopSound},
        {"setSoundVolume", l_setSoundVolume},
        {"getSoundVolume", l_getSoundVolume},
        {"setSoundLooped", l_setSoundLooped},
        {"isSoundLooped", l_isSoundLooped},
        {"setSoundPaused", l_setSoundPaused},
        {"isSoundPaused", l_isSoundPaused},
        {"getSoundPosition", l_getSoundPosition},
        {"setSoundPosition", l_setSoundPosition},
        {"getSoundLength", l_getSoundLength},
        {"setSoundSpeed", l_setSoundSpeed},
        {"getSoundSpeed", l_getSoundSpeed},
        {"setSoundPan", l_setSoundPan},
        {"getSoundPan", l_getSoundPan},
        {"setSoundMinDistance", l_setSoundMinDistance},
        {"getSoundMinDistance", l_getSoundMinDistance},
        {"setSoundMaxDistance", l_setSoundMaxDistance},
        {"getSoundMaxDistance", l_getSoundMaxDistance},
        {"playSoundFrontEnd", l_playSoundFrontEnd},
        {"getRadioChannelName", l_getRadioChannelName},

        // Lot 1 additions
        {"setSoundPanningEnabled", l_setSoundPanningEnabled},
        {"isSoundPanningEnabled", l_isSoundPanningEnabled},
        {"getSoundProperties", l_getSoundProperties},
        {"setSoundProperties", l_setSoundProperties},
        {"getSoundBufferLength", l_getSoundBufferLength},
        {"getSoundBPM", l_getSoundBPM},
        {"getSoundLevelData", l_getSoundLevelData},

        // Lot 2 additions
        {"getSoundFFTData", l_getSoundFFTData},
        {"getSoundWaveData", l_getSoundWaveData},
        {"getSoundMetaTags", l_getSoundMetaTags},
        {"setSoundEffectEnabled", l_setSoundEffectEnabled},
        {"getSoundEffects", l_getSoundEffects},
        {"setSoundEffectParameter", l_setSoundEffectParameter},
        {"getSoundEffectParameters", l_getSoundEffectParameters},
        {"showSound", l_showSound},
        {"isShowSoundEnabled", l_isShowSoundEnabled},

        // Lot 3 additions
        {"setRadioChannel", l_setRadioChannel},
        {"getRadioChannel", l_getRadioChannel},
        {"setAmbientSoundEnabled", l_setAmbientSoundEnabled},
        {"isAmbientSoundEnabled", l_isAmbientSoundEnabled},
        {"resetAmbientSounds", l_resetAmbientSounds},
        {"setWorldSoundEnabled", l_setWorldSoundEnabled},
        {"isWorldSoundEnabled", l_isWorldSoundEnabled},
        {"resetWorldSounds", l_resetWorldSounds},
        {"playSFX", l_playSFX},
        {"playSFX3D", l_playSFX3D},
        {"getSFXStatus", l_getSFXStatus},

        // Lot 4 additions
        {"playMissionPassedTune", l_playMissionPassedTune},
        {"requestMissionAudio", l_requestMissionAudio},
        {"isMissionAudioLoaded", l_isMissionAudioLoaded},
        {"playMissionAudio", l_playMissionAudio},
        {"isMissionAudioFinished", l_isMissionAudioFinished},
        {"releaseMissionAudio", l_releaseMissionAudio},
        {"reportPositionMissionAudioEvent", l_reportPositionMissionAudioEvent},
        {"reportVehicleMissionAudioEvent", l_reportVehicleMissionAudioEvent},
    };
    for (const auto& bind : binds) {
        lua_pushcfunction(L, bind.fn);
        lua_setglobal(L, bind.name);
    }
}

}  // namespace WorldRPG::Script
