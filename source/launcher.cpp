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
#include <signal.h>
#include <sys/syscall.h>
#include <unistd.h>

extern "C" {
#include "notify.h"
}

/* The server itself, which elfldr runs as a process of its own. */
__asm__(".section .rodata\n"
        ".global binldr_server_elf\n"
        ".global binldr_server_elf_end\n"
        ".align 16\n"
        "binldr_server_elf:\n"
        ".incbin \"" BINLDR_SERVER_ELF "\"\n"
        "binldr_server_elf_end:\n"
        ".previous\n");
extern "C" const unsigned char binldr_server_elf[];
extern "C" const unsigned char binldr_server_elf_end[];

int
main(void) {
  syscall(SYS_thr_set_name, -1, "binldr launcher");
  signal(SIGPIPE, SIG_IGN);

  LOG_PUTS("Spawning binldr.elf...");
  if(binldr_spawn(binldr_server_elf, binldr_server_elf_end - binldr_server_elf)) {
    notify("Failed to spawn binldr.elf");
    LOG_PUTS("Failed to spawn binldr.elf");
    return 1;
  }

  return 0;
}
