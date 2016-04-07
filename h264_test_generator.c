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
#include <getopt.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#include "bitstream.h"

#define DUMMY_MACROBLOCK		0x27

#define WRITE_UI(f, param, size)	write_ui(f, #param, param, size)
#define WRITE_UE(f, param)		write_ue(f, #param, param)
#define WRITE_SE(f, param)		write_se(f, #param, param)

#define MAX(a, b)	(((a) > (b)) ? (a) : (b))

static bitstream_writer writer;
static const char *h264_out_file_path;
static const char *misc_out_dir;

static int stop_bit = 1;

/* Sequence parameter set (SPS) */
static int SPS_profile_idc = 77;
static int SPS_constraint_set0_flag = 0;
static int SPS_constraint_set1_flag = 0;
static int SPS_constraint_set2_flag = 0;
static int SPS_constraint_set3_flag = 0;
static int SPS_constraint_set4_flag = 0;
static int SPS_constraint_set5_flag = 0;
static int SPS_level_idc = 31;
static int SPS_seq_parameter_set_id = 0;
static int SPS_log2_max_frame_num_minus4 = -1;
static int SPS_pic_order_cnt_type = 2;
static int SPS_log2_max_pic_order_cnt_lsb_minus4 = 0;
static int SPS_delta_pic_order_always_zero_flag = 0;
static int SPS_offset_for_non_ref_pic = 0;
static int SPS_offset_for_top_to_bottom_field = 0;
static int SPS_num_ref_frames_in_pic_order_cnt_cycle = 0;
static int SPS_offset_for_ref_frame = 0;
static int SPS_max_num_ref_frames = 0;
static int SPS_gaps_in_frame_num_value_allowed_flag = 0;
static int SPS_pic_width_in_mbs = 6;
static int SPS_pic_height_in_map_units = 6;
static int SPS_frame_mbs_only_flag = 1;
static int SPS_mb_adaptive_frame_field_flag = 0;
static int SPS_direct_8x8_inference_flag = 1;
static int SPS_frame_cropping_flag = 0;
static int SPS_frame_crop_left_offset = 0;
static int SPS_frame_crop_right_offset = 0;
static int SPS_frame_crop_top_offset = 0;
static int SPS_frame_crop_bottom_offset = 0;
static int SPS_vui_parameters_present_flag = 0;

/* Picture parameter set (PPS) */
static int PPS_pic_parameter_set_id = 0;
static int PPS_seq_parameter_set_id = 0;
static int PPS_entropy_coding_mode_flag = 0;
static int PPS_bottom_field_pic_order_in_frame_present_flag = 1;
static int PPS_num_slice_groups_minus1 = 0;
static int PPS_num_ref_idx_l0_default_active_minus1 = 0;
static int PPS_num_ref_idx_l1_default_active_minus1 = 0;
static int PPS_weighted_pred_flag = 0;
static int PPS_weighted_bipred_idc = 0;
static int PPS_pic_init_qp_minus26 = 4;
static int PPS_pic_init_qs_minus26 = 0;
static int PPS_chroma_qp_index_offset = 3;
static int PPS_deblocking_filter_control_present_flag = 1;
static int PPS_constrained_intra_pred_flag = 0;
static int PPS_redundant_pic_cnt_present_flag = 0;

/* Slice layer without partitioning IDR */
static int SH_first_mb_in_slice = 0;
static int SH_slice_type = 7; // I_ONLY
static int SH_pic_parameter_set_id = 0;
static int SH_frame_num = 0;
static int SH_idr_pic_id = 0;
static int SH_no_output_of_prior_pics_flag = 0;
static int SH_long_term_reference_flag = 0;
static int SH_slice_qp_delta = 7;
static int SH_disable_deblocking_filter_idc = 0;
static int SH_slice_alpha_c0_offset_div2 = 0;
static int SH_slice_beta_offset_div2 = 0;

static char * misc_path(const char *name)
{
	char *fpath = malloc(strlen(misc_out_dir) + strlen(name) + 2);
	assert(fpath != NULL);
	sprintf(fpath, "%s/%s", misc_out_dir, name);
	return fpath;
}

static FILE * open_misc_file(char *name)
{
	char *fpath = misc_path(name);
	FILE *f = fopen(fpath, "a+");
	assert(ferror(f) == 0);
	free(fpath);
	return f;
}

static void write_ui(FILE *f, const char *param, unsigned val, int size)
{
	bitstream_write_ui(&writer, val, size);
	fprintf(f, "%s = %u\n", param, val);
}

static void write_ue(FILE *f, const char *param, unsigned val)
{
	bitstream_write_ue(&writer, val);
	fprintf(f, "%s = %u\n", param, val);
}

static void write_se(FILE *f, const char *param, signed val)
{
	bitstream_write_se(&writer, val);
	fprintf(f, "%s = %d\n", param, val);
}

static void write_bitstream_to_file(const char *path, unsigned data_offset,
				    unsigned data_size)
{
	FILE *fp_out = fopen(path, "w+");
	assert(fp_out != NULL);
	fwrite(writer.data_ptr + data_offset, 1, data_size, fp_out);
	assert(ferror(fp_out) == 0);
}

static void generate_NAL_header(int nal_ref_idc, int nal_unit_type)
{
	bitstream_write_u_ne(&writer, 0, 8 - writer.bit_shift); // byte align
	bitstream_write_u_ne(&writer, 0x000001, 24); // NAL start code
	bitstream_write_u_ne(&writer, 0, 1); // forbidden zero bit = 0
	bitstream_write_u_ne(&writer, nal_ref_idc, 2);
	bitstream_write_u_ne(&writer, nal_unit_type, 5);
}

static void generate_SPS(void)
{
	FILE *f = open_misc_file("SPS.txt");
	uint32_t data_cnt_old = writer.data_cnt;
	int reserved_zero_2bits = 0;
	int i;

	generate_NAL_header(1, 7);

	WRITE_UI(f, SPS_profile_idc, 8);
	WRITE_UI(f, SPS_constraint_set0_flag, 1);
	WRITE_UI(f, SPS_constraint_set1_flag, 1);
	WRITE_UI(f, SPS_constraint_set2_flag, 1);
	WRITE_UI(f, SPS_constraint_set3_flag, 1);
	WRITE_UI(f, SPS_constraint_set4_flag, 1);
	WRITE_UI(f, SPS_constraint_set5_flag, 1);
	WRITE_UI(f, reserved_zero_2bits, 2);
	WRITE_UI(f, SPS_level_idc, 8);
	WRITE_UE(f, SPS_seq_parameter_set_id);
	WRITE_UE(f, SPS_log2_max_frame_num_minus4);
	WRITE_UE(f, SPS_pic_order_cnt_type);

	switch (SPS_pic_order_cnt_type) {
	case 0:
		WRITE_UE(f, SPS_log2_max_pic_order_cnt_lsb_minus4);
		break;
	case 1:
		WRITE_UI(f, SPS_delta_pic_order_always_zero_flag, 1);
		WRITE_SE(f, SPS_offset_for_non_ref_pic);
		WRITE_SE(f, SPS_offset_for_top_to_bottom_field);
		WRITE_UE(f, SPS_num_ref_frames_in_pic_order_cnt_cycle);

		for (i = 0; i < SPS_num_ref_frames_in_pic_order_cnt_cycle; i++) {
			WRITE_SE(f, SPS_offset_for_ref_frame);
		}
		break;
	}

	WRITE_UE(f, SPS_max_num_ref_frames);
	WRITE_UI(f, SPS_gaps_in_frame_num_value_allowed_flag, 1);
	WRITE_UE(f, SPS_pic_width_in_mbs - 1);
	WRITE_UE(f, SPS_pic_height_in_map_units - 1);
	WRITE_UI(f, SPS_frame_mbs_only_flag, 1);

	if (!SPS_frame_mbs_only_flag) {
		WRITE_UI(f, SPS_mb_adaptive_frame_field_flag, 1);
	}

	WRITE_UI(f, SPS_direct_8x8_inference_flag, 1);
	WRITE_UI(f, SPS_frame_cropping_flag, 1);

	if (SPS_frame_cropping_flag) {
		WRITE_UE(f, SPS_frame_crop_left_offset);
		WRITE_UE(f, SPS_frame_crop_right_offset);
		WRITE_UE(f, SPS_frame_crop_top_offset);
		WRITE_UE(f, SPS_frame_crop_bottom_offset);
	}

	WRITE_UI(f, SPS_vui_parameters_present_flag, 1);
	WRITE_UI(f, stop_bit, 1);

	fclose(f);

	write_bitstream_to_file(misc_path("SPS.data"), data_cnt_old,
				writer.data_cnt - data_cnt_old + 1);
}

static void generate_PPS(void)
{
	FILE *f = open_misc_file("PPS.txt");
	uint32_t data_cnt_old = writer.data_cnt;

	generate_NAL_header(1, 8);

	WRITE_UE(f, PPS_pic_parameter_set_id);
	WRITE_UE(f, PPS_seq_parameter_set_id);
	WRITE_UI(f, PPS_entropy_coding_mode_flag, 1);
	WRITE_UI(f, PPS_bottom_field_pic_order_in_frame_present_flag, 1);
	WRITE_UE(f, PPS_num_slice_groups_minus1);
	WRITE_UE(f, PPS_num_ref_idx_l0_default_active_minus1);
	WRITE_UE(f, PPS_num_ref_idx_l1_default_active_minus1);
	WRITE_UI(f, PPS_weighted_pred_flag, 1);
	WRITE_UI(f, PPS_weighted_bipred_idc, 2);
	WRITE_SE(f, PPS_pic_init_qp_minus26);
	WRITE_SE(f, PPS_pic_init_qs_minus26);
	WRITE_SE(f, PPS_chroma_qp_index_offset);
	WRITE_UI(f, PPS_deblocking_filter_control_present_flag, 1);
	WRITE_UI(f, PPS_constrained_intra_pred_flag, 1);
	WRITE_UI(f, PPS_redundant_pic_cnt_present_flag, 1);
	WRITE_UI(f, stop_bit, 1);

	fclose(f);

	write_bitstream_to_file(misc_path("PPS.data"), data_cnt_old,
				writer.data_cnt - data_cnt_old + 1);
}

static void generate_dummy_macroblock(FILE *f)
{
	WRITE_UI(f, DUMMY_MACROBLOCK, 8);
}

static void generate_IDR_slice(void)
{
	FILE *f = open_misc_file("slice.txt");
	int x, y;

	generate_NAL_header(1, 5);

	WRITE_UE(f, SH_first_mb_in_slice);
	WRITE_UE(f, SH_slice_type);
	WRITE_UE(f, SH_pic_parameter_set_id);
	WRITE_UI(f, SH_frame_num++, SPS_log2_max_frame_num_minus4 + 4);
	WRITE_UE(f, SH_idr_pic_id);
	WRITE_UI(f, SH_no_output_of_prior_pics_flag, 1);
	WRITE_UI(f, SH_long_term_reference_flag, 1);
	WRITE_SE(f, SH_slice_qp_delta);

	if (PPS_deblocking_filter_control_present_flag) {
		WRITE_UE(f, SH_disable_deblocking_filter_idc);

		if (SH_disable_deblocking_filter_idc != 1) {
			WRITE_SE(f, SH_slice_alpha_c0_offset_div2);
			WRITE_SE(f, SH_slice_beta_offset_div2);
		}
	}

	for (y = 0; y < SPS_pic_height_in_map_units; y++) {
		for (x = 0; x < SPS_pic_width_in_mbs; x++) {
			generate_dummy_macroblock(f);
		}
	}

	WRITE_UI(f, stop_bit, 1);

	fclose(f);
}

static void generate_h264(void)
{
	int frames_nb = 30;

	if (SPS_log2_max_frame_num_minus4 == -1) {
		SPS_log2_max_frame_num_minus4 = MAX(28 - clz(frames_nb), 0);
	}

	generate_SPS();
	generate_PPS();

	while (frames_nb--) {
		generate_IDR_slice();
	}
}

static void parse_input_params(int argc, char **argv)
{
	int c;

	do {
		static struct option long_options[] =
		{
			{"SPS_profile_idc",				required_argument, &SPS_profile_idc, 0},
			{"SPS_constraint_set0_flag",			required_argument, &SPS_constraint_set0_flag, 0},
			{"SPS_constraint_set1_flag",			required_argument, &SPS_constraint_set1_flag, 0},
			{"SPS_constraint_set2_flag",			required_argument, &SPS_constraint_set2_flag, 0},
			{"SPS_constraint_set3_flag",			required_argument, &SPS_constraint_set3_flag, 0},
			{"SPS_constraint_set4_flag",			required_argument, &SPS_constraint_set4_flag, 0},
			{"SPS_constraint_set5_flag",			required_argument, &SPS_constraint_set5_flag, 0},
			{"SPS_level_idc",				required_argument, &SPS_level_idc, 0},
			{"SPS_seq_parameter_set_id",			required_argument, &SPS_seq_parameter_set_id, 0},
			{"SPS_log2_max_frame_num_minus4",		required_argument, &SPS_log2_max_frame_num_minus4, 0},
			{"SPS_pic_order_cnt_type",			required_argument, &SPS_pic_order_cnt_type, 0},
			{"SPS_log2_max_pic_order_cnt_lsb_minus4 ",	required_argument, &SPS_log2_max_pic_order_cnt_lsb_minus4, 0},
			{"SPS_delta_pic_order_always_zero_flag",	required_argument, &SPS_delta_pic_order_always_zero_flag, 0},
			{"SPS_offset_for_non_ref_pic",			required_argument, &SPS_offset_for_non_ref_pic, 0},
			{"SPS_offset_for_top_to_bottom_field",		required_argument, &SPS_offset_for_top_to_bottom_field, 0},
			{"SPS_num_ref_frames_in_pic_order_cnt_cycle",	required_argument, &SPS_num_ref_frames_in_pic_order_cnt_cycle, 0},
			{"SPS_offset_for_ref_frame",			required_argument, &SPS_offset_for_ref_frame, 0},
			{"SPS_max_num_ref_frames",			required_argument, &SPS_max_num_ref_frames, 0},
			{"SPS_gaps_in_frame_num_value_allowed_flag",	required_argument, &SPS_gaps_in_frame_num_value_allowed_flag, 0},
			{"SPS_pic_width_in_mbs",			required_argument, &SPS_pic_width_in_mbs, 0},
			{"SPS_pic_height_in_map_units",			required_argument, &SPS_pic_height_in_map_units, 0},
			{"SPS_frame_mbs_only_flag",			required_argument, &SPS_frame_mbs_only_flag, 0},
			{"SPS_mb_adaptive_frame_field_flag",		required_argument, &SPS_mb_adaptive_frame_field_flag, 0},
			{"SPS_direct_8x8_inference_flag",		required_argument, &SPS_direct_8x8_inference_flag, 0},
			{"SPS_frame_cropping_flag",			required_argument, &SPS_frame_cropping_flag, 0},
			{"SPS_frame_crop_left_offset",			required_argument, &SPS_frame_crop_left_offset, 0},
			{"SPS_frame_crop_right_offset",			required_argument, &SPS_frame_crop_right_offset, 0},
			{"SPS_frame_crop_top_offset",			required_argument, &SPS_frame_crop_top_offset, 0},
			{"SPS_frame_crop_bottom_offset",		required_argument, &SPS_frame_crop_bottom_offset, 0},
			{"SPS_vui_parameters_present_flag",		required_argument, &SPS_vui_parameters_present_flag, 0},

			{"PPS_pic_parameter_set_id",			required_argument, &PPS_pic_parameter_set_id, 0},
			{"PPS_seq_parameter_set_id",			required_argument, &PPS_seq_parameter_set_id, 0},
			{"PPS_entropy_coding_mode_flag",		required_argument, &PPS_entropy_coding_mode_flag, 0},
			{"PPS_bottom_field_pic_order_in_frame_present_flag",required_argument, &PPS_bottom_field_pic_order_in_frame_present_flag, 0},
			{"PPS_num_slice_groups_minus1",			required_argument, &PPS_num_slice_groups_minus1, 0},
			{"PPS_num_ref_idx_l0_default_active_minus1",	required_argument, &PPS_num_ref_idx_l0_default_active_minus1, 0},
			{"PPS_num_ref_idx_l1_default_active_minus1",	required_argument, &PPS_num_ref_idx_l1_default_active_minus1, 0},
			{"PPS_weighted_pred_flag",			required_argument, &PPS_weighted_pred_flag, 0},
			{"PPS_weighted_bipred_idc",			required_argument, &PPS_weighted_bipred_idc, 0},
			{"PPS_pic_init_qp_minus26",			required_argument, &PPS_pic_init_qp_minus26, 0},
			{"PPS_pic_init_qs_minus26",			required_argument, &PPS_pic_init_qs_minus26, 0},
			{"PPS_chroma_qp_index_offset",			required_argument, &PPS_chroma_qp_index_offset, 0},
			{"PPS_deblocking_filter_control_present_flag",	required_argument, &PPS_deblocking_filter_control_present_flag, 0},
			{"PPS_constrained_intra_pred_flag",		required_argument, &PPS_constrained_intra_pred_flag, 0},
			{"PPS_redundant_pic_cnt_present_flag",		required_argument, &PPS_redundant_pic_cnt_present_flag, 0},

			{"SH_first_mb_in_slice",			required_argument, &SH_first_mb_in_slice, 0},
			{"SH_slice_type",				required_argument, &SH_slice_type, 0},
			{"SH_pic_parameter_set_id",			required_argument, &SH_pic_parameter_set_id, 0},
			{"SH_frame_num",				required_argument, &SH_frame_num, 0},
			{"SH_idr_pic_id",				required_argument, &SH_idr_pic_id, 0},
			{"SH_no_output_of_prior_pics_flag",		required_argument, &SH_no_output_of_prior_pics_flag, 0},
			{"SH_long_term_reference_flag",			required_argument, &SH_long_term_reference_flag, 0},
			{"SH_slice_qp_delta",				required_argument, &SH_slice_qp_delta, 0},
			{"SH_disable_deblocking_filter_idc",		required_argument, &SH_disable_deblocking_filter_idc, 0},
			{"SH_slice_alpha_c0_offset_div2",		required_argument, &SH_slice_alpha_c0_offset_div2, 0},
			{"SH_slice_beta_offset_div2",			required_argument, &SH_slice_beta_offset_div2, 0},
			{ /* Sentinel */ }
		};
		int option_index = 0;

		c = getopt_long(argc, argv, "o:d:", long_options, &option_index);

		switch (c) {
		case 0:
			*long_options[option_index].flag = atoi(optarg);
			break;
		case -1:
			break;
		case 'o':
			h264_out_file_path = optarg;
			break;
		case 'd':
			misc_out_dir = optarg;
			break;
		default:
			abort();
		}
	} while (c != -1);

	if (h264_out_file_path == NULL) {
		fprintf(stderr, "-o generated h264 file path\n");
		exit(EXIT_FAILURE);
	}

	if (misc_out_dir == NULL) {
		fprintf(stderr, "-d misc output directory path\n");
		exit(EXIT_FAILURE);
	}
}

int main(int argc, char **argv)
{
	parse_input_params(argc, argv);

	bitstream_init(&writer);

	generate_h264();

	write_bitstream_to_file(h264_out_file_path, 0, writer.data_cnt + 1);

	return 0;
}
