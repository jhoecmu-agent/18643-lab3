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

#ifdef __VITIS_CL__ // for lab 3
extern "C" {
void krnl_cnn(const cnndata_t* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size) {
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
  const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
  cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
#endif

cnndata_t BufI[N_IFM][R_IFM][C_IFM]; // one whole input image
cnndata_t BufW[TM][N_IFM][K_WTS][K_WTS]; // weights for TM output channels
cnndata_t BufO[TM][R_OFM][C_OFM]; // outputs for TM output channels

#pragma HLS ARRAY_PARTITION variable=BufI type=cyclic factor=TN dim=1
#pragma HLS ARRAY_PARTITION variable=BufW type=complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW type=cyclic factor=TN dim=2
#pragma HLS ARRAY_PARTITION variable=BufO type=complete dim=1

index_t iter, to;
for(iter = 0; iter < batch_size; iter++) {
  // choose which image
  {
    // load all channels of the input into BufI
    index_t n, r, c;

    LdI_N: for(n=0; n<N_IFM; n++) {
      LdI_R: for(r=0; r<R_IFM; r++) {
        LdI_C: for(c=0; c<C_IFM; c++) {
#pragma HLS PIPELINE II=1
            BufI[n][r][c] = ARRAYi(input,iter,n,r,c,0,N_IFM,R_IFM,C_IFM);
          }
        }
      }
  }

    for(to=0; to<M_OFM; to+=TM) {
      // which TM output channels
      {
        // load the weights for the output channels chosen
        index_t m, n, i, j;

        LdW_M: for(m=0; m<TM; m++) {
          LdW_N: for(n=0; n<N_IFM; n++) {
            LdW_I: for(i=0; i<K_WTS; i++) {
              LdW_J: for(j=0; j<K_WTS; j++) {
#pragma HLS PIPELINE II=1
                  BufW[m][n][i][j] = ARRAYw(weights,to+m,n,i,j,M_OFM,N_IFM,K_WTS,K_WTS);
                }
              }
            }
          }
      }

      {
        // zero output buf before accumlating
        index_t m, r, c;

        Zero_M: for(m=0; m<TM; m++) {
          Zero_R: for(r=0; r<R_OFM; r++) {
            Zero_C: for(c=0; c<C_OFM; c++) {
#pragma HLS PIPELINE II=1
                BufO[m][r][c] = 0;
            }
          }
        }
      }

      {
        // compute the TM output channels into BufO
        index_t ti, i, j, row, col, to_b, ti_b;

        Ti: for(ti=0; ti<N_IFM; ti+=TN) {
          // which TN input channels
          Krow: for(i=0; i<K_WTS; i++) {
            // which filter row
            Kcol: for(j=0; j<K_WTS; j++) {
              // which filter col
              Row: for(row=0; row<R_OFM; row++) {
                // which output row
                Col: for(col=0; col<C_OFM; col++) {
#pragma HLS PIPELINE II=1
                  // which output col
                  To_b: for(to_b=0; to_b<TM; to_b++) {
#pragma HLS UNROLL
                    // TM output channels, all in parallel
                    // read the running sum once, add TN products, write once
                    cnndata_t sum = BufO[to_b][row][col];
                    Ti_b: for(ti_b=0; ti_b<TN; ti_b++) {
#pragma HLS UNROLL
                      // TN input channels, all in parallel
                      sum += BufW[to_b][ti+ti_b][i][j] * BufI[ti+ti_b][S_WTS*row+i][S_WTS*col+j];
                    }

                    BufO[to_b][row][col] = sum;
                  }
                }
              }
            }
          }
        }
      }

      {
        // store the finished TM output channels to DRAM
        index_t m, r, c;

        StO_M: for(m=0; m<TM; m++) {
          StO_R: for(r=0; r<R_OFM; r++) {
            StO_C: for(c=0; c<C_OFM; c++) {
#pragma HLS PIPELINE II=1
                ARRAYo(output,iter,to+m,r,c,0,M_OFM,R_OFM,C_OFM)=BufO[m][r][c];
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
  index_t to_b, ti_b, row_b, col_b;

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
}


