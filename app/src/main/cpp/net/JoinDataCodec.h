#pragma once

#include "Checksums.h"
#include "raknet/RakNetWrapper.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <string_view>

namespace WorldRPG::Net {

constexpr uint16_t NEON_NETCODE_VERSION = 0x41E7;

enum class JoinProfile : uint8_t { Automatic = 0, Standard = 1, LegacyMobile = 2 };

// Device captures 2026-11-08/16: these endpoints use the Oracle adapter,
// which requires logical packet 19 and rewrites the peer to bitstream 0x0030.
// MOD_NAME auth=0 alone cannot distinguish this adapter from a normal server.
inline JoinProfile SelectJoinProfile(std::string /*host*/, uint16_t /*port*/, JoinProfile requested) {
    if (requested != JoinProfile::Automatic) return requested;
    return JoinProfile::Standard;
}

// CClientGame::StartGame (desktop CClientGame.cpp:715-769) and
// CPlayerJoinDataPacket::Read: retain the same field order and bit widths.
// Standard peers honestly advertise the 0x0030 layout implemented on Android.
// Legacy keeps the captured 763-byte envelope, marker 676/body 668, unchanged.
inline bool BuildJoinData(JoinProfile profile, const std::string& nick, const std::string& password,
                          const std::string& ticket, BitStreamWriter& out) {
    if ((profile != JoinProfile::Standard && profile != JoinProfile::LegacyMobile) || nick.empty() || nick.size() > 22 || ticket.size() > 4096 ||
        ticket.find('\0') != std::string::npos) return false;
    const bool legacy = profile == JoinProfile::LegacyMobile;
    // The deployed legacy adapter truncates the extracted body to 256 bytes.
    // Never silently truncate an Identity ticket or downgrade authentication.
    if (legacy && !ticket.empty()) return false;
    out.Write(static_cast<uint8_t>(legacy ? 19 : 4));
    if (legacy) {
        const std::array<uint8_t, 667> padding{};
        out.WriteBytes(padding.data(), padding.size());
    }
    out.Write(legacy ? uint16_t{0x41E6} : NEON_NETCODE_VERSION);
    out.Write(static_cast<uint16_t>(0x0170));
    out.Write(static_cast<uint16_t>(legacy ? 0x003D : 0x0030));
    auto string = [&](std::string_view value) {
        out.Write(static_cast<uint16_t>(value.size()));
        if (!value.empty()) out.WriteBytes(reinterpret_cast<const uint8_t*>(value.data()), value.size());
    };
    string(legacy ? "1.6.0-9.29999.0" : "1.7.0-9.29999.0");
    const uint8_t optionalUpdate = 0;
    out.WriteBits(&optionalUpdate, 1);
    out.Write(static_cast<uint8_t>(20));
    std::array<uint8_t, 22> nickname{};
    std::copy(nick.begin(), nick.end(), nickname.begin());
    out.WriteBytes(nickname.data(), nickname.size());
    std::array<uint8_t, 16> digest{};
    if (!password.empty()) {
        Md5 md5;
        md5.Update(password.data(), password.size());
        md5.Finish(digest.data());
    }
    out.WriteBytes(digest.data(), digest.size());
    const std::array<uint8_t, 32> community{};
    out.WriteBytes(community.data(), community.size());
    if (!legacy) string(ticket);
    return true;
}
} // namespace WorldRPG::Net
