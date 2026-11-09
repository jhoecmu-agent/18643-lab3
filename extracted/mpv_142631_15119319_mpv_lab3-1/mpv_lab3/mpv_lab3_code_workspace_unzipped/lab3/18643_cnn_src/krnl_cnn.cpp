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

 #include "../cnn_host/util643.h"
 #include "../cnn_host/instance643.h"
 #include "../cnn_host/kernel643.h"
 
 void cnn_blocked_kernel(cnndata_t BufI[2*TN][R_IFM][C_IFM],
                         cnndata_t BufO[TM][TR][TC],
                         cnndata_t BufW[TM][TN][K_WTS][K_WTS], index_t input_base);
 
 #ifdef __VITIS_CL__ // for lab 3
 extern "C" {
 void krnl_cnn(const cnndata_t* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size) {
 #else
 void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
  const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
  cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
 #endif
 
   /*
    * buffer two input images so each weight tile can be reused
    */
   cnndata_t BufI[2*TN][R_IFM][C_IFM];
 #pragma HLS ARRAY_PARTITION variable=BufI cyclic factor=4 dim=2
 #pragma HLS ARRAY_PARTITION variable=BufI cyclic factor=4 dim=3
 #pragma HLS BIND_STORAGE variable=BufI type=ram_s2p impl=bram latency=2
   cnndata_t BufO[TM][TR][TC];
 #pragma HLS ARRAY_PARTITION variable=BufO complete dim=1
   cnndata_t BufW[TM][TN][K_WTS][K_WTS];
 #pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
 #pragma HLS ARRAY_RESHAPE variable=BufW complete dim=3
 #pragma HLS ARRAY_RESHAPE variable=BufW complete dim=4
 
  ImagePairs: for(uint64_t batch=0; batch<batch_size; batch+=2) {
     const index_t count = (batch+1 < batch_size) ? 2 : 1;
 
     LoadImages: for(index_t b=0; b<count; ++b) {
       for(index_t n=0; n<N_IFM; ++n) {
         for(index_t r=0; r<R_IFM; ++r) {
           for(index_t c=0; c<C_IFM; ++c) {
 #pragma HLS PIPELINE II=1
             BufI[b*TN+n][r][c] = ARRAYi(input,batch+b,n,r,c,0,N_IFM,R_IFM,C_IFM);
           }
         }
       }
     }
 
     OutputTiles: for(index_t to=0; to<M_OFM; to+=TM) {
       const index_t active = MIN(TM,M_OFM-to);
       /*
        * load active weights into local buffer for both images
        */
       LoadWeights: for(index_t m=0; m<active; ++m) {
         for(index_t n=0; n<N_IFM; ++n) {
           for(index_t r=0; r<K_WTS; ++r) {
             for(index_t c=0; c<K_WTS; ++c) {
 #pragma HLS PIPELINE II=1
               BufW[m][n][r][c] = ARRAYw(weights,to+m,n,r,c,M_OFM,N_IFM,K_WTS,K_WTS);
             }
           }
         }
       }
       /*
        * write 0s into unused output-channel lanes in the final tile
        */
       PadWeights: for(index_t m=active; m<TM; ++m) {
         for(index_t n=0; n<TN; ++n) {
           for(index_t r=0; r<K_WTS; ++r) {
             for(index_t c=0; c<K_WTS; ++c) {
 #pragma HLS PIPELINE II=1
               BufW[m][n][r][c] = 0;
             }
           }
         }
       }
       ReuseWeights: for(index_t b=0; b<count; ++b) {
         cnn_blocked_kernel(BufI,BufO,BufW,b*TN);
         StoreImage: for(index_t m=0; m<active; ++m) {
           for(index_t r=0; r<R_OFM; ++r) {
             for(index_t c=0; c<C_OFM; ++c) {
 #pragma HLS PIPELINE II=1
               ARRAYo(output,batch+b,to+m,r,c,0,M_OFM,R_OFM,C_OFM) = BufO[m][r][c];
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
 
   /*
    * Compute TM output channels over a TR x TC tile using TN input channels.
    * Process one input channel and one output position at a time.
    * Share its 3x3 input patch across TM parallel output lanes.
    * Each lane uses its own weights: TM x 9 parallel multiplications.
    *
    * Three stages: initialize sums, accumulate, store final sums.
    */
 
 void cnn_blocked_kernel(cnndata_t BufI[2*TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                         cnndata_t BufO[TM][TR][TC],
                         cnndata_t BufW[TM][TN][K_WTS][K_WTS], index_t input_base) {
 #pragma HLS INLINE off
 #pragma HLS INTERFACE mode=ap_memory port=BufO storage_type=ram_1p
 #pragma HLS INTERFACE mode=ap_memory port=BufW storage_type=ram_1p
 
 
 
   /*
    * Split input rows and columns into 16 banks.
    * A 3x3 patch occupies nine distinct banks, allowing parallel reads.
    */
 #pragma HLS ARRAY_PARTITION variable=BufI cyclic factor=4 dim=2
 #pragma HLS ARRAY_PARTITION variable=BufI cyclic factor=4 dim=3
 
   // One weight bank per lane; pack its nine weights into a 288-bit word.
 #pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
 #pragma HLS ARRAY_RESHAPE variable=BufW complete dim=3
 #pragma HLS ARRAY_RESHAPE variable=BufW complete dim=4
 
   // One output bank per channel for parallel loading and storing.
 #pragma HLS ARRAY_PARTITION variable=BufO complete dim=1
 
   // Map each numbered output position to its row and column.
   unsigned char row[TR*TC], col[TR*TC];
  Table: for(int p=0; p<TR*TC; p++) {
     row[p] = p / TC;
     col[p] = p % TC;
   }
 
   // Keep sums locally: one accumulator bank per lane, with read/write ports.
   cnndata_t acc[TM][TR*TC];
 #pragma HLS ARRAY_PARTITION variable=acc complete dim=1
 #pragma HLS BIND_STORAGE variable=acc type=ram_s2p impl=bram
 
   // Start each output tile at zero; no separate external clear loop.
  LoadO: for(index_t q=0; q<TR*TC; q++) {
 #pragma HLS PIPELINE II=1
     for(int m=0; m<TM; m++) {
 #pragma HLS UNROLL
       acc[m][q] = 0; // All input channels are accumulated in this call.
     }
   }
 
   /*
    * Visit every pixel for one input channel, then advance to the next.
    * Keep channel traversals separate so pending accumulator updates finish.
    */
  Ti: for(index_t ti_b=0; ti_b<TN; ti_b++) {
 #pragma HLS LOOP_FLATTEN off
   Compute: for(index_t pix=0; pix<TR*TC; pix++) {
 #pragma HLS PIPELINE II=1
       const int r = row[pix];
       const int c = col[pix];
 
       // Read the 3x3 patch once and share it across all output lanes.
       cnndata_t win[K_WTS][K_WTS];
 #pragma HLS ARRAY_PARTITION variable=win complete dim=0
       for(int i=0; i<K_WTS; i++) {
 #pragma HLS UNROLL
         for(int j=0; j<K_WTS; j++) {
 #pragma HLS UNROLL
           win[i][j] = BufI[input_base + ti_b][r+i][c+j];
         }
       }
 
       // Each lane sums nine products and adds to its saved pixel sum.
       for(int m=0; m<TM; m++) {
 #pragma HLS UNROLL
         cnndata_t dot = 0;
         for(int i=0; i<K_WTS; i++) {
 #pragma HLS UNROLL
           for(int j=0; j<K_WTS; j++) {
 #pragma HLS UNROLL
             cnndata_t product;
 #pragma HLS BIND_OP variable=product op=mul impl=dsp latency=3
             product = win[i][j] * BufW[m][ti_b][i][j];
             dot += product;
           }
         }
         acc[m][pix] = acc[m][pix] + dot;
       }
     }
   }
 
   // All input channels have contributed; write the completed sums back.
  StoreO: for(index_t q=0; q<TR*TC; q++) {
 #pragma HLS PIPELINE II=1
     for(int m=0; m<TM; m++) {
 #pragma HLS UNROLL
       BufO[m][row[q]][col[q]] = acc[m][q];
     }
   }
 }
 
 