#include "runtime/arc_layout.h"
#include "runtime/allocation_internal.h"
#include "runtime/string_layout.h"
#include "runtime/string_internal.h"
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

static int zap_net_last_error = 0;
static long zap_tls_last_error_code = 0;

long zap_sum_variadic(long count, ...) {
  va_list args;
  va_start(args, count);
  long sum = 0;
  for (long i = 0; i < count; ++i) {
    sum += va_arg(args, long);
  }
  va_end(args);
  return sum;
}

static long zap_process_argc = 0;
static char **zap_process_argv = NULL;

void __zap_process_set_args(int argc, char **argv) {
  zap_process_argc = argc;
  zap_process_argv = argv;
}

long zap_printf(zap_string_t format, ...) {
  char *fmt = zap_string_to_cstr(format);
  if (!fmt)
    return -1;

  va_list args;
  va_start(args, format);
  long written = vprintf(fmt, args);
  va_end(args);

  free(fmt);
  return written;
}

long zap_printfln(zap_string_t format, ...) {
  char *fmt = zap_string_to_cstr(format);
  if (!fmt)
    return -1;

  va_list args;
  va_start(args, format);
  long written = vprintf(fmt, args);
  va_end(args);

  free(fmt);

  if (written < 0)
    return written;
  if (printf("\n") < 0)
    return -1;
  return written + 1;
}

zap_string_t getln() {
  char *line = NULL;
  size_t len = 0;
  size_t read = getline(&line, &len, stdin);
  if (read == -1) {
    free(line);
    return (zap_string_t){.ptr = NULL, .len = 0};
  }
  // Remove newline if present
  if (read > 0 && line[read - 1] == '\n') {
    line[--read] = '\0';
  }
  char *owned = zap_string_alloc_owned(read);
  if (!owned) {
    free(line);
    return (zap_string_t){.ptr = NULL, .len = 0};
  }
  if (read > 0) {
    memcpy(owned, line, read);
  }
  owned[read] = '\0';
  free(line);
  zap_string_t result = {.ptr = owned, .len = read};
  return result;
}

long argc() { return zap_process_argc; }

zap_string_t argv(long i) {
  if (i < 0 || i >= zap_process_argc || !zap_process_argv ||
      !zap_process_argv[i]) {
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  const char *arg = zap_process_argv[i];
  return zap_string_from_cstr(arg);
}

long len(zap_string_t s) { return s.len; }

char at(zap_string_t s, long i) {
  if (!s.ptr || i < 0 || i >= s.len) {
    return '\0';
  }
  return s.ptr[i];
}

zap_string_t slice(zap_string_t s, long start, long length) {
  if (!s.ptr || s.len <= 0 || length <= 0 || start >= s.len) {
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  if (start < 0) {
    start = 0;
  }

  long available = s.len - start;
  if (available < 0) {
    available = 0;
  }
  if (length > available) {
    length = available;
  }

  char *out = zap_string_alloc_owned((size_t)length);
  if (!out) {
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  if (length > 0) {
    memcpy(out, s.ptr + start, (size_t)length);
  }
  out[length] = '\0';
  return (zap_string_t){.ptr = out, .len = length};
}

_Bool eq(zap_string_t a, zap_string_t b) {
  if (a.len != b.len) {
    return 0;
  }

  if (a.len == 0) {
    return 1;
  }

  if (!a.ptr || !b.ptr) {
    return 0;
  }

  return memcmp(a.ptr, b.ptr, (size_t)a.len) == 0;
}

static char *zap_copy_path(zap_string_t path) {
  if (!path.ptr) {
    return NULL;
  }

  char *buffer = (char *)malloc((size_t)path.len + 1);
  if (!buffer) {
    return NULL;
  }

  memcpy(buffer, path.ptr, (size_t)path.len);
  buffer[path.len] = '\0';
  return buffer;
}

static int zap_net_bind_addrinfo(const char *host, long port, int socktype,
                                 int flags, struct addrinfo **out) {
  if (!out) {
    zap_net_last_error = EINVAL;
    return -1;
  }

  char port_buf[32];
  snprintf(port_buf, sizeof(port_buf), "%ld", port);

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = socktype;
  hints.ai_flags = flags;

  const char *node = host;
  if (host && (strcmp(host, "") == 0 || strcmp(host, "*") == 0)) {
    node = NULL;
  }

  int rc = getaddrinfo(node, port_buf, &hints, out);
  if (rc != 0) {
    if (rc == EAI_SYSTEM) {
      zap_net_last_error = errno;
    } else {
      zap_net_last_error = EINVAL;
    }
    return -1;
  }

  zap_net_last_error = 0;
  return 0;
}

long netConnect(zap_string_t host, long port) {
  if (!host.ptr || port <= 0 || port > 65535) {
    zap_net_last_error = EINVAL;
    return -1;
  }

  char *host_buf = zap_copy_path(host);
  if (!host_buf) {
    zap_net_last_error = ENOMEM;
    return -1;
  }

  struct addrinfo *res = NULL;
  if (zap_net_bind_addrinfo(host_buf, port, SOCK_STREAM, 0, &res) != 0) {
    free(host_buf);
    return -1;
  }

  long out_fd = -1;
  int last_err = ECONNREFUSED;
  for (struct addrinfo *it = res; it; it = it->ai_next) {
    int fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) {
      last_err = errno;
      continue;
    }

    if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) {
      out_fd = fd;
      last_err = 0;
      break;
    }

    last_err = errno;
    close(fd);
  }

  freeaddrinfo(res);
  free(host_buf);

  zap_net_last_error = last_err;
  return out_fd;
}

long netListen(zap_string_t host, long port) {
  if (port <= 0 || port > 65535) {
    zap_net_last_error = EINVAL;
    return -1;
  }

  char *host_buf = NULL;
  if (host.ptr) {
    host_buf = zap_copy_path(host);
    if (!host_buf) {
      zap_net_last_error = ENOMEM;
      return -1;
    }
  }

  struct addrinfo *res = NULL;
  if (zap_net_bind_addrinfo(host_buf ? host_buf : "", port, SOCK_STREAM,
                            AI_PASSIVE, &res) != 0) {
    free(host_buf);
    return -1;
  }

  long out_fd = -1;
  int last_err = EADDRNOTAVAIL;
  for (struct addrinfo *it = res; it; it = it->ai_next) {
    int fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
    if (fd < 0) {
      last_err = errno;
      continue;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    if (bind(fd, it->ai_addr, it->ai_addrlen) != 0) {
      last_err = errno;
      close(fd);
      continue;
    }

    if (listen(fd, 128) != 0) {
      last_err = errno;
      close(fd);
      continue;
    }

    out_fd = fd;
    last_err = 0;
    break;
  }

  freeaddrinfo(res);
  free(host_buf);

  zap_net_last_error = last_err;
  return out_fd;
}

long netAccept(long listenerFd) {
  if (listenerFd < 0) {
    zap_net_last_error = EINVAL;
    return -1;
  }

  int fd = accept((int)listenerFd, NULL, NULL);
  if (fd < 0) {
    zap_net_last_error = errno;
    return -1;
  }

  zap_net_last_error = 0;
  return fd;
}

long netClose(long fd) {
  if (fd < 0) {
    zap_net_last_error = EINVAL;
    return EINVAL;
  }

  if (close((int)fd) != 0) {
    zap_net_last_error = errno;
    return errno;
  }

  zap_net_last_error = 0;
  return 0;
}

long netSend(long fd, zap_string_t data) {
  if (fd < 0 || !data.ptr) {
    zap_net_last_error = EINVAL;
    return -1;
  }

  size_t total = 0;
  size_t target = data.len > 0 ? (size_t)data.len : 0;

  while (total < target) {
    ssize_t n = send((int)fd, data.ptr + total, target - total, 0);
    if (n < 0) {
      zap_net_last_error = errno;
      return -1;
    }
    if (n == 0) {
      break;
    }
    total += (size_t)n;
  }

  zap_net_last_error = 0;
  return (long)total;
}

zap_string_t netRecv(long fd, long maxLen) {
  if (fd < 0 || maxLen <= 0) {
    zap_net_last_error = EINVAL;
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  size_t cap = (size_t)maxLen;
  char *buf = zap_string_alloc_owned(cap);
  if (!buf) {
    zap_net_last_error = ENOMEM;
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  ssize_t n;
  do {
    n = recv((int)fd, buf, cap, 0);
  } while (n < 0 && errno == EINTR);
  if (n < 0) {
    zap_net_last_error = errno;
    zap_string_release_ptr(buf);
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  buf[n] = '\0';
  zap_net_last_error = 0;
  return (zap_string_t){.ptr = buf, .len = (long)n};
}

zap_string_t netResolve(zap_string_t host) {
  if (!host.ptr || host.len == 0) {
    zap_net_last_error = EINVAL;
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  char *host_buf = zap_copy_path(host);
  if (!host_buf) {
    zap_net_last_error = ENOMEM;
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  struct addrinfo hints;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;

  struct addrinfo *res = NULL;
  int rc = getaddrinfo(host_buf, NULL, &hints, &res);
  free(host_buf);
  if (rc != 0) {
    if (rc == EAI_SYSTEM) {
      zap_net_last_error = errno;
    } else {
      zap_net_last_error = EINVAL;
    }
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  char ipbuf[INET6_ADDRSTRLEN];
  memset(ipbuf, 0, sizeof(ipbuf));

  for (struct addrinfo *it = res; it; it = it->ai_next) {
    void *addr_ptr = NULL;
    if (it->ai_family == AF_INET) {
      struct sockaddr_in *sa = (struct sockaddr_in *)it->ai_addr;
      addr_ptr = &(sa->sin_addr);
    } else if (it->ai_family == AF_INET6) {
      struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)it->ai_addr;
      addr_ptr = &(sa6->sin6_addr);
    }

    if (addr_ptr &&
        inet_ntop(it->ai_family, addr_ptr, ipbuf, sizeof(ipbuf)) != NULL) {
      break;
    }
  }

  freeaddrinfo(res);

  if (ipbuf[0] == '\0') {
    zap_net_last_error = EADDRNOTAVAIL;
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  zap_net_last_error = 0;
  return zap_string_from_ptrlen(ipbuf, (long)strlen(ipbuf));
}

long netLastError() { return zap_net_last_error; }

typedef struct {
  SSL_CTX *context;
  SSL *ssl;
  int fd;
} zap_tls_session_t;

static void zap_tls_session_free(zap_tls_session_t *session) {
  if (!session) {
    return;
  }
  if (session->ssl) {
    SSL_shutdown(session->ssl);
    SSL_free(session->ssl);
  }
  if (session->context) {
    SSL_CTX_free(session->context);
  }
  if (session->fd >= 0) {
    close(session->fd);
  }
  free(session);
}

long zap_tls_connect(zap_string_t host, long port) {
  const long fd = netConnect(host, port);
  if (fd < 0) {
    zap_tls_last_error_code = zap_net_last_error;
    return 0;
  }

  char *host_buffer = zap_copy_path(host);
  if (!host_buffer) {
    close((int)fd);
    zap_tls_last_error_code = ENOMEM;
    return 0;
  }

  zap_tls_session_t *session = calloc(1, sizeof(*session));
  if (!session) {
    free(host_buffer);
    close((int)fd);
    zap_tls_last_error_code = ENOMEM;
    return 0;
  }
  session->fd = (int)fd;
  session->context = SSL_CTX_new(TLS_client_method());
  if (!session->context ||
      SSL_CTX_set_default_verify_paths(session->context) != 1) {
    free(host_buffer);
    zap_tls_session_free(session);
    zap_tls_last_error_code = EIO;
    return 0;
  }
  SSL_CTX_set_verify(session->context, SSL_VERIFY_PEER, NULL);
  session->ssl = SSL_new(session->context);
  if (!session->ssl ||
      SSL_set_tlsext_host_name(session->ssl, host_buffer) != 1 ||
      SSL_set1_host(session->ssl, host_buffer) != 1 ||
      SSL_set_fd(session->ssl, session->fd) != 1 ||
      SSL_connect(session->ssl) != 1) {
    free(host_buffer);
    zap_tls_session_free(session);
    zap_tls_last_error_code = EIO;
    return 0;
  }
  free(host_buffer);

  if (SSL_get_verify_result(session->ssl) != X509_V_OK) {
    zap_tls_session_free(session);
    zap_tls_last_error_code = EACCES;
    return 0;
  }

  zap_tls_last_error_code = 0;
  return (long)(intptr_t)session;
}

long zap_tls_send(long handle, zap_string_t data) {
  zap_tls_session_t *session = (zap_tls_session_t *)(intptr_t)handle;
  if (!session || !session->ssl || !data.ptr) {
    zap_tls_last_error_code = EINVAL;
    return -1;
  }

  size_t total = 0;
  const size_t target = data.len > 0 ? (size_t)data.len : 0;
  while (total < target) {
    const size_t remaining = target - total;
    const int request = remaining > INT_MAX ? INT_MAX : (int)remaining;
    const int written = SSL_write(session->ssl, data.ptr + total, request);
    if (written <= 0) {
      zap_tls_last_error_code = EIO;
      return -1;
    }
    total += (size_t)written;
  }
  zap_tls_last_error_code = 0;
  return (long)total;
}

zap_string_t zap_tls_recv(long handle, long max_len) {
  zap_tls_session_t *session = (zap_tls_session_t *)(intptr_t)handle;
  if (!session || !session->ssl || max_len <= 0) {
    zap_tls_last_error_code = EINVAL;
    return (zap_string_t){.ptr = NULL, .len = 0};
  }

  const size_t capacity = (size_t)max_len;
  char *buffer = zap_string_alloc_owned(capacity);
  if (!buffer) {
    zap_tls_last_error_code = ENOMEM;
    return (zap_string_t){.ptr = NULL, .len = 0};
  }
  const int request = capacity > INT_MAX ? INT_MAX : (int)capacity;
  const int received = SSL_read(session->ssl, buffer, request);
  if (received <= 0) {
    const int ssl_error = SSL_get_error(session->ssl, received);
    zap_string_release_ptr(buffer);
    if (ssl_error == SSL_ERROR_ZERO_RETURN) {
      zap_tls_last_error_code = 0;
    } else {
      zap_tls_last_error_code = EIO;
    }
    return (zap_string_t){.ptr = NULL, .len = 0};
  }
  buffer[received] = '\0';
  zap_tls_last_error_code = 0;
  return (zap_string_t){.ptr = buffer, .len = received};
}

long zap_tls_close(long handle) {
  zap_tls_session_t *session = (zap_tls_session_t *)(intptr_t)handle;
  if (!session) {
    zap_tls_last_error_code = EINVAL;
    return EINVAL;
  }
  zap_tls_session_free(session);
  zap_tls_last_error_code = 0;
  return 0;
}

long zap_tls_last_error() { return zap_tls_last_error_code; }
