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
 * CMU 18643 Fall 2021 Lab Exercise
 *
 * You can edit this file
 */

/****************************************************************
 * Blocked convolution layer implementation
 * (loop structure derived from Figure 9 of):
 *    C. Zhang, et al., "Optimizing FPGA-based Accelerator
 *    Design for Deep Convolutional Neural Networks," FPGA, 2015.
 ****************************************************************/

/*
 * Strategy (per image):
 *   load whole input image -> BufI (173 KB, fits on chip; each input read once)
 *   for each group of TM out maps:
 *     clear BufO
 *     for each group of TN in maps: load weight tile, compute whole 13x13 plane
 *     write BufO back (each output written once)
 * Weights (885 KB) too big to keep on chip -> streamed once per image,
 * tile-major ARRAYw so each tile is one contiguous run.
 * TM=32, TN=2 -> 64 MACs/cycle, ~195 DSP, meets timing at 200 MHz.
 * Board: 116.7 ms / batch of 10, ~8.6x starter (1003.7 ms @ 200 MHz).
 */

#include "util643.h"
#include "instance643.h"
#include "kernel643.h"

// whole image in one burst (perfect nest -> HLS flattens it, II=1)
static void load_input(const cnndata_t *input, index_t b,
                       cnndata_t BufI[N_IFM][R_IFM][C_IFM]) {
#pragma HLS INLINE
  const cnndata_t *src = &ARRAYi(input, b, 0, 0, 0, 0, N_IFM, R_IFM, C_IFM);

 LdI_N: for (index_t n = 0; n < N_IFM; n++) {
  LdI_R: for (index_t r = 0; r < R_IFM; r++) {
   LdI_C: for (index_t c = 0; c < C_IFM; c++) {
#pragma HLS PIPELINE II=1
        BufI[n][r][c] = src[(n * R_IFM + r) * C_IFM + c];
      }
    }
  }
}

// one TM x TN x K x K tile = one contiguous run (tile-major ARRAYw)
// keep this a perfect nest w/ linear address -> single 576-beat burst
// (per-row pointer version risked 64 short bursts per tile)
static void load_weights(const cnndata_t *weights, index_t to, index_t ti,
                         cnndata_t BufW[TM][TN][K_WTS][K_WTS]) {
#pragma HLS INLINE
  const cnndata_t *src = &ARRAYw(weights, to, ti, 0, 0, M_OFM, N_IFM, K_WTS, K_WTS);

 LdW_M: for (index_t m = 0; m < TM; m++) {
  LdW_N: for (index_t n = 0; n < TN; n++) {
   LdW_I: for (index_t i = 0; i < K_WTS; i++) {
    LdW_J: for (index_t j = 0; j < K_WTS; j++) {
#pragma HLS PIPELINE II=1
          BufW[m][n][i][j] = src[((m * TN + n) * K_WTS + i) * K_WTS + j];
        }
      }
    }
  }
}

// TM finished out maps -> DRAM, one burst
static void store_output(cnndata_t *output, index_t b, index_t to,
                         cnndata_t BufO[TM][R_OFM][C_OFM]) {
#pragma HLS INLINE
  cnndata_t *dst = &ARRAYo(output, b, to, 0, 0, 0, M_OFM, R_OFM, C_OFM);

 StO_M: for (index_t m = 0; m < TM; m++) {
  StO_R: for (index_t r = 0; r < R_OFM; r++) {
   StO_C: for (index_t c = 0; c < C_OFM; c++) {
#pragma HLS PIPELINE II=1
        dst[(m * R_OFM + r) * C_OFM + c] = BufO[m][r][c];
      }
    }
  }
}

// compute core: one weight tile over whole output plane
// taps outer, pixels inner -> 9*169 = 1521 steps @ II=1, TM x TN MACs each
// BufI value broadcast to all TM outputs; BufW const for 169 steps
// same BufO[m][r][c] revisited 169 steps later (> pipeline depth)
static void cnn_compute_tile(cnndata_t BufI[N_IFM][R_IFM][C_IFM], index_t ti,
                             cnndata_t BufW[TM][TN][K_WTS][K_WTS],
                             cnndata_t BufO[TM][R_OFM][C_OFM]) {
#pragma HLS INLINE off
#pragma HLS ARRAY_PARTITION variable=BufI cyclic factor=TN dim=1
#pragma HLS ARRAY_PARTITION variable=BufO complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=2

  Krow: for (index_t i = 0; i < K_WTS; i++) {
    Kcol: for (index_t j = 0; j < K_WTS; j++) {
      Row: for (index_t r = 0; r < R_OFM; r++) {
        Col: for (index_t c = 0; c < C_OFM; c++) {
#pragma HLS PIPELINE II=1
          To: for (index_t m = 0; m < TM; m++) {
#pragma HLS UNROLL
            cnndata_t partial = 0;
            Ti: for (index_t n = 0; n < TN; n++) {
#pragma HLS UNROLL
              partial += BufW[m][n][i][j] *
                         BufI[ti + n][S_WTS * r + i][S_WTS * c + j];
            }
            BufO[m][r][c] += partial;
          }
        }
      }
    }
  }
}

#ifdef __VITIS_CL__ // for lab 3
extern "C" {
void krnl_cnn(const cnndata_t* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size) {
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
  const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
  cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
#endif

  // static: BufI too big for sw_emu thread stack; on-chip RAM in HW anyway
  static cnndata_t BufI[N_IFM][R_IFM][C_IFM];
  static cnndata_t BufO[TM][R_OFM][C_OFM];
  static cnndata_t BufW[TM][TN][K_WTS][K_WTS];

  // same splits as cnn_compute_tile (must match)
  // BufI: TN reads/cycle; BufO: TM accumulators; BufW: all TM*TN weights at once
#pragma HLS ARRAY_PARTITION variable=BufI cyclic factor=TN dim=1
#pragma HLS ARRAY_PARTITION variable=BufO complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=2

 Batch: for (index_t b = 0; b < batch_size; b++) {
#pragma HLS LOOP_TRIPCOUNT min=10 max=10 avg=10

    load_input(input, b, BufI);

  OutGrp: for (index_t to = 0; to < M_OFM; to += TM) {

    // clear all TM banks in parallel
    ZeroO_R: for (index_t r = 0; r < R_OFM; r++) {
      ZeroO_C: for (index_t c = 0; c < C_OFM; c++) {
#pragma HLS PIPELINE II=1
        ZeroO_M: for (index_t m = 0; m < TM; m++) {
#pragma HLS UNROLL
            BufO[m][r][c] = 0;
          }
        }
      }

    InGrp: for (index_t ti = 0; ti < N_IFM; ti += TN) {
        load_weights(weights, to, ti, BufW);
        cnn_compute_tile(BufI, ti, BufW, BufO);
      }

      store_output(output, b, to, BufO);
    }
  }
}

#ifdef __VITIS_CL__ // for lab 3
} // extern
#endif
