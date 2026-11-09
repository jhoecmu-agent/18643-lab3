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

static_assert(M_OFM % TM == 0 && N_IFM % TN == 0, "Packed layout requires complete channel groups.");
static_assert(TR == R_OFM && TC == C_OFM && TN % 4 == 0, "This loader requires a full spatial tile and four-channel groups.");

void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                        cnndata_t BufO[M_OFM/TM][TM][TR][TC],
                        cnndata_t BufW[TM][TN][K_WTS][K_WTS],
                        index_t tb, bool first_input_block);

// CPU-mode queue models the same producer/consumer data ordering.
#ifdef __VITIS_CL__
#include <hls_stream.h>
#else
#include <queue>
namespace hls {
template<class T> class stream {
  std::queue<T> items;
public:
  explicit stream(const char* = "") {}
  void write(const T& value) { items.push(value); }
  T read() { T value=items.front(); items.pop(); return value; }
};
}
#endif

// 128-bit input packing
struct PackedInput { cnndata_t lane[4]; };
static_assert(sizeof(PackedInput) == 4*sizeof(cnndata_t), "Input packing must be dense.");
#ifndef __VITIS_CL__
#include <cstring>
#endif

struct WeightVector { cnndata_t lane[TM]; };

static void stream_weights(const cnndata_t* weights,
                           hls::stream<WeightVector>& fifo, index_t ng) {
#pragma HLS INLINE off
 WeightGroup: for(index_t mg=0; mg<M_OFM/TM; ++mg) {
  WeightWord: for(index_t word=0; word<TN*K_WTS*K_WTS; ++word) {
#pragma HLS PIPELINE II=1
    const index_t n=word/(K_WTS*K_WTS);
    const index_t r=(word/K_WTS)%K_WTS;
    const index_t c=word%K_WTS;
    WeightVector data;
   WeightLane: for(index_t m=0; m<TM; ++m) {
#pragma HLS UNROLL
      data.lane[m]=ARRAYw(weights,mg*TM+m,ng*TN+n,r,c,M_OFM,N_IFM,K_WTS,K_WTS);
   }
    fifo.write(data);
  }
 }
}

static void compute_output_groups(cnndata_t BufI[TN][R_IFM][C_IFM],
                                  cnndata_t BufO[M_OFM/TM][TM][TR][TC],
                                  hls::stream<WeightVector>& fifo,
                                  bool first_input_block) {
#pragma HLS INLINE off
  cnndata_t BufW[TM][TN][K_WTS][K_WTS];
// 8 x 12 weight banks
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=2
 OutputGroup: for(index_t mg=0; mg<M_OFM/TM; ++mg) {
  ReceiveWord: for(index_t word=0; word<TN*K_WTS*K_WTS; ++word) {
#pragma HLS PIPELINE II=1
    const index_t n=word/(K_WTS*K_WTS);
    const index_t r=(word/K_WTS)%K_WTS;
    const index_t c=word%K_WTS;
    const WeightVector data=fifo.read();
   ReceiveLane: for(index_t m=0; m<TM; ++m) {
#pragma HLS UNROLL
      BufW[m][n][r][c]=data.lane[m];
   }
  }
  // Load tile before compute
  cnn_blocked_kernel(BufI,BufO,BufW,mg,first_input_block);
 }
}

static void process_input_group(const cnndata_t* weights,
                                cnndata_t BufI[TN][R_IFM][C_IFM],
                                cnndata_t BufO[M_OFM/TM][TM][TR][TC],
                                index_t ng) {
#pragma HLS INLINE off
// Prefetch during compute
#pragma HLS DATAFLOW
  hls::stream<WeightVector> weight_fifo("weight_fifo");
#pragma HLS STREAM variable=weight_fifo depth=256
  stream_weights(weights,weight_fifo,ng);
  compute_output_groups(BufI,BufO,weight_fifo,ng==0);
}

#ifdef __VITIS_CL__ // for lab 3
extern "C" {
void krnl_cnn(const PackedInput* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size) {
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
  const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
  cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
#endif
#pragma HLS INTERFACE m_axi port=input   bundle=gmem0
#pragma HLS AGGREGATE variable=input compact=none
#pragma HLS INTERFACE m_axi port=weights bundle=gmem1
#pragma HLS INTERFACE m_axi port=output  bundle=gmem2

  // Full 13x13 spatial tile; static group bounds expose aligned bursts.
  for(index_t iter=0; iter<batch_size; ++iter) {
#pragma HLS LOOP_TRIPCOUNT min=BATCH_SIZE max=BATCH_SIZE
    cnndata_t BufI[TN][R_IFM][C_IFM];
    // On-chip partial sums
    cnndata_t BufO[M_OFM/TM][TM][TR][TC];
#pragma HLS ARRAY_PARTITION variable=BufI complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufO complete dim=2

   InputGroup: for(index_t ng=0; ng<N_IFM/TN; ++ng) {
    const index_t ti=ng*TN;
    // Four contiguous words per cycle, scattered to channel banks.
    InputWord: for(index_t word=0; word<R_IFM*C_IFM*TN; word+=4) {
#pragma HLS PIPELINE II=1
      // 128-bit word index
      const index_t word_index=(INDEXi(iter,ti,0,0,N_IFM,R_IFM,C_IFM)+word)/4;
      PackedInput data;
#ifdef __VITIS_CL__
      data=input[word_index];
#else
      std::memcpy(data.lane,input+word_index*4,sizeof(data));
#endif
     InputLane: for(index_t lane=0; lane<4; ++lane) {
#pragma HLS UNROLL
        const index_t index=word+lane;
        const index_t n=index%TN;
        const index_t pixel=index/TN;
        const index_t r=pixel/C_IFM;
        const index_t c=pixel%C_IFM;
        BufI[n][r][c]=data.lane[lane];
     }
    }

    // Reuse across 16 output groups
    process_input_group(weights,BufI,BufO,ng);
   }

   // 256-bit output packing
   WriteGroup: for(index_t mg=0; mg<M_OFM/TM; ++mg) {
    OutputWord: for(index_t pixel=0; pixel<TR*TC; ++pixel) {
#pragma HLS PIPELINE II=1
      const index_t r=pixel/TC;
      const index_t c=pixel%TC;
     OutputLane: for(index_t m=0; m<TM; ++m) {
#pragma HLS UNROLL
        ARRAYo(output,iter,mg*TM+m,r,c,0,M_OFM,R_OFM,C_OFM)=BufO[mg][m][r][c];
     }
    }
   }
  }
}

#ifdef __VITIS_CL__ // for lab 3
} // extern
#endif

void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1],
                        cnndata_t BufO[M_OFM/TM][TM][TR][TC],
                        cnndata_t BufW[TM][TN][K_WTS][K_WTS],
                        index_t tb, bool first_input_block) {
  index_t to_b, ti_b, row_b, col_b;

 Krow: for(index_t i=0; i<K_WTS; i++) {
  Kcol: for(index_t j=0; j<K_WTS; j++) {
   Row: for(row_b=0; row_b<TR; row_b++) {
    Col: for(col_b=0; col_b<TC; col_b++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=BufO inter false
      // 8 x 12 channel unroll
      To: for(to_b=0; to_b<TM; to_b++) {
#pragma HLS UNROLL
        // Zero on first block and tap
        cnndata_t acc = (first_input_block && i == 0 && j == 0)
                         ? 0 : BufO[tb][to_b][row_b][col_b];
        Ti: for(ti_b=0; ti_b<TN; ti_b++) {
#pragma HLS UNROLL
          acc += BufW[to_b][ti_b][i][j]*
                 BufI[ti_b][S_WTS*row_b+i][S_WTS*col_b+j];
        }
        BufO[tb][to_b][row_b][col_b] = acc;
      }
    }
   }
  }
 }
}
