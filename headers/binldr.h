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

#pragma once

#include <stddef.h>
#include <stdint.h>

#include "sysmodule_ids.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct binldr_server binldr_server_t;

typedef int (*binldr_binary_handler_t)(const uint8_t *payload, size_t size,
                                       char *message, size_t message_size,
                                       void *user_data);

typedef struct binldr_sysmodule {
  uint32_t id;
  int internal;
} binldr_sysmodule_t;

typedef struct binldr_config {
  uint16_t port;
  size_t max_payload_size;
  unsigned request_timeout_seconds;
  binldr_binary_handler_t handle_binary;
  void *user_data;
} binldr_config_t;

binldr_server_t *binldr_create(const binldr_config_t *config);
int binldr_start(binldr_server_t *server);
int binldr_stop(binldr_server_t *server);
int binldr_is_running(const binldr_server_t *server);
int binldr_set_sysmodules(binldr_server_t *server,
                          const binldr_sysmodule_t *modules, size_t count);
void binldr_destroy(binldr_server_t *server);

#ifdef __cplusplus
}
#endif
