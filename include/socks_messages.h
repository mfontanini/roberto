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

} // roberto
