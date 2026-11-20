#pragma once

namespace WorldRPG::Script {

void* NativeLoadDFF(const char* filepath);
void* NativeLoadTXD(const char* filepath);
void NativeDestroyClump(void* clump);
void NativeDestroyTXD(void* txd);

} // namespace WorldRPG::Script
