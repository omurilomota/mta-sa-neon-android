#include "EngineModelLoader.h"

#if defined(__ANDROID__)
#include <cstdint>
#include <dlfcn.h>

namespace WorldRPG::Script {
namespace {

struct RwStream;
struct RpClump;
struct RwTexDictionary;

constexpr int kStreamFilename = 2;
constexpr int kStreamRead = 1;
constexpr std::uint32_t kChunkClump = 0x10;
constexpr std::uint32_t kChunkTextureDictionary = 0x16;

struct RenderWare {
    RwStream* (*open)(int, int, const void*);
    int (*close)(RwStream*, void*);
    int (*findChunk)(RwStream*, std::uint32_t, std::uint32_t*, std::uint32_t*);
    RpClump* (*readClump)(RwStream*);
    int (*destroyClump)(RpClump*);
    RwTexDictionary* (*readDictionary)(RwStream*);
    int (*destroyDictionary)(RwTexDictionary*);
};

const RenderWare& Symbols() {
    // Lua resources run after the game has loaded its RenderWare library.
    static const RenderWare symbols = [] {
        void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD);
        if (!lib) return RenderWare{};
        RenderWare rw{
            reinterpret_cast<decltype(RenderWare::open)>(
                dlsym(lib, "_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv")),
            reinterpret_cast<decltype(RenderWare::close)>(dlsym(lib, "_Z13RwStreamCloseP8RwStreamPv")),
            reinterpret_cast<decltype(RenderWare::findChunk)>(dlsym(lib, "_Z17RwStreamFindChunkP8RwStreamjPjS1_")),
            reinterpret_cast<decltype(RenderWare::readClump)>(dlsym(lib, "_Z17RpClumpStreamReadP8RwStream")),
            reinterpret_cast<decltype(RenderWare::destroyClump)>(dlsym(lib, "_Z14RpClumpDestroyP7RpClump")),
            reinterpret_cast<decltype(RenderWare::readDictionary)>(
                dlsym(lib, "_Z28RwTexDictionaryGtaStreamReadP8RwStream")),
            reinterpret_cast<decltype(RenderWare::destroyDictionary)>(
                dlsym(lib, "_Z22RwTexDictionaryDestroyP15RwTexDictionary")),
        };
        dlclose(lib);
        return rw;
    }();
    return symbols;
}

template <typename Asset>
Asset* ReadAsset(const RenderWare& rw, const char* path, std::uint32_t chunk, Asset* (*read)(RwStream*)) {
    if (!rw.open || !rw.close || !rw.findChunk || !read) return nullptr;
    RwStream* stream = rw.open(kStreamFilename, kStreamRead, path);
    if (!stream) return nullptr;
    Asset* asset = rw.findChunk(stream, chunk, nullptr, nullptr) ? read(stream) : nullptr;
    rw.close(stream, nullptr);
    return asset;
}

}  // namespace

void* NativeLoadDFF(const char* path) {
    const RenderWare& rw = Symbols();
    if (!rw.destroyClump) return nullptr;
    return ReadAsset(rw, path, kChunkClump, rw.readClump);
}

void* NativeLoadTXD(const char* path) {
    const RenderWare& rw = Symbols();
    if (!rw.destroyDictionary) return nullptr;
    // CFileLoader::LoadTexDictionary creates an empty dictionary on error.
    return ReadAsset(rw, path, kChunkTextureDictionary, rw.readDictionary);
}

void NativeDestroyClump(void* clump) {
    if (clump) Symbols().destroyClump(static_cast<RpClump*>(clump));
}

void NativeDestroyTXD(void* txd) {
    if (txd) Symbols().destroyDictionary(static_cast<RwTexDictionary*>(txd));
}

}  // namespace WorldRPG::Script

#else

namespace WorldRPG::Script {

void* NativeLoadDFF(const char*) { return nullptr; }
void* NativeLoadTXD(const char*) { return nullptr; }
void NativeDestroyClump(void*) {}
void NativeDestroyTXD(void*) {}

}  // namespace WorldRPG::Script

#endif
