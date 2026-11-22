/* Copyright (C) 2026 ItsJokerZz

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>. */

#include "binldr.h"

#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <elf.h>
#include <fcntl.h>
#include <mutex>
#include <netinet/in.h>
#include <new>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <vector>

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

static constexpr size_t DEFAULT_MAX_PAYLOAD = 32u * 1024u * 1024u;
static constexpr unsigned DEFAULT_TIMEOUT = 60;
static constexpr uint16_t ELFLDR_PORT = 9021;
static constexpr size_t MAX_HTTP_HEADER = 8192;
static constexpr size_t PAGE_SIZE_PS4 = 0x4000;
static constexpr size_t MAX_REQUESTED_SYSMODULES = 32;

extern "C" {
extern const unsigned char binldr_elfldr_bin[];
extern const unsigned long binldr_elfldr_bin_size[];
extern const unsigned char binldr_binrunner_elf[];
extern const unsigned long binldr_binrunner_elf_size[];
}

struct binldr_server {
  binldr_config_t config{};
  std::atomic<bool> stopping{ false };
  std::atomic<bool> started{ false };
  std::atomic<int> listener{ -1 };
  std::atomic<int> client{ -1 };
  pthread_t thread{};
  std::mutex lifecycle;
  std::vector<binldr_sysmodule_t> sysmodules;
};

static bool
send_all(int fd, const void *buffer, size_t size) {
  const uint8_t *data = static_cast<const uint8_t *>(buffer);
  size_t sent = 0;
  while(sent < size) {
    ssize_t count = send(fd, data + sent, size - sent, 0);
    if(count < 0 && errno == EINTR)
      continue;
    if(count <= 0) {
      if(!count)
        errno = EIO;
      return false;
    }
    sent += static_cast<size_t>(count);
  }
  return true;
}

static void
set_socket_timeout(int fd, unsigned seconds) {
  timeval timeout{};
  timeout.tv_sec = seconds ? seconds : 1;
  setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
#ifdef SO_NOSIGPIPE
  int enabled = 1;
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
}

static bool
receive_exact(binldr_server_t *server, int fd, void *buffer, size_t size,
              const std::chrono::steady_clock::time_point &deadline,
              size_t *received = nullptr) {
  uint8_t *data = static_cast<uint8_t *>(buffer);
  size_t total = 0;
  while(total < size && !server->stopping) {
    if(std::chrono::steady_clock::now() >= deadline)
      break;
    ssize_t count = recv(fd, data + total, size - total, 0);
    if(count < 0 && errno == EINTR)
      continue;
    if(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      continue;
    if(count <= 0)
      break;
    total += static_cast<size_t>(count);
  }
  if(received)
    *received = total;
  return total == size;
}

static bool
is_elf(const uint8_t *data, size_t size) {
  return data && size >= 4 && data[0] == 0x7f && data[1] == 'E'
         && data[2] == 'L' && data[3] == 'F';
}

static bool
is_legacy_elf(const uint8_t *data, size_t size) {
  if(!is_elf(data, size) || size < sizeof(Elf64_Ehdr))
    return false;
  Elf64_Ehdr header;
  memcpy(&header, data, sizeof(header));
  if(header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_machine != EM_X86_64
     || header.e_phentsize != sizeof(Elf64_Phdr) || !header.e_phnum
     || header.e_phnum > 128 || header.e_phoff > size
     || (size - header.e_phoff) / sizeof(Elf64_Phdr) < header.e_phnum)
    return false;
  for(unsigned i = 0; i < header.e_phnum; ++i) {
    Elf64_Phdr segment;
    memcpy(&segment, data + header.e_phoff + i * sizeof(segment),
           sizeof(segment));
    if(segment.p_type != PT_DYNAMIC)
      continue;
    if(segment.p_offset > size || size - segment.p_offset < segment.p_filesz)
      return false;
    for(uint64_t offset = 0; offset + sizeof(Elf64_Dyn) <= segment.p_filesz;
        offset += sizeof(Elf64_Dyn)) {
      Elf64_Dyn entry;
      memcpy(&entry, data + segment.p_offset + offset, sizeof(entry));
      if(entry.d_tag == DT_NULL)
        break;
      if(entry.d_tag == DT_NEEDED)
        return false;
    }
  }
  return true;
}

static bool
is_ps4_self(const uint8_t *data, size_t size) {
  return data && size >= 4 && data[0] == 0x4f && data[1] == 0x15
         && data[2] == 0x3d && data[3] == 0x1d;
}

static int
connect_loopback(uint16_t port, unsigned timeout_seconds) {
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0)
    return -1;
  set_socket_timeout(fd, timeout_seconds);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if(connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))) {
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
  }
  return fd;
}

static void *
map_executable(size_t size) {
  register long flags __asm__("r10") = MAP_PRIVATE | MAP_ANONYMOUS;
  register long descriptor __asm__("r8") = -1;
  register long offset __asm__("r9") = 0;
  unsigned long result;
  char failed;
  __asm__ __volatile__("syscall"
                       : "=a"(result), "=@ccc"(failed), "+r"(flags),
                         "+r"(descriptor), "+r"(offset)
                       : "a"(477L), "D"(0L), "S"(size),
                         "d"(PROT_READ | PROT_WRITE | PROT_EXEC)
                       : "rcx", "r11", "memory");
  if(failed) {
    errno = static_cast<int>(result);
    return MAP_FAILED;
  }
  return reinterpret_cast<void *>(result);
}

static bool
elfldr_listening(uint16_t port) {
  int fd = connect_loopback(port, 1);
  if(fd < 0)
    return false;
  close(fd);
  return true;
}

static bool
start_elfldr(uint16_t port) {
  size_t loader_size = static_cast<size_t>(binldr_elfldr_bin_size[0]);
  size_t mapped_size
      = (loader_size + PAGE_SIZE_PS4 - 1) & ~(PAGE_SIZE_PS4 - 1);
  void *image = map_executable(mapped_size);
  if(image == MAP_FAILED)
    return false;
  memcpy(image, binldr_elfldr_bin, loader_size);

  struct sigaction previous {
  }, ignored{};
  ignored.sa_handler = SIG_IGN;
  sigemptyset(&ignored.sa_mask);
  bool changed_signal = sigaction(SIGCHLD, &ignored, &previous) == 0;
  using bootstrap_fn = int (*)(void);
  reinterpret_cast<bootstrap_fn>(image)();
  int saved = errno;
  if(changed_signal)
    sigaction(SIGCHLD, &previous, nullptr);
  munmap(image, mapped_size);
  errno = saved;

  const auto deadline
      = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while(std::chrono::steady_clock::now() < deadline) {
    if(elfldr_listening(port))
      return true;
    usleep(100000);
  }
  errno = ETIMEDOUT;
  return false;
}

static bool
ensure_elfldr() {
  static std::mutex loader_mutex;
  std::lock_guard<std::mutex> lock(loader_mutex);
  const uint16_t port = ELFLDR_PORT;
  if(elfldr_listening(port))
    return true;
  return start_elfldr(port);
}

static bool
relay_to_elfldr(binldr_server_t *server, const uint8_t *payload, size_t size) {
  if(!ensure_elfldr())
    return false;
  int fd
      = connect_loopback(ELFLDR_PORT, server->config.request_timeout_seconds);
  if(fd < 0)
    return false;
  bool sent = send_all(fd, payload, size);
  int saved = errno;
  shutdown(fd, SHUT_WR);
  close(fd);
  errno = saved;
  return sent;
}

extern "C" int
binldr_spawn(const uint8_t *elf, size_t size) {
  if(!ensure_elfldr())
    return -1;
  int fd = connect_loopback(ELFLDR_PORT, 15);
  if(fd < 0)
    return -1;
  bool sent = send_all(fd, elf, size);
  shutdown(fd, SHUT_WR);
  close(fd);
  return sent ? 0 : -1;
}

static bool
relay_binary_to_runner(binldr_server_t *server, const uint8_t *payload,
                       size_t size) {
  if(!ensure_elfldr())
    return false;

  char path[] = "/system_tmp/binldr-payload-XXXXXX";
  int file = mkstemp(path);
  if(file < 0)
    return false;

  size_t written = 0;
  while(written < size) {
    ssize_t count = write(file, payload + written, size - written);
    if(count < 0 && errno == EINTR)
      continue;
    if(count <= 0) {
      int saved = count < 0 ? errno : EIO;
      close(file);
      unlink(path);
      errno = saved;
      return false;
    }
    written += static_cast<size_t>(count);
  }
  if(close(file)) {
    int saved = errno;
    unlink(path);
    errno = saved;
    return false;
  }

  int fd
      = connect_loopback(ELFLDR_PORT, server->config.request_timeout_seconds);
  if(fd < 0) {
    int saved = errno;
    unlink(path);
    errno = saved;
    return false;
  }

  const size_t runner_size = static_cast<size_t>(binldr_binrunner_elf_size[0]);
  bool sent = send_all(fd, binldr_binrunner_elf, runner_size)
              && send_all(fd, path, strlen(path)) && send_all(fd, "\n", 1);
  if(sent) {
    static const char module_magic[] = "BLDRMOD1";
    const uint16_t module_count
        = static_cast<uint16_t>(server->sysmodules.size());
    sent = send_all(fd, module_magic, sizeof(module_magic) - 1)
           && send_all(fd, &module_count, sizeof(module_count));
    for(const binldr_sysmodule_t &module : server->sysmodules) {
      const uint8_t kind = module.internal ? 1 : 0;
      sent = sent && send_all(fd, &kind, sizeof(kind))
             && send_all(fd, &module.id, sizeof(module.id));
    }
  }
  int saved = errno;
  shutdown(fd, SHUT_WR);

  bool ready = false;
  bool timed_out = false;
  if(sent) {
    static const char marker[] = "PAYLOAD_READY\n";
    char buffer[512];
    size_t matched = 0;
    timeval timeout{};
    timeout.tv_sec
        = static_cast<time_t>(server->config.request_timeout_seconds);
    if(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout))) {
      saved = errno;
      sent = false;
    }
    errno = 0;
    while(sent && !ready) {
      ssize_t count = recv(fd, buffer, sizeof(buffer), 0);
      if(count < 0 && errno == EINTR)
        continue;
      if(count < 0
         && (errno == EAGAIN || errno == EWOULDBLOCK || errno == ETIMEDOUT)) {
        timed_out = true;
        break;
      }
      if(count <= 0)
        break;
      for(ssize_t i = 0; i < count; ++i) {
        if(buffer[i] == marker[matched]) {
          if(++matched == sizeof(marker) - 1) {
            ready = true;
            break;
          }
        } else {
          matched = buffer[i] == marker[0] ? 1 : 0;
        }
      }
    }
  }
  const bool pending = sent && !ready && timed_out;
  if(!ready && !pending)
    unlink(path);
  close(fd);
  if(!ready && !pending) {
    if(!sent)
      errno = saved ? saved : EIO;
    else
      errno = EIO;
  }
  return ready || pending;
}

static void
reply(int fd, bool http, int status, const char *text) {
  std::string body(text ? text : "");
  body += '\n';
  if(http) {
    char status_line[64];
    snprintf(status_line, sizeof(status_line), "HTTP/1.1 %d %s\r\n", status,
             status < 300 ? "OK" : "Error");
    std::string header(status_line);
    header += "Connection: close\r\nContent-Type: text/plain\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "Content-Length: "
              + std::to_string(body.size()) + "\r\n\r\n";
    if(!send_all(fd, header.data(), header.size()))
      return;
  }
  send_all(fd, body.data(), body.size());
}

static bool
is_http_prefix(const uint8_t *prefix) {
  static const char *methods[] = { "POST", "GET ", "OPTI", "HEAD", "PUT ",
                                   "DELE", "PATC", "CONN", "TRAC" };
  for(const char *method : methods)
    if(!memcmp(prefix, method, 4))
      return true;
  return false;
}

static bool
has_suffix(const std::string &text, const char *suffix) {
  const size_t length = strlen(suffix);
  return text.size() >= length
         && text.compare(text.size() - length, length, suffix) == 0;
}

static bool
read_payload(binldr_server_t *server, int fd, bool http,
             const uint8_t prefix[4], uint8_t **payload,
             size_t *payload_size) {
  const auto deadline
      = std::chrono::steady_clock::now()
        + std::chrono::seconds(server->config.request_timeout_seconds);
  if(http) {
    std::string header(reinterpret_cast<const char *>(prefix), 4);
    while(header.size() < MAX_HTTP_HEADER && !has_suffix(header, "\r\n\r\n")) {
      char byte;
      if(!receive_exact(server, fd, &byte, 1, deadline) || !byte) {
        reply(fd, true, 400, "Incomplete HTTP headers");
        return false;
      }
      header += byte;
    }
    if(!has_suffix(header, "\r\n\r\n")) {
      reply(fd, true, 431, "HTTP headers are too large");
      return false;
    }

    size_t line_end = header.find("\r\n");
    std::string request = header.substr(0, line_end);
    if(request == "GET / HTTP/1.1" || request == "GET /status HTTP/1.1"
       || request == "GET / HTTP/1.0" || request == "GET /status HTTP/1.0") {
      reply(fd, true, 200, "Binary payload receiver ready");
      return false;
    }
    if(request.compare(0, 9, "OPTIONS /") == 0) {
      reply(fd, true, 200, "");
      return false;
    }
    if(request != "POST / HTTP/1.1" && request != "POST / HTTP/1.0"
       && request != "POST /payload HTTP/1.1"
       && request != "POST /payload HTTP/1.0") {
      reply(fd, true, 405, "Use POST / or POST /payload");
      return false;
    }

    bool have_length = false;
    bool expect_continue = false;
    size_t length = 0;
    size_t offset = line_end + 2;
    while(offset < header.size() - 2) {
      size_t end = header.find("\r\n", offset);
      if(end == std::string::npos)
        break;
      std::string line = header.substr(offset, end - offset);
      offset = end + 2;
      size_t colon = line.find(':');
      if(colon == std::string::npos) {
        reply(fd, true, 400, "Malformed HTTP header");
        return false;
      }
      std::string key = line.substr(0, colon);
      std::string value = line.substr(colon + 1);
      for(char &character : key)
        character = static_cast<char>(
            tolower(static_cast<unsigned char>(character)));
      size_t first = value.find_first_not_of(" \t");
      size_t last = value.find_last_not_of(" \t");
      value = first == std::string::npos
                  ? ""
                  : value.substr(first, last - first + 1);
      if(key == "transfer-encoding") {
        reply(fd, true, 400, "Chunked transfer encoding is unsupported");
        return false;
      }
      if(key == "expect") {
        if(value != "100-continue") {
          reply(fd, true, 417, "Unsupported expectation");
          return false;
        }
        expect_continue = true;
      }
      if(key != "content-length")
        continue;
      if(have_length || value.empty()) {
        reply(fd, true, 400, "Invalid Content-Length");
        return false;
      }
      have_length = true;
      for(char character : value) {
        if(character < '0' || character > '9') {
          reply(fd, true, 400, "Invalid Content-Length");
          return false;
        }
        size_t digit = static_cast<size_t>(character - '0');
        if(digit > server->config.max_payload_size
           || length > (server->config.max_payload_size - digit) / 10) {
          reply(fd, true, 413, "Payload exceeds the configured limit");
          return false;
        }
        length = length * 10 + digit;
      }
    }
    if(!have_length || !length) {
      reply(fd, true, 411, "Positive Content-Length required");
      return false;
    }
    if(length > server->config.max_payload_size) {
      reply(fd, true, 413, "Payload exceeds the configured limit");
      return false;
    }
    if(expect_continue) {
      static const char response[] = "HTTP/1.1 100 Continue\r\n\r\n";
      if(!send_all(fd, response, sizeof(response) - 1))
        return false;
    }
    void *memory = mmap(nullptr, length, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if(memory == MAP_FAILED) {
      reply(fd, true, 503, "Could not allocate payload buffer");
      return false;
    }
    *payload = static_cast<uint8_t *>(memory);
    if(!receive_exact(server, fd, *payload, length, deadline)) {
      munmap(*payload, length);
      *payload = nullptr;
      reply(fd, true, 400, "Incomplete request body");
      return false;
    }
    *payload_size = length;
    return true;
  }

  const size_t capacity = server->config.max_payload_size;
  void *memory = mmap(nullptr, capacity, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(memory == MAP_FAILED) {
    reply(fd, false, 503, "Could not allocate payload buffer");
    return false;
  }
  *payload = static_cast<uint8_t *>(memory);
  memcpy(*payload, prefix, 4);
  *payload_size = 4;
  uint8_t block[16384];
  while(!server->stopping && std::chrono::steady_clock::now() < deadline) {
    ssize_t count = recv(fd, block, sizeof(block), 0);
    if(count < 0 && errno == EINTR)
      continue;
    if(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
      continue;
    if(!count)
      return true;
    if(count < 0 || *payload_size + static_cast<size_t>(count) > capacity) {
      munmap(*payload, capacity);
      *payload = nullptr;
      *payload_size = 0;
      reply(fd, false, count < 0 ? 400 : 413,
            count < 0 ? "Payload receive failed"
                      : "Payload exceeds the configured limit");
      return false;
    }
    memcpy(*payload + *payload_size, block, static_cast<size_t>(count));
    *payload_size += static_cast<size_t>(count);
  }
  munmap(*payload, capacity);
  *payload = nullptr;
  *payload_size = 0;
  reply(fd, false, 400, "Payload receive timed out");
  return false;
}

static void
dispatch_payload(binldr_server_t *server, int fd, bool http,
                 const uint8_t *payload, size_t size) {
  char message[256] = "";
  int status = 202;
  if(is_ps4_self(payload, size)
     || (is_elf(payload, size) && !is_legacy_elf(payload, size))) {
    if(!relay_to_elfldr(server, payload, size)) {
      snprintf(message, sizeof(message),
               "Could not forward payload to ELF loader: %s", strerror(errno));
      reply(fd, http, 502, message);
      return;
    }
    snprintf(message, sizeof(message),
             "Payload forwarded to ELF loader; execution is not acknowledged");
  } else if(server->config.handle_binary) {
    status = server->config.handle_binary(
        payload, size, message, sizeof(message), server->config.user_data);
    if(status < 100 || status > 599)
      status = 500;
    if(!message[0])
      snprintf(message, sizeof(message), "Binary handler returned status %d",
               status);
  } else if(relay_binary_to_runner(server, payload, size)) {
    status = 202;
    snprintf(message, sizeof(message),
             "BIN payload started in ELF loader process");
  } else {
    status = 502;
    snprintf(message, sizeof(message), "Could not start BIN payload: %s",
             strerror(errno));
  }
  reply(fd, http, status, message);
}

static void
handle_client(binldr_server_t *server, int fd) {
  set_socket_timeout(fd, server->config.request_timeout_seconds);
  uint8_t prefix[4];
  size_t prefix_size = 0;
  const auto deadline
      = std::chrono::steady_clock::now()
        + std::chrono::seconds(server->config.request_timeout_seconds);
  if(!receive_exact(server, fd, prefix, sizeof(prefix), deadline,
                    &prefix_size)) {
    if(prefix_size)
      reply(fd, false, 400, "Incomplete payload header");
    return;
  }
  bool http = is_http_prefix(prefix);
  uint8_t *payload = nullptr;
  size_t size = 0;
  if(!read_payload(server, fd, http, prefix, &payload, &size))
    return;
  dispatch_payload(server, fd, http, payload, size);
  munmap(payload, http ? size : server->config.max_payload_size);
}

static void *
server_thread(void *argument) {
  binldr_server_t *server = static_cast<binldr_server_t *>(argument);
  while(!server->stopping) {
    int listener = server->listener;
    if(listener < 0)
      break;
    pollfd event{ listener, POLLIN, 0 };
    int ready = poll(&event, 1, 500);
    if(ready < 0 && errno == EINTR)
      continue;
    if(!ready)
      continue;
    if(ready < 0 || !(event.revents & POLLIN))
      break;
    int client = accept(listener, nullptr, nullptr);
    if(client < 0) {
      if(errno == EINTR || errno == ECONNABORTED)
        continue;
      continue;
    }
    server->client = client;
    handle_client(server, client);
    server->client = -1;
    close(client);
  }
  int listener = server->listener.exchange(-1);
  if(listener >= 0)
    close(listener);
  server->stopping = true;
  return nullptr;
}

extern "C" binldr_server_t *
binldr_create(const binldr_config_t *config) {
  if(!config || !config->port) {
    errno = EINVAL;
    return nullptr;
  }
  binldr_server_t *server = new(std::nothrow) binldr_server;
  if(!server) {
    errno = ENOMEM;
    return nullptr;
  }
  server->config = *config;
  if(!server->config.max_payload_size)
    server->config.max_payload_size = DEFAULT_MAX_PAYLOAD;
  if(!server->config.request_timeout_seconds)
    server->config.request_timeout_seconds = DEFAULT_TIMEOUT;
  if(server->config.max_payload_size < 4
     || server->config.port == ELFLDR_PORT) {
    delete server;
    errno = EINVAL;
    return nullptr;
  }
  return server;
}

extern "C" int
binldr_start(binldr_server_t *server) {
  if(!server) {
    errno = EINVAL;
    return -1;
  }
  std::lock_guard<std::mutex> lock(server->lifecycle);
  if(server->started)
    return 0;
  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if(fd < 0)
    return -1;
  int enabled = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
#ifdef SO_NOSIGPIPE
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_ANY);
  address.sin_port = htons(server->config.port);
  if(bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address))
     || listen(fd, 4)) {
    int saved = errno;
    close(fd);
    errno = saved;
    return -1;
  }
  server->listener = fd;
  server->stopping = false;
  int result = pthread_create(&server->thread, nullptr, server_thread, server);
  if(result) {
    server->listener = -1;
    close(fd);
    errno = result;
    return -1;
  }
  server->started = true;
  return 0;
}

extern "C" int
binldr_stop(binldr_server_t *server) {
  if(!server) {
    errno = EINVAL;
    return -1;
  }
  std::lock_guard<std::mutex> lock(server->lifecycle);
  if(!server->started)
    return 0;
  server->stopping = true;
  int client = server->client;
  if(client >= 0)
    shutdown(client, SHUT_RDWR);
  int listener = server->listener;
  if(listener >= 0)
    shutdown(listener, SHUT_RDWR);
  int result = pthread_join(server->thread, nullptr);
  if(result) {
    errno = result;
    return -1;
  }
  server->started = false;
  return 0;
}

extern "C" int
binldr_is_running(const binldr_server_t *server) {
  return server && server->started && !server->stopping;
}

extern "C" int
binldr_set_sysmodules(binldr_server_t *server,
                      const binldr_sysmodule_t *modules, size_t count) {
  if(!server || (count && !modules) || count > MAX_REQUESTED_SYSMODULES) {
    errno = EINVAL;
    return -1;
  }
  std::lock_guard<std::mutex> lock(server->lifecycle);
  if(server->started) {
    errno = EBUSY;
    return -1;
  }
  for(size_t i = 0; i < count; ++i) {
    if((modules[i].internal != 0 && modules[i].internal != 1)
       || (modules[i].internal && !(modules[i].id & 0x80000000u))
       || (!modules[i].internal
           && (!modules[i].id || modules[i].id > UINT16_MAX))) {
      errno = EINVAL;
      return -1;
    }
  }
  try {
    if(count)
      server->sysmodules.assign(modules, modules + count);
    else
      server->sysmodules.clear();
  } catch(...) {
    errno = ENOMEM;
    return -1;
  }
  return 0;
}

extern "C" void
binldr_destroy(binldr_server_t *server) {
  if(!server)
    return;
  binldr_stop(server);
  delete server;
}
