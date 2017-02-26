#pragma once

#include <cstdint>

// Check if this is Visual Studio
#ifdef _MSC_VER
    // This is Visual Studio
    #define ROBERTO_BEGIN_PACK __pragma( pack(push, 1) )
    #define ROBERTO_END_PACK __pragma( pack(pop) )
#else
    // Not Visual Studio. Assume this is gcc compatible
    #define ROBERTO_BEGIN_PACK 
    #define ROBERTO_END_PACK __attribute__((packed))
#endif // _MSC_VER

namespace roberto {

enum class SocksAuthentication {
    NONE = 0
};

enum class AddressType {
    IPV4 = 1,
    DOMAIN_NAME = 3,
    IPV6 = 4
};

enum class CommandType {
    CONNECT = 1,
    BIND = 2,
    UDP_ASSOCIATE = 3
};

enum class ReplyType {
    SUCCESS = 0,
    GENERAL_FAILURE = 1,
    CONNECTION_NOT_ALLOWED = 2,
    NETWORK_UNREACHABLE = 3,
    HOST_UNREACHABLE = 4,
    CONNECTION_REFUSED = 5,
    TTL_EXPIRED = 6,
    COMMAND_NOT_SUPPORTED = 7,
    ADDRESS_NOT_SUPPORTED = 8,
};

ROBERTO_BEGIN_PACK
struct MethodSelectionRequest {
    uint8_t version;
    uint8_t method_count;
} ROBERTO_END_PACK;

ROBERTO_BEGIN_PACK
struct MethodSelectionResponse {
    uint8_t version;
    uint8_t method;
} ROBERTO_END_PACK;

ROBERTO_BEGIN_PACK
struct SocksCommandHeader {
    uint8_t version;
    uint8_t command;
    uint8_t reserved;
    uint8_t address_type;
} ROBERTO_END_PACK;

ROBERTO_BEGIN_PACK
struct SocksCommandEndpointIPv4 {
    uint32_t address;
    uint16_t port;
} ROBERTO_END_PACK;

ROBERTO_BEGIN_PACK
struct SocksCommandEndpointIPv6 {
    uint8_t address[16];
    uint16_t port;
} ROBERTO_END_PACK;

ROBERTO_BEGIN_PACK
struct SocksCommandResponseHeader {
    uint8_t version;
    uint8_t reply;
    uint8_t reserved;
    uint8_t address_type;
} ROBERTO_END_PACK;

ROBERTO_BEGIN_PACK
struct SocksCommandResponseEndpointIPv4 {
    uint32_t bind_ipv4_address;
    uint16_t bind_port;
} ROBERTO_END_PACK;

ROBERTO_BEGIN_PACK
struct SocksCommandResponseEndpointIPv6 {
    uint8_t bind_ipv6_address[16];
    uint16_t bind_port;
} ROBERTO_END_PACK;

} // roberto
