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
#include <stdarg.h>
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

#define P	0
#define B	1
#define I	2
#define SP	3
#define SI	4

static bitstream_writer writer;
static const char *h264_out_file_path;
static const char *misc_out_dir;

static const int stop_bit = 1;

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
static int SPS_log2_max_pic_order_cnt_lsb_minus4 = -1;
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
static int SPS_direct_8x8_inference_flag = 0;
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
static int PPS_bottom_field_pic_order_in_frame_present_flag = 0;
static int PPS_num_slice_groups_minus1 = 0;
static int PPS_num_ref_idx_l0_default_active_minus1 = 0;
static int PPS_num_ref_idx_l1_default_active_minus1 = 0;
static int PPS_weighted_pred_flag = 0;
static int PPS_weighted_bipred_idc = 0;
static int PPS_pic_init_qp_minus26 = 0;
static int PPS_pic_init_qs_minus26 = 0;
static int PPS_chroma_qp_index_offset = 3;
static int PPS_deblocking_filter_control_present_flag = 1;
static int PPS_constrained_intra_pred_flag = 0;
static int PPS_redundant_pic_cnt_present_flag = 0;
static int PPS_transform_8x8_mode_flag = 0;
static int PPS_second_chroma_qp_index_offset = 0;

struct slice_header {
	int slice_type;
	int first_mb_in_slice;
	int pic_parameter_set_id;
	int frame_num;
	int is_idr;
	int idr_pic_id;
	int field_pic_flag;
	int bottom_field_flag;
	int no_output_of_prior_pics_flag;
	int long_term_reference_flag;
	int adaptive_ref_pic_marking_mode_flag;
	int cabac_init_idc;
	int slice_qp_delta;
	int disable_deblocking_filter_idc;
	int slice_alpha_c0_offset_div2;
	int slice_beta_offset_div2;
	int num_ref_idx_active_override_flag;
	int num_ref_idx_l0_active_minus1;
	int num_ref_idx_l1_active_minus1;
	int ref_pic_list_modification_flag_l0;
	int ref_pic_list_modification_flag_l1;
	int direct_spatial_mv_pred_flag;
	int pic_order_cnt_lsb;
	int macroblocks_nb;
};

static struct slice_header **slice_headers;
static int slices_NB;
static int max_frame_nb;
static int max_pic_order_cnt;

static int REF_IDC = 1;

static char * misc_path(const char *name_fmt, ...)
{
	static char fpath[256];
	char name_formated[64];
	va_list args;

	if (misc_out_dir == NULL) {
		return NULL;
	}

	va_start(args, name_fmt);
	assert(vsnprintf(name_formated, 64, name_fmt, args) > 0);
	va_end(args);

	snprintf(fpath, 256, "%s/%s", misc_out_dir, name_formated);

	return fpath;
}

static FILE * open_file(const char *fpath)
{
	FILE *f;

	if (fpath == NULL) {
		return NULL;
	}

	f = fopen(fpath, "a+");
	if (f == NULL) {
		perror(fpath);
	}

	return f;
}

static void write_ui(FILE *f, const char *param, unsigned val, int size)
{
	bitstream_write_ui(&writer, val, size);

	if (f == NULL) {
		return;
	}

	fprintf(f, "%s = %u\n", param, val);

	if (ferror(f) != 0) {
		perror("");
	}

	assert(ferror(f) == 0);
}

static void write_ue(FILE *f, const char *param, unsigned val)
{
	bitstream_write_ue(&writer, val);

	if (f == NULL) {
		return;
	}

	fprintf(f, "%s = %u\n", param, val);

	if (ferror(f) != 0) {
		perror("");
	}

	assert(ferror(f) == 0);
}

static void write_se(FILE *f, const char *param, signed val)
{
	bitstream_write_se(&writer, val);

	if (f == NULL) {
		return;
	}

	fprintf(f, "%s = %d\n", param, val);

	if (ferror(f) != 0) {
		perror("");
	}

	assert(ferror(f) == 0);
}

static void write_bitstream_to_file(const char *path, unsigned data_offset,
				    unsigned data_size)
{
	FILE *fp_out;

	if (path == NULL) {
		return;
	}

	fp_out = fopen(path, "w+");
	if (fp_out == NULL) {
		perror(path);
	}

	assert(fp_out != NULL);
	fwrite(writer.data_ptr + data_offset, 1, data_size, fp_out);
	assert(ferror(fp_out) == 0);
}

static void generate_NAL_header(int nal_ref_idc, int nal_unit_type)
{
	if (writer.bit_shift != 0) { // byte align
		bitstream_write_u_ne(&writer, 0, 8 - writer.bit_shift);
	}
	bitstream_write_u_ne(&writer, 0x00000001, 32); // NAL start code
	bitstream_write_u_ne(&writer, 0, 1); // forbidden zero bit = 0
	bitstream_write_u_ne(&writer, nal_ref_idc, 2);
	bitstream_write_u_ne(&writer, nal_unit_type, 5);
}

static void generate_SPS(void)
{
	FILE *f = open_file( misc_path("SPS.txt") );
	uint32_t data_cnt_old = writer.data_cnt;
	int reserved_zero_2bits = 0;
	int i;

	generate_NAL_header(REF_IDC, 7);

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

	if (f) {
		fclose(f);
	}

	write_bitstream_to_file(misc_path("SPS.data"), data_cnt_old,
				writer.data_cnt - data_cnt_old + 1);
}

static void generate_PPS(void)
{
	FILE *f = open_file( misc_path("PPS.txt") );
	uint32_t data_cnt_old = writer.data_cnt;

	generate_NAL_header(REF_IDC, 8);

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

	if (PPS_transform_8x8_mode_flag) {
		WRITE_UI(f, PPS_transform_8x8_mode_flag, 1);
		WRITE_UI(f, 0/*PPS_pic_scaling_matrix_present_flag*/, 1);
		WRITE_SE(f, PPS_second_chroma_qp_index_offset);
	}

	WRITE_UI(f, stop_bit, 1);

	if (f) {
		fclose(f);
	}

	write_bitstream_to_file(misc_path("PPS.data"), data_cnt_old,
				writer.data_cnt - data_cnt_old + 1);
}

static void generate_dummy_I_macroblock(FILE *f)
{
	WRITE_UI(f, DUMMY_MACROBLOCK, 8);
}

static void generate_slice(struct slice_header *sh, int slice_id)
{
	FILE *f = open_file( misc_path("slice_%d.txt", slice_id) );
	int pic_height = SPS_pic_height_in_map_units * (2 - SPS_frame_mbs_only_flag);
	uint32_t data_cnt_old = writer.data_cnt;
	int slice_type = sh->slice_type;
	int macroblocks_nb = sh->macroblocks_nb ?: SPS_pic_width_in_mbs * pic_height;

	generate_NAL_header(REF_IDC, sh->is_idr ? 5 : 1);

	WRITE_UE(f, sh->first_mb_in_slice);
	WRITE_UE(f, sh->slice_type);
	WRITE_UE(f, sh->pic_parameter_set_id);
	WRITE_UI(f, sh->frame_num, SPS_log2_max_frame_num_minus4 + 4);

	slice_type %= 5;

	if (sh->is_idr) {
		assert(slice_type == 2);
		WRITE_UE(f, sh->idr_pic_id);
	}

	if (SPS_pic_order_cnt_type == 0) {
		WRITE_UI(f, sh->pic_order_cnt_lsb,
			 SPS_log2_max_pic_order_cnt_lsb_minus4 + 4);
	}

	if (slice_type == P || slice_type == B) {
		if (slice_type == B) {
			WRITE_UI(f, sh->direct_spatial_mv_pred_flag, 1);
		}

		WRITE_UI(f, sh->num_ref_idx_active_override_flag, 1);

		if (sh->num_ref_idx_active_override_flag) {
			WRITE_UE(f, sh->num_ref_idx_l0_active_minus1);

			if (slice_type == B) {
				WRITE_UE(f, sh->num_ref_idx_l1_active_minus1);
			}
		}
	}

	if (slice_type != I && slice_type != SI) {
		WRITE_UI(f, sh->ref_pic_list_modification_flag_l0, 1);

// 		if (ref_pic_list_modification_flag_l0) {
// 			do {
// 				WRITE_UI(f, modification_of_pic_nums_idc, 1);
//
// 				if (modification_of_pic_nums_idc == 0 ||
// 					modification_of_pic_nums_idc == 1)
// 				{
// 					abs_diff_pic_num_minus1
// 				} else if (modification_of_pic_nums_idc == 2) {
// 					long_term_pic_num
// 				}
// 			} while (modification_of_pic_nums_idc != 3);
// 		}

		if (slice_type == B) {
			WRITE_UI(f, sh->ref_pic_list_modification_flag_l1, 1);
		}
	}

	if (!SPS_frame_mbs_only_flag) {
		WRITE_UI(f, sh->field_pic_flag, 1);

		if (sh->field_pic_flag) {
			WRITE_UI(f, sh->bottom_field_flag, 1);
		}
	}

	if (REF_IDC != 0) {
		if (sh->is_idr) {
			WRITE_UI(f, sh->no_output_of_prior_pics_flag, 1);
			WRITE_UI(f, sh->long_term_reference_flag, 1);
		} else {
			WRITE_UI(f, sh->adaptive_ref_pic_marking_mode_flag, 1);
		}
	}

	if (slice_type != I && slice_type != SI && PPS_entropy_coding_mode_flag) {
		WRITE_UE(f, sh->cabac_init_idc);
	}

	WRITE_SE(f, sh->slice_qp_delta);

	if (PPS_deblocking_filter_control_present_flag) {
		WRITE_UE(f, sh->disable_deblocking_filter_idc);

		if (sh->disable_deblocking_filter_idc != 1) {
			WRITE_SE(f, sh->slice_alpha_c0_offset_div2);
			WRITE_SE(f, sh->slice_beta_offset_div2);
		}
	}

	switch (slice_type) {
	case I:
		while (macroblocks_nb--) {
			generate_dummy_I_macroblock(f);
		}
		break;
	case P:
	case B:
		WRITE_UE(f, macroblocks_nb);
		break;
	default:
		assert(0);
	}

	WRITE_UI(f, stop_bit, 1);

	if (f) {
		fclose(f);
	}

	write_bitstream_to_file(misc_path("slice_%d.data", slice_id),
			data_cnt_old, writer.data_cnt - data_cnt_old + 1);
}

static void generate_h264(void)
{
	int i;

	if (SPS_log2_max_frame_num_minus4 == -1) {
		SPS_log2_max_frame_num_minus4 = MAX(28 - clz(max_frame_nb), 0);
	}

	if (SPS_log2_max_pic_order_cnt_lsb_minus4 == -1) {
		SPS_log2_max_pic_order_cnt_lsb_minus4 = MAX(28 - clz(max_pic_order_cnt), 0);
	}

	generate_SPS();
	generate_PPS();

	for (i = 0; i < slices_NB; i++) {
		generate_slice(slice_headers[i], i);
	}

	generate_NAL_header(REF_IDC, 11); // End of stream
}

static void parse_sh_params(void)
{
	struct slice_header *sh = calloc(1, sizeof(*sh));

	enum {
		SLICE_TYPE,
		FIRST_MB_IN_SLICE,
		PIC_PARAMETER_SET_ID,
		FRAME_NUM,
		IS_IDR,
		IDR_PIC_ID,
		FIELD_PIC_FLAG,
		BOTTOM_FIELD_FLAG,
		NO_OUTPUT_OF_PRIOR_PICS_FLAG,
		LONG_TERM_REFERENCE_FLAG,
		CABAC_INIT_IDC,
		SLICE_QP_DELTA,
		DISABLE_DEBLOCKING_FILTER_IDC,
		SLICE_ALPHA_C0_OFFSET_DIV2,
		SLICE_BETA_OFFSET_DIV2,
		NUM_REF_IDX_ACTIVE_OVERRIDE_FLAG,
		NUM_REF_IDX_L0_ACTIVE_MINUS1,
		NUM_REF_IDX_L1_ACTIVE_MINUS1,
		DIRECT_SPATIAL_MV_PRED_FLAG,
		PIC_ORDER_CNT_LSB,
		MACROBLOCKS_NB,
		SENTINEL,
	};

	char *const params[] = {
		[SLICE_TYPE]			= "slice_type",
		[FIRST_MB_IN_SLICE]		= "first_mb_in_slice",
		[PIC_PARAMETER_SET_ID]		= "pic_parameter_set_id",
		[FRAME_NUM]			= "frame_num",
		[IS_IDR]			= "is_idr",
		[IDR_PIC_ID]			= "idr_pic_id",
		[FIELD_PIC_FLAG]		= "field_pic_flag",
		[BOTTOM_FIELD_FLAG]		= "bottom_field_flag",
		[NO_OUTPUT_OF_PRIOR_PICS_FLAG]	= "no_output_of_prior_pics_flag",
		[LONG_TERM_REFERENCE_FLAG]	= "long_term_reference_flag",
		[CABAC_INIT_IDC]		= "cabac_init_idc",
		[SLICE_QP_DELTA]		= "slice_qp_delta",
		[DISABLE_DEBLOCKING_FILTER_IDC]	= "disable_deblocking_filter_idc",
		[SLICE_ALPHA_C0_OFFSET_DIV2]	= "slice_alpha_c0_offset_div2",
		[SLICE_BETA_OFFSET_DIV2]	= "slice_beta_offset_div2",
		[NUM_REF_IDX_ACTIVE_OVERRIDE_FLAG] = "num_ref_idx_active_override_flag",
		[NUM_REF_IDX_L0_ACTIVE_MINUS1]	= "num_ref_idx_l0_active_minus1",
		[NUM_REF_IDX_L1_ACTIVE_MINUS1]	= "num_ref_idx_l1_active_minus1",
		[DIRECT_SPATIAL_MV_PRED_FLAG]	= "direct_spatial_mv_pred_flag",
		[PIC_ORDER_CNT_LSB]		= "pic_order_cnt_lsb",
		[MACROBLOCKS_NB]		= "macroblocks_nb",
		[SENTINEL]			= NULL,
	};
	char *subopts = optarg, *value;

	assert(sh != NULL);

	while (*subopts != '\0') {
		int param_id = getsubopt(&subopts, params, &value);

		if (param_id < 0) {
			printf ("Unknown suboption '%s'\n", value);
			continue;
		}

		assert(value != NULL);

		switch (param_id) {
		case SLICE_TYPE:
			sh->slice_type = atoi(value);
			break;
		case FIRST_MB_IN_SLICE:
			sh->first_mb_in_slice = atoi(value);
			break;
		case PIC_PARAMETER_SET_ID:
			sh->pic_parameter_set_id = atoi(value);
			break;
		case FRAME_NUM:
			sh->frame_num = atoi(value);
			break;
		case IS_IDR:
			sh->is_idr = atoi(value);
			break;
		case IDR_PIC_ID:
			sh->idr_pic_id = atoi(value);
			break;
		case FIELD_PIC_FLAG:
			sh->field_pic_flag = atoi(value);
			assert(sh->field_pic_flag <= 1);
			assert(sh->field_pic_flag >= 0);
			break;
		case BOTTOM_FIELD_FLAG:
			sh->bottom_field_flag = atoi(value);
			assert(sh->bottom_field_flag <= 1);
			assert(sh->bottom_field_flag >= 0);
			break;
		case NO_OUTPUT_OF_PRIOR_PICS_FLAG:
			sh->no_output_of_prior_pics_flag = atoi(value);
			assert(sh->no_output_of_prior_pics_flag <= 1);
			assert(sh->no_output_of_prior_pics_flag >= 0);
			break;
		case LONG_TERM_REFERENCE_FLAG:
			sh->long_term_reference_flag = atoi(value);
			assert(sh->long_term_reference_flag <= 1);
			assert(sh->long_term_reference_flag >= 0);
			break;
		case CABAC_INIT_IDC:
			sh->cabac_init_idc = atoi(value);
			break;
		case SLICE_QP_DELTA:
			sh->slice_qp_delta = atoi(value);
			break;
		case DISABLE_DEBLOCKING_FILTER_IDC:
			sh->disable_deblocking_filter_idc = atoi(value);
			break;
		case SLICE_ALPHA_C0_OFFSET_DIV2:
			sh->slice_alpha_c0_offset_div2 = atoi(value);
			break;
		case SLICE_BETA_OFFSET_DIV2:
			sh->slice_beta_offset_div2 = atoi(value);
			break;
		case NUM_REF_IDX_ACTIVE_OVERRIDE_FLAG:
			sh->num_ref_idx_active_override_flag = atoi(value);
			assert(sh->num_ref_idx_active_override_flag <= 1);
			assert(sh->num_ref_idx_active_override_flag >= 0);
			break;
		case NUM_REF_IDX_L0_ACTIVE_MINUS1:
			sh->num_ref_idx_l0_active_minus1 = atoi(value);
			break;
		case NUM_REF_IDX_L1_ACTIVE_MINUS1:
			sh->num_ref_idx_l1_active_minus1 = atoi(value);
			break;
		case DIRECT_SPATIAL_MV_PRED_FLAG:
			sh->direct_spatial_mv_pred_flag = atoi(value);
			assert(sh->direct_spatial_mv_pred_flag <= 1);
			assert(sh->direct_spatial_mv_pred_flag >= 0);
			break;
		case PIC_ORDER_CNT_LSB:
			sh->pic_order_cnt_lsb = atoi(value);
			break;
		case MACROBLOCKS_NB:
			sh->macroblocks_nb = atoi(value);
			break;
		}
	}

	slice_headers = realloc(slice_headers, ++slices_NB * sizeof(void *));
	assert(slice_headers != NULL);

	slice_headers[slices_NB - 1] = sh;

	max_frame_nb = MAX(max_frame_nb, sh->frame_num);
	max_pic_order_cnt = MAX(max_pic_order_cnt, sh->pic_order_cnt_lsb);
}

static void parse_input_params(int argc, char **argv)
{
	int c;

	do {
		struct option long_options[] =
		{
			{"slice",					required_argument, 0, 0},
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
			{"PPS_transform_8x8_mode_flag",			required_argument, &PPS_transform_8x8_mode_flag, 0},
			{"PPS_second_chroma_qp_index_offset",		required_argument, &PPS_second_chroma_qp_index_offset, 0},

			{"REF_IDC",					required_argument, &REF_IDC, 0},
			{ /* Sentinel */ }
		};
		int option_index = 0;

		c = getopt_long(argc, argv, "o:d:", long_options, &option_index);

		switch (c) {
		case 0:
			if (option_index == 0) {
				parse_sh_params();
			} else {
				*long_options[option_index].flag = atoi(optarg);
			}
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
		fprintf(stderr, "-d misc output directory path [optional]\n");
	}
}

int main(int argc, char **argv)
{
	parse_input_params(argc, argv);

	bitstream_init(&writer);

	generate_h264();

	write_bitstream_to_file(h264_out_file_path, 0, writer.data_cnt + 1);

	printf("H.264 bitstream generation completed!\n");

	return 0;
}
