/*
 * Copyright (C) 2017 Pablo Correa Gómez
 *
 * GPLv3+
 *
 */

#include "fpga_top.h"
#include "small_conv.h"
#include "memory_controller.h"

layer_t OperationID[TOTAL_OPS] = NET;

result_t av_pooling(memory_t output[MAX_INPUT_SIZE][X_PAR_UNROLL])
{
#pragma HLS INLINE
	int acc[OUT_CH],max;
	result_t max_index = 0;

	init_accumulator: for (int ch_out = 0; ch_out < OUT_CH; ch_out++) {
		acc[ch_out] = 0;
	}

	av_pool_row: for (int row = 0; row < OUT_PX; row++) {
		av_pool_ch_out: for (int ch_out = 0; ch_out < OUT_CH; ch_out++) {
		#pragma HLS PIPELINE
			av_pool_col: for (int col = 0; col < OUT_PX; col++) {
					#pragma HLS UNROLL
					acc[ch_out] += output[ch_out * OUT_PX + row][col];
			}
		}
	}

	max = acc[0];
	av_pool_comp: for (int ch_out = 1; ch_out < OUT_CH; ch_out++) {
		#pragma HLS LOOP_TRIPCOUNT min=25 max=25
		if (acc[ch_out] > max) {
			max_index = ch_out;
			max = acc[ch_out];
		}
	}

	return max_index;
}

void convolutions(memory_t mem_i[MAX_INPUT_SIZE>>3][X_PAR_UNROLL],
				memory_t mem_o[MAX_OUTPUT_SIZE >> 3][X_PAR_UNROLL],
				kernel_t weights_ker[TOTAL_WEIGHTS][9],
				kernel_t weights_bi[TOTAL_BIAS])
{
	#pragma HLS INLINE off
	//CONV1
	mem_ctr::config_controller(OperationID[0]);
	hw_conv(OperationID[0],mem_i,mem_o,weights_ker,weights_bi,false);
	mem_ctr::set_offsets_next_lay(false);

	fire_modules: for (int lay = 1; lay < TOTAL_OPS - 1; lay += 3) {

		//Squeeze
		mem_ctr::config_controller(OperationID[lay]);
		hw_conv(OperationID[lay],mem_o,mem_i,weights_ker,weights_bi,true);
		mem_ctr::set_offsets_next_lay(false);

		//Expand 1x1
		mem_ctr::config_controller(OperationID[lay+1]);
		hw_conv(OperationID[lay+1],mem_i,mem_o,weights_ker,weights_bi,true);
		mem_ctr::set_offsets_next_lay(true);

		//Expand 3x3
		mem_ctr::config_controller(OperationID[lay+2]);
		hw_conv(OperationID[lay+2],mem_i,mem_o,weights_ker,weights_bi,true);
		mem_ctr::set_offsets_next_lay(false);
	}

	//Classifier
	mem_ctr::config_controller(OperationID[TOTAL_OPS - 1]);
	hw_conv(OperationID[TOTAL_OPS - 1],mem_o,mem_i,weights_ker,weights_bi,false);

}

result_t fpga_top(data_t image[MAX_INPUT_SIZE],
				bool load,
				kernel_t bias[TOTAL_BIAS],
				kernel_t kernels[TOTAL_WEIGHTS][9])
{
	result_t result;

	//#pragma HLS DATAFLOW
	static kernel_t weights_bi[TOTAL_BIAS];
	static kernel_t weights_ker[TOTAL_WEIGHTS][9];
	memory_t mem_i[MAX_INPUT_SIZE >> 3][X_PAR_UNROLL], mem_o[MAX_OUTPUT_SIZE >> 3][X_PAR_UNROLL];
	#pragma HLS ARRAY_PARTITION variable=weights_ker complete factor=9 dim=2
	#pragma HLS ARRAY_PARTITION variable=mem_i complete factor=8 dim=2
	#pragma HLS ARRAY_PARTITION variable=mem_o complete factor=8 dim=2

	if (load) {
		ld_bias: for (int i = 0; i < TOTAL_BIAS; i++) {
			weights_bi[i] = bias[i];
		}

		ld_kernels: for (int i = 0; i < TOTAL_WEIGHTS; i++) {
			ld_single_ker: for (int j = 0; j < 9; j++) {
				weights_ker[i][j] = kernels[i][j];
			}
		}
		return 0;
	}

	mem_ctr::init_mem_controller(image,mem_i);

	convolutions(mem_i,mem_o,weights_ker,weights_bi);

	result = av_pooling(mem_i);

	return result;
}
