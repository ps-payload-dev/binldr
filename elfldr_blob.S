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

#ifndef BINLDR_ELFLDR_BIN
#error "BINLDR_ELFLDR_BIN must name the built upstream elfldr BIN"
#endif

.section .rodata
.p2align 4
.global binldr_elfldr_bin
.global binldr_elfldr_bin_size
binldr_elfldr_bin:
.incbin BINLDR_ELFLDR_BIN
binldr_elfldr_bin_size:
.quad binldr_elfldr_bin_size - binldr_elfldr_bin
