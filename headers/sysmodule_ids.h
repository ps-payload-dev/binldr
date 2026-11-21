/* Copyright (C) 2026 ItsJokerZz

This program is free software; you can redistribute it and/or modify it
under the terms of the GNU General Public License as published by the
Free Software Foundation; either version 3, or (at your option) any
later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; see the file COPYING. If not, see
<http://www.gnu.org/licenses/>. */

#pragma once

#include <stdint.h>

typedef enum binldr_sysmodule_id {
  BINLDR_SYSMODULE_INTERNAL_SYSTEM_SERVICE = 0x80000010,
  BINLDR_SYSMODULE_INTERNAL_VIDEO_OUT = 0x80000022,
  BINLDR_SYSMODULE_INTERNAL_NET = 0x8000001c,
  BINLDR_SYSMODULE_INTERNAL_USER_SERVICE = 0x80000011,
  BINLDR_SYSMODULE_INTERNAL_PAD = 0x80000024,
  BINLDR_SYSMODULE_INTERNAL_SYS_UTIL = 0x80000026,
  BINLDR_SYSMODULE_INTERNAL_GNM_DRIVER = 0x80000052,
  BINLDR_SYSMODULE_INTERNAL_NET_CTL = 0x80000009,
  BINLDR_SYSMODULE_INTERNAL_HTTP = 0x8000000a,
  BINLDR_SYSMODULE_INTERNAL_SSL = 0x8000000b,
} binldr_sysmodule_id_t;

typedef enum binldr_public_sysmodule_id {
  BINLDR_SYSMODULE_MESSAGE_DIALOG = 0x00a4,
  BINLDR_SYSMODULE_IME_DIALOG = 0x0096,
  BINLDR_SYSMODULE_SAVE_DATA_DIALOG = 0x00a0,
  BINLDR_SYSMODULE_APP_CONTENT = 0x00b4,
  BINLDR_SYSMODULE_NP_TROPHY = 0x00ad,
} binldr_public_sysmodule_id_t;
