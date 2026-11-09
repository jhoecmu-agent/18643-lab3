/****************************************************************
 * Copyright (c) 2020~2021, 18-643 Course Staff, CMU
 * All rights reserved.

 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:

 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.

 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.

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

 * The views and conclusions contained in the software and
 * documentation are those of the authors and should not be
 * interpreted as representing official policies, either expressed or
 * implied, of the FreeBSD Project.
 ****************************************************************/

/*
 * Blocked CNN accelerator based on Zhang et al., FPGA 2015.
 *
 * Strategy: retain the complete output feature maps on-chip, load each
 * input-channel tile once, and accumulate with a banked TM x TN datapath.
 * Fixed-length, contiguous weight reads enable AXI bursts. The final
 * input-channel tile overlaps earlier channels; duplicated inputs are
 * masked during loading to avoid counting them twice.
 */

#include "util643.h"
#include "instance643.h"
#include "kernel643.h"

static void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                               cnndata_t BufO[TM][M_OFM/TM][TR][TC],
                               cnndata_t BufW[TM][TN][K_WTS][K_WTS],
                               index_t out_group) {
  index_t to_b, ti_b, row_b, col_b, i, j;

  // Pipeline output positions while processing TM x TN products in parallel.
  Krow: for (i=0; i<K_WTS; i++) {
    Kcol: for (j=0; j<K_WTS; j++) {
      Row: for (row_b=0; row_b<TR; row_b++) {
        Col: for (col_b=0; col_b<TC; col_b++) {
#pragma HLS PIPELINE
          To: for (to_b=0; to_b<TM; to_b++) {
#pragma HLS UNROLL
            Ti: for (ti_b=0; ti_b<TN; ti_b++) {
#pragma HLS UNROLL
              BufO[to_b][out_group][row_b][col_b] +=
                  BufW[to_b][ti_b][i][j] *
                  BufI[ti_b][S_WTS*row_b+i][S_WTS*col_b+j];
            }
          }
        }
      }
    }
  }
}

#ifdef __VITIS_CL__
extern "C" {
void krnl_cnn(const cnndata_t* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size) {
#pragma HLS INTERFACE m_axi port=input   offset=slave bundle=gmem0
#pragma HLS INTERFACE m_axi port=weights offset=slave bundle=gmem1
#pragma HLS INTERFACE m_axi port=output  offset=slave bundle=gmem2
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
              const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
              cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
#endif
  cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1];
  cnndata_t BufO[TM][M_OFM/TM][TR][TC];
  cnndata_t BufW[TM][TN][K_WTS][K_WTS];

  // Independent banks feed the unrolled input and output channel lanes.
#pragma HLS ARRAY_PARTITION variable=BufI complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufO complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=2

  for (index_t iter=0; iter<batch_size; iter++) {
    // Keep all output groups on chip until every input tile is accumulated.
    ClearOGroup: for (index_t group=0; group<M_OFM/TM; group++) {
      ClearO: for (index_t ioo=0; ioo<TM; ioo++) {
        for (index_t irr=0; irr<TR; irr++) {
          for (index_t icc=0; icc<TC; icc++) {
#pragma HLS PIPELINE II=1
            BufO[ioo][group][irr][icc] = 0;
          }
        }
      }
    }

    for (index_t ti=0; ti<N_IFM; ti+=TN) {
      // Shift the final tile back to obtain TN valid, contiguous channels.
      const index_t tile_start = (ti + TN <= N_IFM) ? ti : (N_IFM - TN);

      // Reuse each loaded input tile across every output-channel group.
      LoadI: for (index_t iii=0; iii<TN; iii++) {
        const cnndata_t* src = &ARRAYi(input, iter, tile_start+iii, 0, 0, 0, N_IFM, R_IFM, C_IFM);
        // Read unconditionally for bursting; mask already-accumulated channels on chip.
        const bool overlap = (ti + TN > N_IFM) && (tile_start + iii < ti);
        index_t r = 0, c = 0;

        for (index_t k=0; k<R_IFM*C_IFM; k++) {
#pragma HLS PIPELINE II=1
          const cnndata_t value = src[k];
          BufI[iii][r][c] = overlap ? (cnndata_t)0 : value;
          if (++c == C_IFM) {
            c = 0;
            r++;
          }
        }
      }

      for (index_t to=0; to<M_OFM; to+=TM) {
        const index_t out_group = to/TM;

        // Fixed-length contiguous loads allow weight AXI read bursts.
        LoadW: for (index_t ioo=0; ioo<TM; ioo++) {
          const cnndata_t* wsrc = &ARRAYw(weights, to+ioo, tile_start, 0, 0, M_OFM, N_IFM, K_WTS, K_WTS);
          index_t a = 0, b = 0, d = 0;

          LoadWFull: for (index_t k=0; k<TN*K_WTS*K_WTS; k++) {
#pragma HLS PIPELINE II=1
            BufW[ioo][a][b][d] = wsrc[k];
            if (++d == K_WTS) {
              d = 0;
              if (++b == K_WTS) {
                b = 0;
                a++;
              }
            }
          }
        }

        cnn_blocked_kernel(BufI, BufO, BufW, out_group);
      }
    }

    // Write each completed output value to DRAM once.
    for (index_t to=0; to<M_OFM; to+=TM) {
      const index_t out_group = to/TM;
      cnndata_t* dst = &ARRAYo(output, iter, to, 0, 0, 0, M_OFM, R_OFM, C_OFM);

      StoreO: for (index_t ioo=0; ioo<TM; ioo++) {
        for (index_t irr=0; irr<TR; irr++) {
          for (index_t icc=0; icc<TC; icc++) {
#pragma HLS PIPELINE II=1
            dst[(ioo*R_OFM + irr)*C_OFM + icc] = BufO[ioo][out_group][irr][icc];
          }
        }
      }
    }
  }
}
#ifdef __VITIS_CL__
}
#endif
