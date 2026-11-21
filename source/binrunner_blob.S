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

#ifndef BINLDR_BINRUNNER_ELF
#error "BINLDR_BINRUNNER_ELF must name the built BIN runner ELF"
#endif

.section .rodata
.p2align 4
.global binldr_binrunner_elf
.global binldr_binrunner_elf_size
binldr_binrunner_elf:
.incbin BINLDR_BINRUNNER_ELF
binldr_binrunner_elf_size:
.quad binldr_binrunner_elf_size - binldr_binrunner_elf
