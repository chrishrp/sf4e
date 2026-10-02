#pragma once

#include <cstdint>
#include <string>

namespace sf4e {
namespace DirectEndpoint {

// Host-order IPv4, without OS dependencies so the server and client use the
// same checks. Keep private/LAN/Tailscale addresses: they can reach real peers.
inline bool ParseIPv4(const std::string& text, uint32_t& address) {
    uint32_t result = 0;
    size_t offset = 0;
    for (int octet = 0; octet < 4; ++octet) {
        const size_t begin = offset;
        unsigned value = 0;
        while (offset < text.size() && text[offset] >= '0' && text[offset] <= '9') {
            value = value * 10 + (text[offset++] - '0');
            if (value > 255 || offset - begin > 3) return false;
        }
        if (offset == begin || (offset - begin > 1 && text[begin] == '0')) return false;
        result = (result << 8) | value;
        if (octet < 3) {
            if (offset == text.size() || text[offset++] != '.') return false;
        }
    }
    if (offset != text.size()) return false;
    address = result;
    return true;
}

inline bool Usable(const std::string& ip, uint16_t port) {
    uint32_t address = 0;
    if (port == 0 || !ParseIPv4(ip, address)) return false;
    const uint32_t first = address >> 24;
    // A loopback address names the receiving PC, not the offering PC. Sending
    // our shared punch token there can otherwise prove a connection to self.
    return first != 0 && first != 127 && first < 224;
}

inline bool Same(const std::string& firstIp, uint16_t firstPort,
    const std::string& secondIp, uint16_t secondPort) {
    uint32_t first = 0, second = 0;
    return firstPort != 0 && firstPort == secondPort
        && ParseIPv4(firstIp, first) && ParseIPv4(secondIp, second)
        && first == second;
}

}
}
