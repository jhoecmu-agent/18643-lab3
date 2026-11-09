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
 * based on Figure 5:
 *    C. Zhang, et al., "Optimizing FPGA-based Accelerator
 *    Design for Deep Convolutional Neural Networks," FPGA, 2015.
 ****************************************************************/

#include "util643.h"
#include "instance643.h"
#include "kernel643.h"

void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                        cnndata_t BufO[TM][TR][TC],
                        cnndata_t BufW[TM][TN][K_WTS][K_WTS]);

#ifdef __VITIS_CL__
extern "C" {
void krnl_cnn(const cnndata_t* input, const cnndata_t* weights,
              cnndata_t* output, uint64_t batch_size) {
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE*N_IFM*R_IFM*C_IFM],
              const cnndata_t weights[M_OFM*N_IFM*K_WTS*K_WTS],
              cnndata_t output[BATCH_SIZE*M_OFM*R_OFM*C_OFM],
              uint64_t batch_size) {
#endif
  cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1];
  cnndata_t BufO[TM][TR][TC];
  cnndata_t BufW[TM][TN][K_WTS][K_WTS];
  // Match the TM x TN fully-unrolled compute engine.
#pragma HLS ARRAY_PARTITION variable=BufI complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=2
#pragma HLS ARRAY_PARTITION variable=BufO complete dim=1

  // Variant B: input-stationary schedule across all output-channel tiles.
  // Keep partial sums on chip since ti now surrounds to.
  cnndata_t Partial[M_OFM][TR][TC];
#pragma HLS ARRAY_PARTITION variable=Partial cyclic factor=TM dim=1
#pragma HLS BIND_STORAGE variable=Partial type=ram_2p impl=bram

  for (index_t iter = 0; iter < batch_size; ++iter) {
    for (index_t row = 0; row < R_OFM; row += TR) {
      for (index_t col = 0; col < C_OFM; col += TC) {
        for (index_t ti = 0; ti < N_IFM; ti += TN) {
          LoadInputOnce: for (index_t n = 0; n < TN; ++n) {
            for (index_t r = 0; r < TR*S_WTS+K_WTS-1; ++r) {
              for (index_t c = 0; c < TC*S_WTS+K_WTS-1; ++c) {
#pragma HLS PIPELINE II=1
                index_t ir = row*S_WTS+r;
                index_t ic = col*S_WTS+c;
                BufI[n][r][c] = ((ti+n < N_IFM) &&
                                 (ir < R_IFM) && (ic < C_IFM))
                    ? ARRAYi(input,iter,ti+n,ir,ic,0,N_IFM,R_IFM,C_IFM)
                    : 0;
              }
            }
          }

          for (index_t to = 0; to < M_OFM; to += TM) {
            RestorePartial: for (index_t m = 0; m < TM; ++m) {
              for (index_t r = 0; r < TR; ++r) {
                for (index_t c = 0; c < TC; ++c) {
#pragma HLS PIPELINE II=1
                  BufO[m][r][c] = ((ti == 0) || (to+m >= M_OFM))
                      ? 0 : Partial[to+m][r][c];
                }
              }
            }

          // Weights are laid out [M_OFM][N_IFM][K][K] in DRAM.
          for (index_t m = 0; m < TM; ++m) {
            for (index_t n = 0; n < TN; ++n) {
              for (index_t ki = 0; ki < K_WTS; ++ki) {
                for (index_t kj = 0; kj < K_WTS; ++kj) {
#pragma HLS PIPELINE II=1
                  if ((to + m < M_OFM) && (ti + n < N_IFM)) {
                    BufW[m][n][ki][kj] = ARRAYw(weights, to+m, ti+n,
                        ki, kj, M_OFM, N_IFM, K_WTS, K_WTS);
                  } else {
                    BufW[m][n][ki][kj] = 0;
                  }
                }
              }
            }
          }

            cnn_blocked_kernel(BufI, BufO, BufW);

            SavePartial: for (index_t m = 0; m < TM; ++m) {
              for (index_t r = 0; r < TR; ++r) {
                for (index_t c = 0; c < TC; ++c) {
#pragma HLS PIPELINE II=1
                  if (to+m < M_OFM)
                    Partial[to+m][r][c] = BufO[m][r][c];
                }
              }
            }
          }
        }

        StoreOutput: for (index_t m = 0; m < M_OFM; ++m) {
          for (index_t r = 0; r < TR; ++r) {
            for (index_t c = 0; c < TC; ++c) {
#pragma HLS PIPELINE II=1
              if ((row+r < R_OFM) && (col+c < C_OFM)) {
                ARRAYo(output,iter,m,row+r,col+c,0,M_OFM,R_OFM,C_OFM)
                    = Partial[m][r][c];
              }
            }
          }
        }
      }
    }
  }

}
#ifdef __VITIS_CL__
}
#endif

void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                        cnndata_t BufO[TM][TR][TC],
                        cnndata_t BufW[TM][TN][K_WTS][K_WTS]) {
cnndata_t acc[TM][TR][TC];
#pragma HLS ARRAY_PARTITION variable=acc complete dim=1
#pragma HLS BIND_STORAGE variable=acc type=ram_2p impl=bram
#pragma HLS ARRAY_PARTITION variable=BufW dim=2 type=complete
#pragma HLS ARRAY_PARTITION variable=BufI dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=BufW dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=BufO dim=1 type=complete
  index_t to_b, ti_b, row_b, col_b;
/*
 Row: for(row_b=0; row_b<TR; row_b++) {
  Col: for(col_b=0; col_b<TC; col_b++) {
    To: for(to_b=0; to_b<TM; to_b++) {
      Ti: for(ti_b=0; ti_b<TN; ti_b++) {
          index_t i, j;

        Krow: for(i=0; i<K_WTS; i++) {
          Kcol: for(j=0; j<K_WTS; j++) {
              BufO[to_b][row_b][col_b]+=
                BufW[to_b][ti_b][i][j]*
                BufI[ti_b][S_WTS*row_b+i][S_WTS*col_b+j];
            }
          }
        }
      }
    }
  }
  */


    // A single counter preserves Krow/Kcol/Row/Col order without nested counter resets.
    Compute: for (index_t step = 0; step < K_WTS*K_WTS*TR*TC; step++) {
#pragma HLS PIPELINE II=1
        const index_t col_b = step % TC;
        const index_t row_b = (step / TC) % TR;
        const index_t j = (step / (TR*TC)) % K_WTS;
        const index_t i = step / (TR*TC*K_WTS);
        To: for (index_t to_b = 0; to_b < TM; to_b++) {
#pragma HLS UNROLL
            cnndata_t sum = 0;
            Ti: for (index_t ti_b = 0; ti_b < TN; ti_b++) {
#pragma HLS UNROLL
                sum += BufW[to_b][ti_b][i][j] *
                       BufI[ti_b][S_WTS*row_b+i][S_WTS*col_b+j];
            }
            // First kernel position consumes the incoming partial output directly.
            // Later positions reuse acc, which has been initialized for every pixel.
            const cnndata_t previous = (step < TR*TC)
                ? BufO[to_b][row_b][col_b]
                : acc[to_b][row_b][col_b];
            const cnndata_t updated = previous + sum;
            // Last kernel position writes the final partial output directly.
            if (step >= (K_WTS*K_WTS-1)*TR*TC)
                BufO[to_b][row_b][col_b] = updated;
            else
                acc[to_b][row_b][col_b] = updated;
        }
    }

}
