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
<http://www.gnu.org/licenses/>.
*/

#include "binldr.h"

#include "log.h"
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" {
#include "notify.h"
}

static constexpr uint16_t DEFAULT_PORT = 9090;

static void
notify_address(uint16_t port) {
  char address[INET_ADDRSTRLEN] = "127.0.0.1";
  struct ifaddrs *interfaces;

  if(getifaddrs(&interfaces) == 0) {
    for(struct ifaddrs *interface = interfaces; interface;
        interface = interface->ifa_next) {
      if(!interface->ifa_addr || interface->ifa_addr->sa_family != AF_INET
         || !strncmp(interface->ifa_name, "lo", 2))
        continue;
      sockaddr_in *ipv4 = reinterpret_cast<sockaddr_in *>(interface->ifa_addr);
      inet_ntop(AF_INET, &ipv4->sin_addr, address, sizeof(address));
      if(strncmp(address, "0.", 2))
        break;
    }
    freeifaddrs(interfaces);
  }

  notify("Serving BIN loader server on %s:%u", address, port);
  LOG_PRINTF("Serving BIN loader server on %s:%u\n", address, port);
}

int
main(int argc, char **argv) {
  syscall(SYS_thr_set_name, -1, "binldr.elf");
  LOG_PUTS("Bootstrapping binldr.elf...");
  signal(SIGCHLD, SIG_IGN);
  signal(SIGPIPE, SIG_IGN);

  uint16_t port = DEFAULT_PORT;
  int option;
  while((option = getopt(argc, argv, "p:h")) != -1) {
    if(option == 'h') {
      LOG_PUTS("usage: binldr.elf [-p PORT]");
      return 0;
    }
    if(option != 'p') {
      LOG_PUTS("usage: binldr.elf [-p PORT]");
      return 1;
    }
    char *end = nullptr;
    long parsed = strtol(optarg, &end, 10);
    if(!end || *end || parsed < 1 || parsed > 65535 || parsed == 9021) {
      LOG_PUTS("usage: binldr.elf [-p PORT]");
      return 1;
    }
    port = static_cast<uint16_t>(parsed);
  }

  LOG_PRINTF("Socket server was compiled at %s %s\n", __DATE__, __TIME__);
  binldr_config_t config{};
  config.port = port;
  binldr_server_t *server = binldr_create(&config);
  if(!server) {
    LOG_PRINTF("binldr_create: %s\n", strerror(errno));
    return 1;
  }
  if(binldr_start(server)) {
    LOG_PRINTF("binldr_start: %s\n", strerror(errno));
    binldr_destroy(server);
    return 1;
  }

  notify_address(port);

  while(binldr_is_running(server))
    sleep(1);
  binldr_destroy(server);
  return 0;
}
