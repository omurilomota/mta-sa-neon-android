#include "EngineModelLoader.h"

#include <cstdio>
#include <cstring>

#if defined(__ANDROID__)
#include <dlfcn.h>
#include <android/log.h>

#define RW_CHUNK_CLUMP 0x10
#define RW_CHUNK_TEXDICTIONARY 0x16

namespace WorldRPG::Script {
namespace {

typedef void* (*RwStreamOpenFn)(int type, int accessType, const void* pData);
typedef int (*RwStreamCloseFn)(void* stream, void* pData);
typedef void* (*RwStreamFindChunkFn)(void* stream, uint32_t type, uint32_t* length, uint32_t* version);
typedef void* (*RpClumpStreamReadFn)(void* stream);
typedef void (*RpClumpDestroyFn)(void* clump);
typedef void* (*LoadTexDictionaryFn)(const char* filename);
typedef void* (*RwTexDictionaryStreamReadFn)(void* stream);
typedef void* (*RwTexDictionaryGtaStreamReadFn)(void* stream);
typedef void (*RwTexDictionaryDestroyFn)(void* txd);
typedef void (*SetClumpFn)(void* modelInfo, void* clump);
typedef void (*AddTexDictionariesFn)(void* dst, void* src);
typedef void (*SetTxdParentFn)(void* txd, void* parent);

struct GtaRwSymbols {
    RwStreamOpenFn rwStreamOpen = nullptr;
    RwStreamCloseFn rwStreamClose = nullptr;
    RwStreamFindChunkFn rwStreamFindChunk = nullptr;
    RpClumpStreamReadFn rpClumpStreamRead = nullptr;
    RpClumpDestroyFn rpClumpDestroy = nullptr;
    LoadTexDictionaryFn loadTexDictionary = nullptr;
    RwTexDictionaryStreamReadFn rwTexDictStreamRead = nullptr;
    RwTexDictionaryGtaStreamReadFn rwTexDictGtaStreamRead = nullptr;
    RwTexDictionaryDestroyFn rwTexDictDestroy = nullptr;
    SetClumpFn vehicleSetClump = nullptr;
    SetClumpFn clumpSetClump = nullptr;
    AddTexDictionariesFn addTexDictionaries = nullptr;
    SetTxdParentFn setTxdParent = nullptr;
    void*** modelInfoPtrs = nullptr;
    void** txdPoolPtr = nullptr;
    bool initialized = false;

    void EnsureInit() {
        if (initialized) return;
        void* lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_NOLOAD | RTLD_GLOBAL);
        if (!lib) {
            lib = dlopen("libGTASA.so", RTLD_NOW | RTLD_GLOBAL);
        }
        if (!lib) {
            __android_log_print(ANDROID_LOG_ERROR, "WorldRPGNeon", "EngineModelLoader: cannot dlopen libGTASA.so");
            return;
        }

        rwStreamOpen = reinterpret_cast<RwStreamOpenFn>(dlsym(lib, "_Z12RwStreamOpen12RwStreamType18RwStreamAccessTypePKv"));
        rwStreamClose = reinterpret_cast<RwStreamCloseFn>(dlsym(lib, "_Z13RwStreamCloseP8RwStreamPv"));
        rwStreamFindChunk = reinterpret_cast<RwStreamFindChunkFn>(dlsym(lib, "_Z17RwStreamFindChunkP8RwStreamjPjS1_"));
        rpClumpStreamRead = reinterpret_cast<RpClumpStreamReadFn>(dlsym(lib, "_Z17RpClumpStreamReadP8RwStream"));
        rpClumpDestroy = reinterpret_cast<RpClumpDestroyFn>(dlsym(lib, "_Z14RpClumpDestroyP7RpClump"));
        loadTexDictionary = reinterpret_cast<LoadTexDictionaryFn>(dlsym(lib, "_ZN11CFileLoader17LoadTexDictionaryEPKc"));
        rwTexDictStreamRead = reinterpret_cast<RwTexDictionaryStreamReadFn>(dlsym(lib, "_Z25RwTexDictionaryStreamReadP8RwStream"));
        rwTexDictGtaStreamRead = reinterpret_cast<RwTexDictionaryGtaStreamReadFn>(dlsym(lib, "_Z28RwTexDictionaryGtaStreamReadP8RwStream"));
        rwTexDictDestroy = reinterpret_cast<RwTexDictionaryDestroyFn>(dlsym(lib, "_Z22RwTexDictionaryDestroyP15RwTexDictionary"));
        vehicleSetClump = reinterpret_cast<SetClumpFn>(dlsym(lib, "_ZN17CVehicleModelInfo8SetClumpEP7RpClump"));
        clumpSetClump = reinterpret_cast<SetClumpFn>(dlsym(lib, "_ZN15CClumpModelInfo8SetClumpEP7RpClump"));
        addTexDictionaries = reinterpret_cast<AddTexDictionariesFn>(dlsym(lib, "_ZN11CFileLoader18AddTexDictionariesEP15RwTexDictionaryS1_"));
        setTxdParent = reinterpret_cast<SetTxdParentFn>(dlsym(lib, "_ZN9CTxdStore12SetTxdParentEP15RwTexDictionaryS1_"));
        modelInfoPtrs = reinterpret_cast<void***>(dlsym(lib, "_ZN10CModelInfo16ms_modelInfoPtrsE"));
        txdPoolPtr = reinterpret_cast<void**>(dlsym(lib, "_ZN9CTxdStore11ms_pTxdPoolE"));

        initialized = true;
        __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon",
            "EngineModelLoader: init complete. rwStreamOpen=%p, clumpRead=%p, vehClump=%p, models=%p, txdPool=%p",
            rwStreamOpen, rpClumpStreamRead, vehicleSetClump, modelInfoPtrs, txdPoolPtr);
    }
};

GtaRwSymbols g_symbols;

} // namespace

void* NativeLoadDFF(const char* filepath) {
    if (!filepath || filepath[0] == '\0') return nullptr;
    g_symbols.EnsureInit();
    if (!g_symbols.rwStreamOpen || !g_symbols.rwStreamClose || !g_symbols.rpClumpStreamRead) return nullptr;

    void* stream = g_symbols.rwStreamOpen(1 /* rwSTREAMFILENAME */, 1 /* rwSTREAMREAD */, filepath);
    if (!stream) {
        __android_log_print(ANDROID_LOG_ERROR, "WorldRPGNeon", "NativeLoadDFF: RwStreamOpen failed for %s", filepath);
        return nullptr;
    }

    void* clump = nullptr;
    if (g_symbols.rwStreamFindChunk) {
        if (g_symbols.rwStreamFindChunk(stream, RW_CHUNK_CLUMP, nullptr, nullptr)) {
            clump = g_symbols.rpClumpStreamRead(stream);
        }
    } else {
        clump = g_symbols.rpClumpStreamRead(stream);
    }

    g_symbols.rwStreamClose(stream, nullptr);
    __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", "NativeLoadDFF: %s -> clump=%p", filepath, clump);
    return clump;
}

void* NativeLoadTXD(const char* filepath) {
    if (!filepath || filepath[0] == '\0') return nullptr;
    g_symbols.EnsureInit();

    if (g_symbols.loadTexDictionary) {
        void* txd = g_symbols.loadTexDictionary(filepath);
        if (txd) {
            __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", "NativeLoadTXD: LoadTexDictionary loaded %s -> %p", filepath, txd);
            return txd;
        }
    }

    if (g_symbols.rwStreamOpen && g_symbols.rwStreamClose) {
        void* stream = g_symbols.rwStreamOpen(1, 1, filepath);
        if (stream) {
            void* txd = nullptr;
            if (g_symbols.rwStreamFindChunk && g_symbols.rwStreamFindChunk(stream, RW_CHUNK_TEXDICTIONARY, nullptr, nullptr)) {
                if (g_symbols.rwTexDictStreamRead) txd = g_symbols.rwTexDictStreamRead(stream);
                else if (g_symbols.rwTexDictGtaStreamRead) txd = g_symbols.rwTexDictGtaStreamRead(stream);
            }
            g_symbols.rwStreamClose(stream, nullptr);
            if (txd) {
                __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", "NativeLoadTXD: StreamRead loaded %s -> %p", filepath, txd);
                return txd;
            }
        }
    }

    __android_log_print(ANDROID_LOG_ERROR, "WorldRPGNeon", "NativeLoadTXD: failed for %s", filepath);
    return nullptr;
}

bool NativeReplaceModel(void* clump, uint32_t modelId) {
    if (!clump || modelId > 26000) return false;
    g_symbols.EnsureInit();
    if (!g_symbols.modelInfoPtrs) return false;

    void** table = *g_symbols.modelInfoPtrs;
    if (!table) return false;

    void* modelInfo = table[modelId];
    if (!modelInfo) {
        __android_log_print(ANDROID_LOG_ERROR, "WorldRPGNeon", "NativeReplaceModel: null modelInfo for %u", modelId);
        return false;
    }

    if (modelId >= 400 && modelId <= 611 && g_symbols.vehicleSetClump) {
        g_symbols.vehicleSetClump(modelInfo, clump);
        __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", "NativeReplaceModel: vehicleSetClump model=%u clump=%p", modelId, clump);
        return true;
    } else if (g_symbols.clumpSetClump) {
        g_symbols.clumpSetClump(modelInfo, clump);
        __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", "NativeReplaceModel: clumpSetClump model=%u clump=%p", modelId, clump);
        return true;
    }
    return false;
}

bool NativeImportTXD(void* txd, uint32_t modelId) {
    if (!txd || modelId > 26000) return false;
    g_symbols.EnsureInit();
    if (!g_symbols.modelInfoPtrs) return false;

    void** table = *g_symbols.modelInfoPtrs;
    if (!table) return false;

    void* modelInfo = table[modelId];
    if (!modelInfo) return false;

    int16_t txdIndex = *reinterpret_cast<int16_t*>(reinterpret_cast<uintptr_t>(modelInfo) + 0x24);
    if (txdIndex < 0) return false;

    if (g_symbols.txdPoolPtr) {
        void* pool = *g_symbols.txdPoolPtr;
        if (pool) {
            void** objects = *reinterpret_cast<void***>(pool);
            int8_t* flags = *reinterpret_cast<int8_t**>(reinterpret_cast<uintptr_t>(pool) + 8);
            if (objects && flags && flags[txdIndex] >= 0) {
                void* txdSlot = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(objects) + (txdIndex * 64));
                void* dstTxd = *reinterpret_cast<void**>(txdSlot);
                if (dstTxd && g_symbols.addTexDictionaries) {
                    g_symbols.addTexDictionaries(dstTxd, txd);
                    __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon",
                        "NativeImportTXD: merged textures into dstTxd=%p for model=%u (slot=%d)", dstTxd, modelId, (int)txdIndex);
                    return true;
                } else {
                    *reinterpret_cast<void**>(txdSlot) = txd;
                    __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon",
                        "NativeImportTXD: assigned txd=%p directly to slot=%d for model=%u", txd, (int)txdIndex, modelId);
                    return true;
                }
            }
        }
    }

    __android_log_print(ANDROID_LOG_INFO, "WorldRPGNeon", "NativeImportTXD: model=%u txdIndex=%d txd=%p", modelId, static_cast<int>(txdIndex), txd);
    return true;
}

void NativeDestroyClump(void* clump) {
    if (!clump) return;
    g_symbols.EnsureInit();
    if (g_symbols.rpClumpDestroy) g_symbols.rpClumpDestroy(clump);
}

void NativeDestroyTXD(void* txd) {
    if (!txd) return;
    g_symbols.EnsureInit();
    if (g_symbols.rwTexDictDestroy) g_symbols.rwTexDictDestroy(txd);
}

} // namespace WorldRPG::Script

#else

// Host/Desktop stub for unit tests
namespace WorldRPG::Script {

void* NativeLoadDFF(const char* /*filepath*/) { return nullptr; }
void* NativeLoadTXD(const char* /*filepath*/) { return nullptr; }
bool NativeReplaceModel(void* /*clump*/, uint32_t /*modelId*/) { return false; }
bool NativeImportTXD(void* /*txd*/, uint32_t /*modelId*/) { return false; }
void NativeDestroyClump(void* /*clump*/) {}
void NativeDestroyTXD(void* /*txd*/) {}

} // namespace WorldRPG::Script

#endif
