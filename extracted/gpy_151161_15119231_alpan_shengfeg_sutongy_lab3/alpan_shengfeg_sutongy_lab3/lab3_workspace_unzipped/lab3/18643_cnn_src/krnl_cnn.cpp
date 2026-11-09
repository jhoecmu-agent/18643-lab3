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

  index_t iter;
  index_t row, col, to, ti;

  cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1];
  cnndata_t BufO[TM][TR][TC];
  cnndata_t BufW[TM][TN][K_WTS][K_WTS];

  for(iter = 0; iter < batch_size; iter++) {

    for(row=0; row<R_OFM; row+=TR) {
      for(col=0; col<C_OFM; col+=TC) {
        for(to=0; to<M_OFM; to+=TM) {
          // temporary versions of incremented indices;
          // same usage as in ZhangIsfpga_2()
          index_t trr, tcc, too, tii;

          // only need to zero BufO in this loop ordering
          {
            // indices internal to the block: count from 0
            index_t ioo, icc, irr;

            for(ioo=0; ioo<TM; ioo++) {
              for(irr=0; irr<TR; irr++) {
                for(icc=0; icc<TC; icc++) {
                  BufO[ioo][irr][icc]=0;
                }
              }
            }
          }

          for(ti=0; ti<N_IFM; ti+=TN) {
            {
              /*
               * load active input feature map into local buffer
               */

              // indices internal to the block: count from 0
              index_t irr, icc, iii;

              // incremented temporary indices for input row and col
              index_t  xrr, xcc;

              for(tii=ti,iii=0; tii<MIN(ti+TN,N_IFM); tii++,iii++) {
                for(xrr=row*S_WTS,irr=0; xrr<(MIN(row+TR,R_OFM)*S_WTS+K_WTS-1); xrr++,irr++) {
                  for(xcc=col*S_WTS,icc=0; xcc<(MIN(col+TC,C_OFM)*S_WTS+K_WTS-1); xcc++,icc++) {
                    BufI[iii][irr][icc]=ARRAYi(input,iter,tii,xrr,xcc,0,N_IFM,R_IFM,C_IFM);
                  }
                }
              }
            }

            {
              /*
               * load active weights into local buffer
               */

              // indices internal to the block: count from 0
              index_t ioo, iii, irr, icc;

              for(too=to,ioo=0; too<MIN(to+TM,M_OFM); too++,ioo++) {
                for(tii=ti,iii=0; tii<MIN(ti+TN,N_IFM); tii++,iii++) {
                  for(irr=0; irr<K_WTS; irr++) {
                    for(icc=0; icc<K_WTS; icc++) {
                      BufW[ioo][iii][irr][icc]=ARRAYw(weights,too,tii,irr,icc,M_OFM,N_IFM,K_WTS,K_WTS);
                    }
                  }
                }

                /* write 0s into over-run regions at the end;
                 * this way convolve_kernel() accumulates correctly
                 * without needing a special case */
                if (iii<TN) {
                  for(; iii<TN; iii++) {
                    for(irr=0; irr<K_WTS; irr++) {
                      for(icc=0; icc<K_WTS; icc++) {
                        BufW[ioo][iii][irr][icc]=0;
                      }
                    }
                  }
                }
              }
            }

            /*
             * call the blocked cnn kernel
             */
            cnn_blocked_kernel(BufI, BufO, BufW);
          }

          {
            /*
             * unload finished active intermedaite output feature map
             * from local to full buffer
             */

            // indices internal to the block: count from 0
            index_t ioo, icc, irr;

            for(too=to,ioo=0; too<MIN(to+TM,M_OFM); too++,ioo++) {
              for(trr=row,irr=0; trr<MIN(row+TR,R_OFM); trr++,irr++) {
                for(tcc=col,icc=0; tcc<MIN(col+TC,C_OFM); tcc++,icc++) {
                  ARRAYo(output,iter,too,trr,tcc,0,M_OFM,R_OFM,C_OFM)=BufO[ioo][irr][icc];
                }
              }
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
