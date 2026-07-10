// Dedicated translation unit for minimp4's implementation. Its internal symbols
// collide with minih264 if both implementations are compiled together, so each
// lives in its own .c file.
//
// The vendored upstream header trips warnings under -Wall -Wextra; they are
// suppressed here so the project's own code stays warning-clean.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wimplicit-function-declaration"
#pragma GCC diagnostic ignored "-Wint-conversion"
#if defined(__clang__)
// clang-only: the header redefines MP4E_mux_t (a no-op under C11, warned under c99).
#pragma clang diagnostic ignored "-Wtypedef-redefinition"
#endif

#define MINIMP4_IMPLEMENTATION
#include "minimp4.h"

#pragma GCC diagnostic pop
