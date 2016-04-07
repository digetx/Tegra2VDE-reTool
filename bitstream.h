/*
 * Copyright (c) 2016 Dmitry Osipenko <digetx@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef BITSTREAM_H
#define BITSTREAM_H

#include <stdint.h>

#ifndef clz
#define clz	__builtin_clz
#endif

enum {
	ESCAPE_0,
	ESCAPE_1,
	ESCAPE_2,
};

typedef struct bitstream_writer {
	uint8_t *data_ptr;
	uint32_t data_size;
	uint32_t data_cnt;
	uint8_t bit_shift;
	int track_escape_seq;
} bitstream_writer;

void bitstream_init(bitstream_writer *writer);
void bitstream_write_ui(bitstream_writer *writer, uint32_t value,
			uint8_t bits_nb);
void bitstream_write_u_ne(bitstream_writer *writer, uint32_t value,
			  uint8_t bits_nb);
void bitstream_write_ue(bitstream_writer *writer, uint32_t value);
void bitstream_write_se(bitstream_writer *writer, int32_t value);

#endif // BITSTREAM_H
