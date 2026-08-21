#include "runtime/arc_layout.h"
#include "runtime/allocation_internal.h"
#include "runtime/string_layout.h"
#include "runtime/string_internal.h"
#include "runtime/network_internal.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <netdb.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

_Static_assert(offsetof(zap_string_header_t, refs) == 0,
               "String ABI: refs offset mismatch");
_Static_assert(offsetof(zap_string_header_t, len) == sizeof(int64_t),
               "String ABI: len offset mismatch");
_Static_assert(sizeof(zap_string_header_t) == 2 * sizeof(int64_t),
               "String ABI: unexpected header padding");
_Static_assert(offsetof(zap_string_t, ptr) == 0,
               "String ABI: ptr offset mismatch");
_Static_assert(offsetof(zap_string_t, len) == sizeof(const char *),
               "String ABI: value len offset mismatch");
