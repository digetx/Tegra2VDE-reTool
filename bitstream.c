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

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>

#include "bitstream.h"

static void bitstream_alloc_more(bitstream_writer *writer)
{
	writer->data_size += 1024;
	writer->data_ptr = realloc(writer->data_ptr, writer->data_size);
	assert(writer->data_ptr != NULL);
}

void bitstream_init(bitstream_writer *writer)
{
	writer->bit_shift = 0;
	writer->data_cnt = 0;
	writer->data_size = 0;
	writer->data_ptr = NULL;
	writer->track_escape_seq = ESCAPE_0;

	bitstream_alloc_more(writer);

	writer->data_ptr[0] = 0;
}

static void bitstream_escape(bitstream_writer *writer)
{
	uint8_t byte = writer->data_ptr[writer->data_cnt];

// 	printf("byte 0x%02X track_escape_seq %d\n",
// 	       byte, writer->track_escape_seq);

	switch (byte) {
	case 0:
		switch (writer->track_escape_seq) {
		case ESCAPE_0:
			writer->track_escape_seq = ESCAPE_1;
			return;
		case ESCAPE_1:
			writer->track_escape_seq = ESCAPE_2;
			return;
		case ESCAPE_2:
			break;
		default:
			assert(0);
		}
	case 1 ... 3:
		if (writer->track_escape_seq == ESCAPE_2) {
			break;
		}
	default:
		goto reset;
	}

// 	printf("escaped! offset %d byte 0x%02X \n", writer->data_cnt, byte);

	writer->data_ptr[writer->data_cnt++] = 0x03;
	writer->data_ptr[writer->data_cnt] = byte;
reset:
	writer->track_escape_seq = ESCAPE_0;
}

static void bitstream_write_byte(bitstream_writer *writer, uint8_t byte,
				 uint8_t bits_nb, int escape)
{
	uint8_t bit_shift = writer->bit_shift;

// 	printf("bitstream_write_byte 0x%02X bits_nb %u bit_shift %u\n",
// 	       byte, bits_nb, bit_shift);

	assert(bits_nb <= 8);

	if (writer->data_cnt + 4 >= writer->data_size) {
		bitstream_alloc_more(writer);
	}

	writer->data_ptr[writer->data_cnt] |= byte >> writer->bit_shift;
	writer->bit_shift = (bit_shift + bits_nb) % 8;

	if (bit_shift + bits_nb >= 8) {
		if (escape) {
			bitstream_escape(writer);
		}

		writer->data_ptr[++writer->data_cnt] = byte << (8 - bit_shift);
	}
}

static void __bitstream_write_ui(bitstream_writer *writer, uint32_t value,
				 int bits_nb, int escape)
{
	uint8_t byte;
	int i;

// 	printf("write_u: value %u  bits_nb %u\n", value, bits_nb);

	assert(bits_nb != 0);
	assert(bits_nb <= 32);

	value <<= 32 - bits_nb;

	for (i = 4; bits_nb > 0; i--) {
		byte = (value >> ((i - 1) * 8)) & 0xFF;

		if (bits_nb - 8 >= 0) {
			bitstream_write_byte(writer, byte, 8, escape);
		} else {
			bitstream_write_byte(writer, byte, bits_nb, escape);
		}

		bits_nb -= 8;
	}
}

void bitstream_write_ui(bitstream_writer *writer, uint32_t value,
			uint8_t bits_nb)
{
	__bitstream_write_ui(writer, value, bits_nb, 1);
}

void bitstream_write_u_ne(bitstream_writer *writer, uint32_t value,
			  uint8_t bits_nb)
{
	__bitstream_write_ui(writer, value, bits_nb, 0);
}

void bitstream_write_ue(bitstream_writer *writer, uint32_t value)
{
	unsigned leading_zeros = 31 - clz(value + 1);

// 	printf("value %u leading_zeros %u\n", value, leading_zeros);

	assert(leading_zeros < 17);

	if (value == 0) {
		bitstream_write_ui(writer, 1, 1);
	} else if (leading_zeros < 16) {
		bitstream_write_ui(writer, value + 1, leading_zeros * 2 + 1);
	} else {
		bitstream_write_ui(writer, 0, leading_zeros);
		bitstream_write_ui(writer, value + 1, leading_zeros + 1);
	}
}

void bitstream_write_se(bitstream_writer *writer, int32_t value)
{
	uint32_t mapped = abs(value) * 2 - (value > 0);

	bitstream_write_ue(writer, mapped);
}
