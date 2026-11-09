/****************************************************************
 * Copyright (c) 2020~2021, 18-643 Course Staff, CMU
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * The views and conclusions contained in the software and
 * documentation are those of the authors and should not be
 * interpreted as representing official policies, either expressed or
 * implied, of the FreeBSD Project.
 ****************************************************************/

/*
 * CMU 18643 Fall 2021 Lab Exercise
 *
 * You can edit this file
 */

/****************************************************************
 * Blocked convolution layer implementation
 * based on Figure 5 & Figure 9:
 *    C. Zhang, et al., "Optimizing FPGA-based Accelerator
 *    Design for Deep Convolutional Neural Networks," FPGA, 2015.
 ****************************************************************/

#include "util643.h"
#include "instance643.h"
#include "kernel643.h"

void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                        cnndata_t BufO[TM][TR][TC],
                        cnndata_t BufW[TM][TN][K_WTS][K_WTS]);

#ifdef __VITIS_CL__ // for lab 3
extern "C" {
void krnl_cnn(const cnndata_t* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size) {
#pragma HLS INTERFACE m_axi port=input   offset=slave bundle=gmem0 max_read_burst_length=256
#pragma HLS INTERFACE m_axi port=weights offset=slave bundle=gmem1 max_read_burst_length=256
#pragma HLS INTERFACE m_axi port=output  offset=slave bundle=gmem0 max_write_burst_length=256
#pragma HLS INTERFACE s_axilite port=input      bundle=control
#pragma HLS INTERFACE s_axilite port=weights    bundle=control
#pragma HLS INTERFACE s_axilite port=output     bundle=control
#pragma HLS INTERFACE s_axilite port=batch_size bundle=control
#pragma HLS INTERFACE s_axilite port=return     bundle=control
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
              const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
              cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
#endif

  // On-chip buffers sized for B_TILE=5 images to maximize input, output, and weight reuse
  cnndata_t BufI[B_TILE][TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1];
#pragma HLS ARRAY_PARTITION variable=BufI complete dim=2
#pragma HLS BIND_STORAGE variable=BufI type=ram_s2p impl=bram

  cnndata_t BufO[B_TILE][M_OFM / TM][TM][TR][TC];
#pragma HLS ARRAY_PARTITION variable=BufO complete dim=3
#pragma HLS BIND_STORAGE variable=BufO type=ram_s2p impl=bram

  cnndata_t BufW[TM][TN][K_WTS][K_WTS];
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=2
#pragma HLS BIND_STORAGE variable=BufW type=ram_1p impl=lutram

  const index_t R_IN_TILE = TR * S_WTS + K_WTS - 1;
  const index_t C_IN_TILE = TC * S_WTS + K_WTS - 1;
  const index_t NUM_TO_TILES = M_OFM / TM;
  const index_t NUM_TI_TILES = N_IFM / TN;

Batch_Tile:
  for (index_t iter_b = 0; iter_b < batch_size; iter_b += B_TILE) {
    index_t b_lim = (iter_b + B_TILE <= batch_size) ? (index_t)B_TILE : (index_t)(batch_size - iter_b);

    // 1. Zero on-chip BufO across all output channels for the current batch tile
  Zero_BufO_b:
    for (index_t b = 0; b < b_lim; b++) {
    Zero_BufO_to:
      for (index_t to_t = 0; to_t < NUM_TO_TILES; to_t++) {
      Zero_BufO_r:
        for (index_t irr = 0; irr < TR; irr++) {
        Zero_BufO_c:
          for (index_t icc = 0; icc < TC; icc++) {
#pragma HLS PIPELINE II=1
            for (index_t ioo = 0; ioo < TM; ioo++) {
#pragma HLS UNROLL
              BufO[b][to_t][ioo][irr][icc] = 0;
            }
          }
        }
      }
    }

    // 2. Sweep input channel tiles on the outside so each input slice is loaded only ONCE from DRAM
  Tile_Ti:
    for (index_t ti_t = 0; ti_t < NUM_TI_TILES; ti_t++) {
      index_t ti = ti_t * TN;

      // Load active input feature map slice for all B_TILE images in contiguous DRAM bursts
    Load_BufI_b:
      for (index_t b = 0; b < b_lim; b++) {
      Load_BufI_ti:
        for (index_t iii = 0; iii < TN; iii++) {
        Load_BufI_r:
          for (index_t irr = 0; irr < R_IN_TILE; irr++) {
          Load_BufI_c:
            for (index_t icc = 0; icc < C_IN_TILE; icc++) {
#pragma HLS PIPELINE II=1
              index_t iter = iter_b + b;
              index_t tii = (ti & ~(index_t)(TN - 1)) | iii;
              BufI[b][iii][irr][icc] = ARRAYi(input, iter, tii, irr, icc, 0, N_IFM, R_IFM, C_IFM);
            }
          }
        }
      }

      // 3. Sweep output channel tiles; load each weight tile ONCE and reuse across all B_TILE images
    Tile_To:
      for (index_t to_t = 0; to_t < NUM_TO_TILES; to_t++) {
        index_t to = to_t * TM;

      Load_BufW_o:
        for (index_t ioo = 0; ioo < TM; ioo++) {
        Load_BufW_i:
          for (index_t iii = 0; iii < TN; iii++) {
          Load_BufW_r:
            for (index_t irr = 0; irr < K_WTS; irr++) {
            Load_BufW_c:
              for (index_t icc = 0; icc < K_WTS; icc++) {
#pragma HLS PIPELINE II=1
                index_t too = (to & ~(index_t)(TM - 1)) | ioo;
                index_t tii = (ti & ~(index_t)(TN - 1)) | iii;
                BufW[ioo][iii][irr][icc] = ARRAYw(weights, too, tii, irr, icc, M_OFM, N_IFM, K_WTS, K_WTS);
              }
            }
          }
        }

      Compute_Batch:
        for (index_t b = 0; b < b_lim; b++) {
          cnn_blocked_kernel(BufI[b], BufO[b][to_t], BufW);
        }
      }
    }

    // 4. Unload completed output feature maps for the B_TILE images to DRAM in contiguous bursts
  Unload_BufO_b:
    for (index_t b = 0; b < b_lim; b++) {
    Unload_BufO_to:
      for (index_t to_t = 0; to_t < NUM_TO_TILES; to_t++) {
      Unload_BufO_o:
        for (index_t ioo = 0; ioo < TM; ioo++) {
        Unload_BufO_r:
          for (index_t irr = 0; irr < TR; irr++) {
          Unload_BufO_c:
            for (index_t icc = 0; icc < TC; icc++) {
#pragma HLS PIPELINE II=1
              index_t iter = iter_b + b;
              index_t too = ((to_t * TM) & ~(index_t)(TM - 1)) | ioo;
              ARRAYo(output, iter, too, irr, icc, 0, M_OFM, R_OFM, C_OFM) = BufO[b][to_t][ioo][irr][icc];
            }
          }
        }
      }
    }
  }
}

#ifdef __VITIS_CL__ // for lab 3
} // extern
#endif

void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                        cnndata_t BufO[TM][TR][TC],
                        cnndata_t BufW[TM][TN][K_WTS][K_WTS]) {
#pragma HLS INLINE
#pragma HLS ARRAY_PARTITION variable=BufI complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufO complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=2
#pragma HLS DEPENDENCE variable=BufO inter false

  index_t to_b, ti_b, row_b, col_b;
  index_t i, j;

Krow:
  for (i = 0; i < K_WTS; i++) {
  Kcol:
    for (j = 0; j < K_WTS; j++) {
    Row:
      for (row_b = 0; row_b < TR; row_b++) {
      Col:
        for (col_b = 0; col_b < TC; col_b++) {
#pragma HLS PIPELINE II=1
        To:
          for (to_b = 0; to_b < TM; to_b++) {
#pragma HLS UNROLL
            cnndata_t sum = 0;
          Ti:
            for (ti_b = 0; ti_b < TN; ti_b++) {
#pragma HLS UNROLL
              sum += BufW[to_b][ti_b][i][j] *
                     BufI[ti_b][S_WTS * row_b + i][S_WTS * col_b + j];
            }
            BufO[to_b][row_b][col_b] += sum;
          }
        }
      }
    }
  }
}