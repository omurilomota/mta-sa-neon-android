#pragma once

#include <cstdint>

namespace WorldRPG::Script {

void* NativeLoadDFF(const char* filepath);
void* NativeLoadTXD(const char* filepath);
bool NativeReplaceModel(void* clump, uint32_t modelId);
bool NativeImportTXD(void* txd, uint32_t modelId);
void NativeDestroyClump(void* clump);
void NativeDestroyTXD(void* txd);

} // namespace WorldRPG::Script
