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

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sysmodule_ids.h"

extern int sceSysmoduleLoadModuleInternal(uint32_t module_id);

static void
load_payload_modules(void) {
  static const uint32_t required[] = {
    BINLDR_SYSMODULE_INTERNAL_SYSTEM_SERVICE,
    BINLDR_SYSMODULE_INTERNAL_VIDEO_OUT,
    BINLDR_SYSMODULE_INTERNAL_NET,
    BINLDR_SYSMODULE_INTERNAL_USER_SERVICE,
    BINLDR_SYSMODULE_INTERNAL_PAD,
    BINLDR_SYSMODULE_INTERNAL_SYS_UTIL,
    BINLDR_SYSMODULE_INTERNAL_GNM_DRIVER,
  };
  static const uint32_t optional[] = {
    BINLDR_SYSMODULE_INTERNAL_NET_CTL,
    BINLDR_SYSMODULE_INTERNAL_HTTP,
    BINLDR_SYSMODULE_INTERNAL_SSL,
  };

  for(size_t i = 0; i < sizeof(required) / sizeof(required[0]); ++i)
    sceSysmoduleLoadModuleInternal(required[i]);
  for(size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i)
    sceSysmoduleLoadModuleInternal(optional[i]);
}

static void *
run_payload(void *entry) {
  load_payload_modules();
  static const char ready[] = "PAYLOAD_READY\n";
  if(write(STDOUT_FILENO, ready, sizeof(ready) - 1) != sizeof(ready) - 1)
    return (void *)1;
  return ((void *(*)(void *))entry)(0);
}

int
main(void) {
  char path[256];
  size_t length = 0;
  while(length < sizeof(path) - 1) {
    char c;
    if(read(STDIN_FILENO, &c, 1) != 1)
      return 1;
    if(c == '\n')
      break;
    path[length++] = c;
  }
  if(!length || length == sizeof(path) - 1)
    return 1;
  path[length] = 0;

  int fd = open(path, O_RDONLY);
  if(fd < 0)
    return 1;
  struct stat st;
  if(fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0
     || (uint64_t)st.st_size > 32u * 1024u * 1024u) {
    close(fd);
    unlink(path);
    return 1;
  }

  long page_size = sysconf(_SC_PAGESIZE);
  if(page_size <= 0) {
    close(fd);
    unlink(path);
    return 1;
  }
  size_t size = ((size_t)st.st_size + (size_t)page_size - 1)
                & ~((size_t)page_size - 1);
  void *image = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if(image == MAP_FAILED) {
    close(fd);
    unlink(path);
    return 1;
  }

  size_t done = 0;
  while(done < (size_t)st.st_size) {
    ssize_t count
        = read(fd, (uint8_t *)image + done, (size_t)st.st_size - done);
    if(count <= 0) {
      close(fd);
      unlink(path);
      munmap(image, size);
      return 1;
    }
    done += (size_t)count;
  }
  close(fd);
  unlink(path);

  pthread_t thread;
  int result = pthread_create(&thread, NULL, run_payload, image);
  if(result) {
    munmap(image, size);
    return 1;
  }
  result = pthread_join(thread, NULL);
  munmap(image, size);
  return result ? 1 : 0;
}
