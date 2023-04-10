#ifndef CLOX_COMMON_H
#define CLOX_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CONSTANTS_MAX
#define CONSTANTS_MAX (UINT16_MAX + 1)
#endif

#ifndef FRAMES_MAX
#define FRAMES_MAX 1000
#endif

#define NAN_BOXING

// #define DEBUG_PRINT_TOKENS
// #define DEBUG_PRINT_CODE

// #define DEBUG_TRACE_EXECUTION
// #define DEBUG_CHECK_STACK

// #define DEBUG_STRESS_GC
// #define DEBUG_LOG_GC

#define UINT8_COUNT (UINT8_MAX + 1)

#endif
