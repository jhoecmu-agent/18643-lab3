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

#ifdef __VITIS_CL__ // for lab 3
extern "C" {
void krnl_cnn(const cnndata_t* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size) {
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
  const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
  cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
#endif

  // TN=N_IFM, TR=R_OFM, TC=C_OFM
  index_t iter, to;

  cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1];
  cnndata_t BufO[TM][TR][TC];
  cnndata_t BufW[TM][TN][K_WTS][K_WTS];

  for(iter = 0; iter < batch_size; iter++) {

    // load in entire input image once, since TN does not partition any BRAM so it won't blow up DSP + BRAM count :D 
    // sequential addresses for DRAM burst
    BufI_Load_Loop: for (index_t n = 0; n < N_IFM; n++) {
      #pragma HLS PIPELINE
      for (index_t r = 0; r < R_IFM; r++) {
        for (index_t c = 0; c < C_IFM; c++) {
          BufI[n][r][c] = ARRAYi(input,iter,n,r,c,0,N_IFM,R_IFM,C_IFM);
        }
      }
    }

    // operate on each chunk of output 
    for(to=0; to<M_OFM; to+=TM) {
      index_t ioo, iii, irr, icc, too;

      BufW_Write_Loop: for(ioo=0; ioo<TM; ioo++) {
        too = to + ioo;
        for(iii=0; iii<TN; iii++) {
        #pragma HLS PIPELINE
          for(irr=0; irr<K_WTS; irr++) {
            for(icc=0; icc<K_WTS; icc++) {
              // sequential access for DRAM burst
              if (too<M_OFM)
                BufW[ioo][iii][irr][icc]=ARRAYw(weights,too,iii,irr,icc,M_OFM,N_IFM,K_WTS,K_WTS);
              else
                BufW[ioo][iii][irr][icc]=0;
            }
          }
        }
      }

      cnn_blocked_kernel(BufI, BufO, BufW);

      BufO_Write_Loop: for(ioo=0; ioo<TM; ioo++) {
        too = to + ioo;
        #pragma HLS PIPELINE
        for(irr=0; irr<TR; irr++) {
          for(icc=0; icc<TC; icc++) {
            // sequential access for DRAM burst
            if (too<M_OFM)
              ARRAYo(output,iter,too,irr,icc,0,M_OFM,R_OFM,C_OFM)=BufO[ioo][irr][icc];
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
// partition weights TM x K x K ways so the unrolled To/Krow/Kcol loops can
// all read in the same cycle
#pragma HLS ARRAY_PARTITION variable=BufW factor=TM type=cyclic dim=1
#pragma HLS ARRAY_PARTITION variable=BufW factor=K_WTS type=cyclic dim=3
#pragma HLS ARRAY_PARTITION variable=BufW factor=K_WTS type=cyclic dim=4
// partition input K x K ways for the same reason
#pragma HLS ARRAY_PARTITION variable=BufI factor=K_WTS type=cyclic dim=2
#pragma HLS ARRAY_PARTITION variable=BufI factor=K_WTS type=cyclic dim=3
// partition output TM ways so all TM results can be written in one cycle
#pragma HLS ARRAY_PARTITION variable=BufO factor=TM type=cyclic dim=1
#pragma HLS INTERFACE mode=ap_memory port=BufI storage_type=ram_1p
#pragma HLS INTERFACE mode=ap_memory port=BufO storage_type=ram_1p
#pragma HLS INTERFACE mode=ap_memory port=BufW storage_type=ram_1p
    index_t to_b, ti_b, row_b, col_b;

    Row: for(row_b=0; row_b<TR; row_b++) {
        Col: for(col_b=0; col_b<TC; col_b++) {

        // use registers to get II of 1
        cnndata_t accum[TM];
        #pragma HLS ARRAY_PARTITION variable=accum complete dim=1

        // we only ever compute the output once, since we load in the entire input in one pass, so no need to read the old value out
        Init: for(to_b=0; to_b<TM; to_b++) {
        #pragma HLS UNROLL
            accum[to_b] = 0;
        }

        Ti: for(ti_b=0; ti_b<TN; ti_b++) {
        #pragma HLS PIPELINE II=1
            To: for(to_b=0; to_b<TM; to_b++) {
                cnndata_t sum = 0;
                Krow: for(index_t i=0; i<K_WTS; i++) {
                    Kcol: for(index_t j=0; j<K_WTS; j++) {
                        sum += BufW[to_b][ti_b][i][j] *
                                BufI[ti_b][S_WTS*row_b+i][S_WTS*col_b+j];
                    }
                }
                accum[to_b] += sum;
            }
        }

        Write: for(to_b=0; to_b<TM; to_b++) {
            #pragma HLS UNROLL
            BufO[to_b][row_b][col_b] = accum[to_b];
        }
        }
    }
}