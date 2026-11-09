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
                        cnndata_t BufW[TM][TN][K_WTS][K_WTS],
                        bool first);

void load_tile(const cnndata_t* __restrict input, const cnndata_t* __restrict weights,
               bool enable,
               index_t iter, index_t row, index_t col, index_t to, index_t ti,
               cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
               cnndata_t BufW[TM][TN][K_WTS][K_WTS]);

void compute_image(const cnndata_t* __restrict input, const cnndata_t* __restrict weights,
                   index_t iter, uint64_t batch_size,
                   cnndata_t BufO[TM][TR][TC],
                   cnndata_t BufI0[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                   cnndata_t BufI1[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                   cnndata_t BufW0[TM][TN][K_WTS][K_WTS],
                   cnndata_t BufW1[TM][TN][K_WTS][K_WTS]);

void store_image(cnndata_t* __restrict output, bool enable, index_t iter,
                 cnndata_t BufO[TM][TR][TC]);

/* cnn_blocked_kernel() computes the TM output channels in TM/TU groups of TU */
static_assert(TM%TU==0, "TU must divide TM");
/* every row, col and to tile is full, so the loads need no MIN() bounds and
 * HLS sees constant trip counts (long bursts, known cycle counts) */
static_assert(R_OFM%TR==0 && C_OFM%TC==0 && M_OFM%TM==0,
              "tile sizes must divide the layer evenly");
/* TN need not divide N_IFM: the last ti block holds TN_LAST channels
 * load_tile() zeroes the rest of BufI, so they add nothing */
#define NB_N ((N_IFM+TN-1)/TN)  // ti blocks per image
#define TN_LAST (N_IFM-(NB_N-1)*TN)  // channels in the last ti block
/* one tile covers a whole image, so the block after an image's last ti block
 * is block 0 of the next image (prefetched by compute_image()) */
static_assert(TR==R_OFM && TC==C_OFM && TM==M_OFM, "one tile per image");
/* an even number of ti blocks per image starts every image on BufI0/BufW0 */
static_assert(NB_N%2==0, "even number of ti blocks per image");

#ifdef __VITIS_CL__ // for lab 3
extern "C" {
void krnl_cnn(const cnndata_t* __restrict input, const cnndata_t* __restrict weights, cnndata_t* __restrict output, uint64_t batch_size) {
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
  const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
  cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
#endif

  index_t iter;

  // output gets separate AXI port, so storing one image overlaps the loads of the next
#pragma HLS INTERFACE m_axi port=input bundle=gmem0
#pragma HLS INTERFACE m_axi port=weights bundle=gmem0
#pragma HLS INTERFACE m_axi port=output bundle=gmem1
  // one compute_image() serves both BufO copies (else HLS may build two)
#pragma HLS ALLOCATION function instances=compute_image limit=1

  // cyclic partition by TU, among the TM dimensions
  // each pipeline cycle accesses TN inputs, TU*TN weights, and TU outputs
  // BufI and BufW are ping pong buffer: one is computed on while the other is loaded
  // BufO is ping pong across images: one is computed on while the other is stored
  cnndata_t BufI0[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1];
#pragma HLS ARRAY_PARTITION variable=BufI0 dim=1 type=complete           // TN banks of 15x15
  cnndata_t BufI1[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1];
#pragma HLS ARRAY_PARTITION variable=BufI1 dim=1 type=complete
  cnndata_t BufO0[TM][TR][TC];
#pragma HLS ARRAY_PARTITION variable=BufO0 dim=1 type=cyclic factor=TU   // TU banks of (TM/TU)x13x13
  cnndata_t BufO1[TM][TR][TC];
#pragma HLS ARRAY_PARTITION variable=BufO1 dim=1 type=cyclic factor=TU
  cnndata_t BufW0[TM][TN][K_WTS][K_WTS];
#pragma HLS ARRAY_PARTITION variable=BufW0 dim=1 type=cyclic factor=TU   // TU x TN banks of (TM/TU)x3x3
#pragma HLS ARRAY_PARTITION variable=BufW0 dim=2 type=complete
  cnndata_t BufW1[TM][TN][K_WTS][K_WTS];
#pragma HLS ARRAY_PARTITION variable=BufW1 dim=1 type=cyclic factor=TU
#pragma HLS ARRAY_PARTITION variable=BufW1 dim=2 type=complete

  // first block of first iamge, compute_image() prefetches it for the later images
  load_tile(input, weights, /*enable*/ batch_size>0, 0, 0, 0, 0, 0, BufI0, BufW0);

  // ping pong buffer: store previous, compute current
  Batch: for(iter=0; iter<batch_size; iter++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=10
    if (iter%2 == 0) {
      store_image(output, /*enable*/ iter>0, /*iter*/ iter-1, BufO1);
      compute_image(input, weights, /*iter*/ iter, batch_size, BufO0, BufI0, BufI1, BufW0, BufW1);
    }
    else {
      store_image(output, /*enable*/ true, /*iter*/ iter-1, BufO0);
      compute_image(input, weights, /*iter*/ iter, batch_size, BufO1, BufI0, BufI1, BufW0, BufW1);
    }
  }

  // store the last image
  if (batch_size%2 == 1)
    store_image(output, /*enable*/ true, /*iter*/ batch_size-1, BufO0);
  else
    store_image(output, /*enable*/ true, /*iter*/ batch_size-1, BufO1);
}

#ifdef __VITIS_CL__ // for lab 3
} // extern
#endif

void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                        cnndata_t BufO[TM][TR][TC],
                        cnndata_t BufW[TM][TN][K_WTS][K_WTS],
                        bool first /* for zeroing BufO */) {
#pragma HLS INLINE off
  index_t too_o, i, j, to_i, ti_b, row_b, col_b;

  // BufO keeps all TM channels on BRAM, but only TU of them are computed per cycle on DSPs
  To_o: for(too_o=0; too_o<TM; too_o+=TU) {   // go through TM/TU loop
    Krow: for(i=0; i<K_WTS; i++) {
      Kcol: for(j=0; j<K_WTS; j++) {
        Row: for(row_b=0; row_b<TR; row_b++) {
          Col: for(col_b=0; col_b<TC; col_b++) {
#pragma HLS PIPELINE II=1
            To: for(to_i=0; to_i<TU; to_i++) {
#pragma HLS UNROLL
              // the first write to an output (first ti block, kr=kc=0) zeroes acc
              // no need to expliticly zero BufO
              cnndata_t acc=(first && i==0 && j==0) ? 0 : BufO[too_o+to_i][row_b][col_b];
              Ti: for(ti_b=0; ti_b<TN; ti_b++) {
#pragma HLS UNROLL
                acc+=BufW[too_o+to_i][ti_b][i][j]*
                     BufI[ti_b][S_WTS*row_b+i][S_WTS*col_b+j];
              }
              BufO[too_o+to_i][row_b][col_b]=acc;
            }
          }
        }
      }
    }
  }
}

void load_tile(const cnndata_t* __restrict input, const cnndata_t* __restrict weights, 
               bool enable,
               index_t iter, index_t row, index_t col, index_t to, index_t ti,
               cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
               cnndata_t BufW[TM][TN][K_WTS][K_WTS]) {
#pragma HLS INLINE off      // make sure HLS preserves the load-compute overlap
  // enable is false on the last ti block, when there is nothing left to load
  if (!enable) return;

  // indices internal to the block: count from 0
  index_t ioo, iii, irr, icc;

  // [N/TN][M][TN][K][K] layout, weights are one contiguous block
  const cnndata_t *wblk=&ARRAYw(weights,to,ti,0,0,M_OFM,N_IFM,K_WTS,K_WTS);

  // write full and partial blocks into separate loops, each with constant trip counts
  // HLS can still infer one burst per loop
  if (TN_LAST==TN || ti+TN<=N_IFM) {    // if TN divides N_IFM perfectly, no need for partial block
    /*
     * load active input feature map into local buffer:
     */
    LoadI_N: for(iii=0; iii<TN; iii++) {
      LoadI_R: for(irr=0; irr<TR*S_WTS+K_WTS-1; irr++) {
        LoadI_C: for(icc=0; icc<TC*S_WTS+K_WTS-1; icc++) {
          BufI[iii][irr][icc]=ARRAYi(input,iter,ti+iii,row*S_WTS+irr,col*S_WTS+icc,0,N_IFM,R_IFM,C_IFM);
        }
      }
    }

    /*
     * load active weights into local buffer
     */
    LoadW_M: for(ioo=0; ioo<TM; ioo++) {
      LoadW_N: for(iii=0; iii<TN; iii++) {
        LoadW_KR: for(irr=0; irr<K_WTS; irr++) {
          LoadW_KC: for(icc=0; icc<K_WTS; icc++) {
            BufW[ioo][iii][irr][icc]=wblk[((ioo*TN+iii)*K_WTS+irr)*K_WTS+icc];
          }
        }
      }
    }
  }
  else {
    /*
     * load active input feature map into local buffer:
     * load only until TN_LAST, then zero fill the rest
     */
    LoadIL_N: for(iii=0; iii<TN_LAST; iii++) {
      LoadIL_R: for(irr=0; irr<TR*S_WTS+K_WTS-1; irr++) {
        LoadIL_C: for(icc=0; icc<TC*S_WTS+K_WTS-1; icc++) {
          BufI[iii][irr][icc]=ARRAYi(input,iter,ti+iii,row*S_WTS+irr,col*S_WTS+icc,0,N_IFM,R_IFM,C_IFM);
        }
      }
    }
    ZeroI_N: for(iii=TN_LAST; iii<TN; iii++) {
      ZeroI_R: for(irr=0; irr<TR*S_WTS+K_WTS-1; irr++) {
        ZeroI_C: for(icc=0; icc<TC*S_WTS+K_WTS-1; icc++) {
          BufI[iii][irr][icc]=0;
        }
      }
    }

    /*
     * load active weights into local buffer, until TN_LAST
     */
    LoadWL_M: for(ioo=0; ioo<TM; ioo++) {
      LoadWL_N: for(iii=0; iii<TN_LAST; iii++) {
        LoadWL_KR: for(irr=0; irr<K_WTS; irr++) {
          LoadWL_KC: for(icc=0; icc<K_WTS; icc++) {
            BufW[ioo][iii][irr][icc]=wblk[((ioo*TN_LAST+iii)*K_WTS+irr)*K_WTS+icc];
          }
        }
      }
    }
  }
}

void compute_image(const cnndata_t* __restrict input, const cnndata_t* __restrict weights,
                   index_t iter, uint64_t batch_size,
                   cnndata_t BufO[TM][TR][TC],
                   cnndata_t BufI0[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                   cnndata_t BufI1[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                   cnndata_t BufW0[TM][TN][K_WTS][K_WTS],
                   cnndata_t BufW1[TM][TN][K_WTS][K_WTS]) {
#pragma HLS INLINE off
  index_t ti, tb;

  // start with ti loop. row, col, to all take only one iteration
  TileN: for(ti=0,tb=0; ti<N_IFM; ti+=TN,tb++) {
    bool last = (ti+TN >= N_IFM);   // last in this input tile

    // if not last image, prefetch block 0 of the next image (iter+1)
    bool ld_en = (!last) || (iter+1 < batch_size);    
    index_t ld_iter = last ? (iter+1) : iter;
    index_t ld_ti = last ? 0 : (ti+TN);

    // ping pong buffer for BufI and BufW, compute current and load next
    if (tb%2 == 0) {
      cnn_blocked_kernel(BufI0, BufO, BufW0, /*first*/ ti==0);
      load_tile(input, weights, ld_en, ld_iter, 0, 0, 0, ld_ti, BufI1, BufW1);
    }
    else {
      cnn_blocked_kernel(BufI1, BufO, BufW1, /*first*/ ti==0);
      load_tile(input, weights, ld_en, ld_iter, 0, 0, 0, ld_ti, BufI0, BufW0);
    }
  }
}

void store_image(cnndata_t* __restrict output, bool enable, index_t iter,
                 cnndata_t BufO[TM][TR][TC]) {
#pragma HLS INLINE off
  // enable is false before the first image, when there is nothing to store
  if (!enable) return;

  /*
   * unload finished output feature map from local to full buffer;
   * one tile is the whole image, so this is one contiguous run
   */

  // indices internal to the block: count from 0
  index_t ioo, irr, icc;

  StoreO_M: for(ioo=0; ioo<TM; ioo++) {
    StoreO_R: for(irr=0; irr<TR; irr++) {
      StoreO_C: for(icc=0; icc<TC; icc++) {
        ARRAYo(output,iter,ioo,irr,icc,0,M_OFM,R_OFM,C_OFM)=BufO[ioo][irr][icc];
      }
    }
  }
}
