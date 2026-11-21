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

#include <elf.h>
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

#ifndef MAP_ANONYMOUS
#define MAP_ANONYMOUS MAP_ANON
#endif

#define BINLDR_PAYLOAD_MAX_SIZE (32u * 1024u * 1024u)
#define BINLDR_MAX_REQUESTED_SYSMODULES 32

extern int sceSysmoduleLoadModuleInternal(uint32_t module_id);
extern int sceSysmoduleLoadModule(uint32_t module_id);

typedef struct binldr_module_request {
  uint8_t internal;
  uint32_t id;
} binldr_module_request_t;

typedef struct binldr_payload_launch {
  void *entry;
  const binldr_module_request_t *modules;
  size_t module_count;
} binldr_payload_launch_t;

static int
read_exact(int fd, void *buffer, size_t size) {
  uint8_t *bytes = buffer;
  size_t done = 0;
  while(done < size) {
    ssize_t count = read(fd, bytes + done, size - done);
    if(count < 0 && errno == EINTR)
      continue;
    if(count <= 0)
      return -1;
    done += (size_t)count;
  }
  return 0;
}

static int
read_module_requests(binldr_module_request_t *modules, size_t *count) {
  static const char magic[] = "BLDRMOD1";
  char received[sizeof(magic) - 1];
  ssize_t first;
  do {
    first = read(STDIN_FILENO, received, 1);
  } while(first < 0 && errno == EINTR);
  if(first == 0) {
    *count = 0;
    return 0;
  }
  if(first != 1
     || read_exact(STDIN_FILENO, received + 1, sizeof(received) - 1))
    return -1;
  if(memcmp(received, magic, sizeof(received)))
    return -1;

  uint16_t requested;
  if(read_exact(STDIN_FILENO, &requested, sizeof(requested))
     || requested > BINLDR_MAX_REQUESTED_SYSMODULES)
    return -1;
  for(size_t i = 0; i < requested; ++i) {
    if(read_exact(STDIN_FILENO, &modules[i].internal,
                  sizeof(modules[i].internal))
       || read_exact(STDIN_FILENO, &modules[i].id, sizeof(modules[i].id)))
      return -1;
    if((modules[i].internal != 0 && modules[i].internal != 1)
       || (modules[i].internal && !(modules[i].id & 0x80000000u))
       || (!modules[i].internal
           && (!modules[i].id || modules[i].id > UINT16_MAX)))
      return -1;
  }
  *count = requested;
  return 0;
}

static int
module_is_requested(const binldr_module_request_t *modules, size_t count,
                    uint32_t id) {
  for(size_t i = 0; i < count; ++i)
    if(modules[i].internal && modules[i].id == id)
      return 1;
  return 0;
}

static void
load_payload_modules(const binldr_module_request_t *modules, size_t count) {
  static const uint32_t common[] = {
    SYSMODULE_INTERNAL_SYSTEM_SERVICE,
    SYSMODULE_INTERNAL_VIDEO_OUT,
    SYSMODULE_INTERNAL_NET,
    SYSMODULE_INTERNAL_USER_SERVICE,
    SYSMODULE_INTERNAL_PAD,
    SYSMODULE_INTERNAL_SYS_UTIL,
    SYSMODULE_INTERNAL_GNM_DRIVER,
  };
  static const uint32_t optional[] = {
    SYSMODULE_INTERNAL_AUDIO_OUT,
    SYSMODULE_INTERNAL_NET_CTL,
    SYSMODULE_INTERNAL_HTTP,
    SYSMODULE_INTERNAL_SSL,
  };

  for(size_t i = 0; i < sizeof(common) / sizeof(common[0]); ++i)
    if(!module_is_requested(modules, count, common[i]))
      sceSysmoduleLoadModuleInternal(common[i]);
  for(size_t i = 0; i < sizeof(optional) / sizeof(optional[0]); ++i)
    if(!module_is_requested(modules, count, optional[i]))
      sceSysmoduleLoadModuleInternal(optional[i]);
}

static int
load_requested_modules(const binldr_module_request_t *modules, size_t count) {
  for(size_t i = 0; i < count; ++i) {
    int duplicate = 0;
    for(size_t previous = 0; previous < i; ++previous)
      if(modules[i].internal == modules[previous].internal
         && modules[i].id == modules[previous].id) {
        duplicate = 1;
        break;
      }
    if(duplicate)
      continue;
    int result = modules[i].internal
                     ? sceSysmoduleLoadModuleInternal(modules[i].id)
                     : sceSysmoduleLoadModule(modules[i].id);
    if(result)
      return result;
  }
  return 0;
}

static void *
run_payload(void *argument) {
  const binldr_payload_launch_t *launch = argument;
  load_payload_modules(launch->modules, launch->module_count);
  if(load_requested_modules(launch->modules, launch->module_count))
    return (void *)1;
  static const char ready[] = "PAYLOAD_READY\n";
  if(write(STDOUT_FILENO, ready, sizeof(ready) - 1) != sizeof(ready) - 1)
    return (void *)1;
  return ((void *(*)(void *))launch->entry)(0);
}

static void *
map_legacy_elf(const uint8_t *file, size_t file_size, size_t page_size,
               size_t *mapped_size, size_t *entry_offset) {
  Elf64_Ehdr header;
  if(file_size < sizeof(header))
    return MAP_FAILED;
  memcpy(&header, file, sizeof(header));
  if(header.e_ident[EI_CLASS] != ELFCLASS64 || header.e_machine != EM_X86_64
     || header.e_phentsize != sizeof(Elf64_Phdr) || !header.e_phnum
     || header.e_phnum > 128 || header.e_phoff > file_size
     || (file_size - header.e_phoff) / sizeof(Elf64_Phdr) < header.e_phnum)
    return MAP_FAILED;

  uint64_t low = UINT64_MAX, high = 0;
  for(unsigned i = 0; i < header.e_phnum; ++i) {
    Elf64_Phdr segment;
    memcpy(&segment, file + header.e_phoff + i * sizeof(segment),
           sizeof(segment));
    if(segment.p_type != PT_LOAD || !segment.p_memsz)
      continue;
    if(segment.p_filesz > segment.p_memsz || segment.p_offset > file_size
       || file_size - segment.p_offset < segment.p_filesz
       || segment.p_vaddr > UINT64_MAX - segment.p_memsz)
      return MAP_FAILED;
    if(segment.p_vaddr < low)
      low = segment.p_vaddr;
    if(segment.p_vaddr + segment.p_memsz > high)
      high = segment.p_vaddr + segment.p_memsz;
  }
  if(low == UINT64_MAX || high <= low || high - low > BINLDR_PAYLOAD_MAX_SIZE
     || header.e_entry < low || header.e_entry >= high)
    return MAP_FAILED;

  size_t extent = (size_t)(high - low);
  *mapped_size = ((extent + page_size - 1) / page_size) * page_size;
  void *image = mmap(NULL, *mapped_size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | 0x8000, -1, 0);
  if(image == MAP_FAILED)
    return MAP_FAILED;
  for(unsigned i = 0; i < header.e_phnum; ++i) {
    Elf64_Phdr segment;
    memcpy(&segment, file + header.e_phoff + i * sizeof(segment),
           sizeof(segment));
    if(segment.p_type == PT_LOAD && segment.p_filesz)
      memcpy((uint8_t *)image + (segment.p_vaddr - low),
             file + segment.p_offset, segment.p_filesz);
  }
  *entry_offset = (size_t)(header.e_entry - low);
  return image;
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

  binldr_module_request_t requested_modules[BINLDR_MAX_REQUESTED_SYSMODULES];
  size_t requested_module_count = 0;
  if(read_module_requests(requested_modules, &requested_module_count))
    return 1;

  static const char prefix[] = "/system_tmp/binldr-payload-";
  if(path[0] != '/' || strncmp(path, prefix, sizeof(prefix) - 1))
    return 1;

  int fd = open(path, O_RDONLY);
  if(fd < 0)
    return 1;
  unlink(path);
  struct stat st;
  if(fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0
     || (uint64_t)st.st_size > BINLDR_PAYLOAD_MAX_SIZE) {
    close(fd);
    return 1;
  }

  long page_size = sysconf(_SC_PAGESIZE);
  if(page_size <= 0) {
    close(fd);
    return 1;
  }
  size_t size = ((size_t)st.st_size + (size_t)page_size - 1)
                & ~((size_t)page_size - 1);
  void *image = mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_PRIVATE | MAP_ANONYMOUS | 0x8000, -1, 0);
  if(image == MAP_FAILED) {
    close(fd);
    return 1;
  }

  size_t done = 0;
  while(done < (size_t)st.st_size) {
    ssize_t count
        = read(fd, (uint8_t *)image + done, (size_t)st.st_size - done);
    if(count <= 0) {
      close(fd);
      munmap(image, size);
      return 1;
    }
    done += (size_t)count;
  }
  close(fd);
  unlink(path);

  void *entry = image;
  size_t entry_offset = 0, legacy_size = 0;
  if(done >= SELFMAG && !memcmp(image, ELFMAG, SELFMAG)) {
    void *legacy
        = map_legacy_elf((const uint8_t *)image, done, (size_t)page_size,
                         &legacy_size, &entry_offset);
    munmap(image, size);
    if(legacy == MAP_FAILED)
      return 1;
    image = legacy;
    size = legacy_size;
    entry = (uint8_t *)image + entry_offset;
  }

  pthread_t thread;
  binldr_payload_launch_t launch
      = { entry, requested_modules, requested_module_count };
  int result = pthread_create(&thread, NULL, run_payload, &launch);
  if(result) {
    munmap(image, size);
    return 1;
  }
  result = pthread_join(thread, NULL);
  munmap(image, size);
  return result ? 1 : 0;
}
