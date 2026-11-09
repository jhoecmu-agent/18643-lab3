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
 * Whole-layer convolution kernel (Lab 3, design v2)
 *
 * Blocked loop nest of Zhang et al., "Optimizing FPGA-based Accelerator
 * Design for Deep Convolutional Neural Networks," FPGA 2015 (Fig. 9),
 * with one tile covering the whole 13 x 13 output map (TR = R_OFM,
 * TC = C_OFM, see kernel643.h).
 *
 * Four processes run concurrently (DATAFLOW), linked by FIFOs:
 *
 *   read_input  --sI-->            +---------+
 *                                  | compute |--sO--> write_output
 *   read_weights --sW-->           +---------+
 *
 * read_input    streams each image, TN words per FIFO entry.
 * read_weights  streams the weights of every (to, ti) block in the order
 *               compute uses them, TN words per entry; it runs ahead while
 *               compute works, so a block's weights are waiting in the FIFO
 *               when compute needs them.
 * compute       per image: copies the image into BufI, then for each
 *               block of TM output maps and each block of TN input maps,
 *               takes the block's weights into BufW (TM x KK entries) and
 *               does TM x TN multiply-adds per clock for every tap and
 *               pixel.  On the last tap of the last input block the sums
 *               are final and go straight to sO instead of back to BufO.
 * write_output  writes each finished pixel's TM words to DRAM.
 *
 * DRAM traffic per image: input once, output once, weights once.
 *
 * Every index into a partitioned dimension (the TN input banks, the TM x TN
 * weight RAMs, the TM output RAMs) comes from an unrolled loop, so it is a
 * constant in hardware.  Row and column dimensions are padded to 16 so
 * on-chip addresses are bit concatenations.
 *
 * Arithmetic is on unsigned int: same bits as int, but wrap-around on
 * overflow is defined in C++ (it is undefined for signed int).
 ****************************************************************/

#include "hls_stream.h"
#include "util643.h"
#include "instance643.h"
#include "kernel643.h"

#define NBLK_I (N_IFM/TN)   // input-map blocks
#define NBLK_O (M_OFM/TM)   // output-map blocks
#define KK     (K_WTS*K_WTS) // taps per output pixel
#define PAD    (16)         // padded row/column extent of the buffers

static_assert(R_IFM <= PAD && C_IFM <= PAD, "input map must fit the padded buffer");

// FIFO entries: TN input words (one pixel of one input block, or one row of
// a weight block) or TM output words (one pixel of one output block)
struct vec_i_t { cnndata_t v[TN]; };
struct vec_o_t { cnndata_t v[TM]; };

// one image of input maps: bank tn holds maps tn, tn+TN, tn+2*TN, ...
typedef cnndata_t bufi_t[TN][NBLK_I][PAD][PAD];
// one block of weights: [tm][tn][i*K_WTS+j]
typedef cnndata_t bufw_t[TM][TN][KK];
// partial sums of one block of TM output maps: [tm][r][c]
typedef unsigned int bufo_t[TM][R_OFM][PAD];

/*
 * w * x mod 2^32 with two DSP blocks instead of three (from our Lab 2
 * kernel, v4, where it passed full-range co-simulation and a mutation test).
 * A DSP48E2 multiplies 27 x 18 signed (26 x 17 unsigned).  Split
 *     w = wH*2^26 + wL   (wL = w[25:0], wH = w[31:26])
 *     x = xH*2^17 + xL   (xL = x[16:0], xH = x[31:17])
 * Mod 2^32 a term shifted left by s keeps only its low 32-s bits, and the
 * low n bits of a product need only the low n bits of each factor:
 *     main  wL * xL                   26 x 17 -> DSP
 *     mid   (w[14:0] * xH) mod 2^15   15 x 15 -> DSP
 *     top   (wH * x[5:0])  mod 2^6     6 x 6  -> LUTs
 *     w*x mod 2^32 = main + (mid << 17) + (top << 26)
 *
 * The 360 DSPs do not stretch to TM x TN x 2, so the mid piece of the first
 * N_LUT_MULS multipliers (kernel643.h) is built from LUTs instead (also from
 * Lab 2): split a = aH*2^8 + aL, b = bH*2^8 + bL (aL, bL 8 bits; aH, bH 7):
 *     a*b mod 2^15 = (aL*bL + ((aH*b[6:0] + a[6:0]*bH) mod 2^7) * 2^8) mod 2^15
 * one 8 x 8 and two 7 x 7 LUT multipliers.
 */
static inline unsigned int mul_mid_dsp(unsigned int a15, unsigned int b15) {
#pragma HLS INLINE
  unsigned int p = a15 * b15;
#pragma HLS BIND_OP variable=p op=mul impl=dsp
  return p & 0x7FFFu;
}

static inline unsigned int mul_mid_lut(unsigned int a15, unsigned int b15) {
#pragma HLS INLINE
  unsigned int aL = a15 & 0xFFu, aH = (a15 >> 8) & 0x7Fu;
  unsigned int bL = b15 & 0xFFu, bH = (b15 >> 8) & 0x7Fu;
  unsigned int p0 = aL * bL;              // 8 x 8, full 16-bit product
  unsigned int p1 = aH * (bL & 0x7Fu);    // 7 x 7, only its low 7 bits matter
  unsigned int p2 = (aL & 0x7Fu) * bH;    // 7 x 7, only its low 7 bits matter
#pragma HLS BIND_OP variable=p0 op=mul impl=fabric
#pragma HLS BIND_OP variable=p1 op=mul impl=fabric
#pragma HLS BIND_OP variable=p2 op=mul impl=fabric
  return (p0 + (((p1 + p2) & 0x7Fu) << 8)) & 0x7FFFu;
}

// `lut_mid` is a constant in every unrolled copy, so only one mid is built;
// that needs mul32 inlined into the unrolled loop (as its own module the flag
// would be a run-time input and both mids would be built: v3's first
// synthesis, 392 DSP)
static inline unsigned int mul32(unsigned int w, unsigned int x, bool lut_mid) {
#pragma HLS INLINE
  unsigned int pmain = (w & 0x03FFFFFFu) * (x & 0x0001FFFFu);
#pragma HLS BIND_OP variable=pmain op=mul impl=dsp
  unsigned int pmid = lut_mid ? mul_mid_lut(w & 0x7FFFu, x >> 17)
                              : mul_mid_dsp(w & 0x7FFFu, x >> 17);
  unsigned int ptop = (w >> 26) * (x & 0x3Fu);
#pragma HLS BIND_OP variable=ptop op=mul impl=fabric
  return pmain + (pmid << 17) + ((ptop & 0x3Fu) << 26);
}

/*
 * Stream every image, front to back in DRAM.  ARRAYi keeps a pixel's N_IFM
 * input maps adjacent; one entry is the TN maps of one block of one pixel.
 */
static void read_input(const cnndata_t* input, index_t batch, hls::stream<vec_i_t>& sI) {
 RdImg: for (index_t iter = 0; iter < batch; iter++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=BATCH_SIZE avg=BATCH_SIZE
  RdI: for (index_t r = 0; r < R_IFM; r++) {
      for (index_t c = 0; c < C_IFM; c++) {
        for (index_t nb = 0; nb < NBLK_I; nb++) {
#pragma HLS PIPELINE II=TN/VEC
          vec_i_t e;
          for (index_t tn = 0; tn < TN; tn++) {
            e.v[tn] = ARRAYi(input, iter, nb * TN + tn, r, c, 0, N_IFM, R_IFM, C_IFM);
          }
          sI.write(e);
        }
      }
    }
  }
}

/*
 * Stream the weights of every (to, ti) block of every image, in compute's
 * order.  ARRAYw stores a block contiguously in [i][j][tm][tn] order; the
 * block starts at ARRAYw(weights, to, ti, 0, 0), and inside it map
 * (to+tm, ti+tn) sits where ARRAYw puts (tm, tn) of a block at offset 0.
 * Indexing relative to the block start keeps the address affine in the loop
 * counters, so HLS sees one burst per block.
 */
static void read_weights(const cnndata_t* weights, index_t batch, hls::stream<vec_i_t>& sW) {
 WImg: for (index_t iter = 0; iter < batch; iter++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=BATCH_SIZE avg=BATCH_SIZE
  WBlk: for (index_t to_blk = 0; to_blk < NBLK_O; to_blk++) {
      for (index_t nb = 0; nb < NBLK_I; nb++) {
        const cnndata_t* wblk = &ARRAYw(weights, to_blk * TM, nb * TN, 0, 0, M_OFM, N_IFM, K_WTS, K_WTS);
      RdW: for (index_t i = 0; i < K_WTS; i++) {
          for (index_t j = 0; j < K_WTS; j++) {
            for (index_t tm = 0; tm < TM; tm++) {
#pragma HLS PIPELINE II=TN/VEC
              vec_i_t e;
              for (index_t tn = 0; tn < TN; tn++) {
                e.v[tn] = ARRAYw(wblk, tm, tn, i, j, M_OFM, N_IFM, K_WTS, K_WTS);
              }
              sW.write(e);
            }
          }
        }
      }
    }
  }
}

/*
 * All the arithmetic.  Per image: fill BufI from sI, then every
 * (to, ti) block: weights from sW into BufW, then the block's taps.
 *
 * Tap (i,j) outermost, pixel innermost: consecutive iterations update
 * different pixels, and the same BufO word comes round again only
 * R_OFM * C_OFM iterations later, longer than the pipeline is deep.  The
 * first tap of the first input block starts the sums from zero; on the last
 * tap of the last input block the sums are final and go to sO.
 */
static void compute(index_t batch, hls::stream<vec_i_t>& sI, hls::stream<vec_i_t>& sW,
                    hls::stream<vec_o_t>& sO) {
  static bufi_t BufI;
  static bufw_t BufW;
  static bufo_t BufO;
// TN banks of input maps, TM x TN weight RAMs (KK words each), TM output-map
// RAMs, so one clock's TM x TN multiply-adds get all operands at once
#pragma HLS ARRAY_PARTITION variable=BufI dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=BufW dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=BufW dim=2 type=complete
#pragma HLS ARRAY_PARTITION variable=BufO dim=1 type=complete
// BufO is read and written in the same clock (read-add-write)
#pragma HLS BIND_STORAGE variable=BufO type=ram_s2p

 CImg: for (index_t iter = 0; iter < batch; iter++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=BATCH_SIZE avg=BATCH_SIZE
  FillI: for (index_t r = 0; r < R_IFM; r++) {
      for (index_t c = 0; c < C_IFM; c++) {
        for (index_t nb = 0; nb < NBLK_I; nb++) {
#pragma HLS PIPELINE II=1
          vec_i_t e = sI.read();
          for (index_t tn = 0; tn < TN; tn++) {
            BufI[tn][nb][r][c] = e.v[tn];
          }
        }
      }
    }
  OutBlk: for (index_t to_blk = 0; to_blk < NBLK_O; to_blk++) {
    InBlk: for (index_t nb = 0; nb < NBLK_I; nb++) {
      FillW: for (index_t k = 0; k < KK; k++) {
          for (index_t tm = 0; tm < TM; tm++) {
#pragma HLS PIPELINE II=1
            vec_i_t e = sW.read();
            for (index_t tn = 0; tn < TN; tn++) {
              BufW[tm][tn][k] = e.v[tn];
            }
          }
        }
        bool first_blk = nb == 0;
        bool last_blk = nb == NBLK_I - 1;
      Tap: for (index_t k = 0; k < KK; k++) {
          for (index_t r = 0; r < R_OFM; r++) {
            for (index_t c = 0; c < C_OFM; c++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=BufO type=inter direction=RAW dependent=true distance=R_OFM*C_OFM
              index_t i = k / K_WTS, j = k % K_WTS;
              bool first = first_blk && k == 0;
              bool last = last_blk && k == KK - 1;
              vec_o_t out;
              for (index_t tm = 0; tm < TM; tm++) {
                unsigned int sum = 0;
                for (index_t tn = 0; tn < TN; tn++) {
                  sum += mul32((unsigned int)BufW[tm][tn][k],
                               (unsigned int)BufI[tn][nb][S_WTS * r + i][S_WTS * c + j],
                               tm * TN + tn < (index_t)N_LUT_MULS);
                }
                unsigned int acc = (first ? 0u : BufO[tm][r][c]) + sum;
                BufO[tm][r][c] = acc;
                out.v[tm] = (cnndata_t)acc;
              }
              if (last) sO.write(out);
            }
          }
        }
      }
    }
  }
}

/*
 * Write each finished pixel's TM words.  ARRAYo stores a block of TM output
 * maps contiguously in [r][c][tm] order, the order compute finishes them;
 * indexed relative to the block start, as cnn.cpp's verify() indexes one
 * image.
 */
static void write_output(cnndata_t* output, index_t batch, hls::stream<vec_o_t>& sO) {
 WrImg: for (index_t iter = 0; iter < batch; iter++) {
#pragma HLS LOOP_TRIPCOUNT min=1 max=BATCH_SIZE avg=BATCH_SIZE
  WrBlk: for (index_t to_blk = 0; to_blk < NBLK_O; to_blk++) {
      cnndata_t* oblk = &ARRAYo(output, iter, to_blk * TM, 0, 0, 0, M_OFM, R_OFM, C_OFM);
    WrO: for (index_t r = 0; r < R_OFM; r++) {
        for (index_t c = 0; c < C_OFM; c++) {
#pragma HLS PIPELINE II=TM/VEC
          vec_o_t e = sO.read();
          for (index_t tm = 0; tm < TM; tm++) {
            ARRAYo(oblk, 0, tm, r, c, 0, M_OFM, R_OFM, C_OFM) = e.v[tm];
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
// one AXI master per buffer, so the three streams do not share a port
#pragma HLS INTERFACE mode=m_axi port=input   bundle=gmem0 offset=slave depth=BATCH_SIZE*N_IFM*R_IFM*C_IFM max_widen_bitwidth=128
#pragma HLS INTERFACE mode=m_axi port=weights bundle=gmem1 offset=slave depth=M_OFM*N_IFM*K_WTS*K_WTS max_widen_bitwidth=128
#pragma HLS INTERFACE mode=m_axi port=output  bundle=gmem2 offset=slave depth=BATCH_SIZE*M_OFM*R_OFM*C_OFM max_widen_bitwidth=128
#pragma HLS DATAFLOW

  index_t batch = (index_t)batch_size;

  // sW holds more than one block (TM x KK entries), so read_weights can
  // fetch the next block while compute works on the current one
  hls::stream<vec_i_t> sI("sI");
  hls::stream<vec_i_t> sW("sW");
  hls::stream<vec_o_t> sO("sO");
#pragma HLS STREAM variable=sI depth=64
#pragma HLS STREAM variable=sW depth=2*TM*KK
#pragma HLS STREAM variable=sO depth=32

  read_input(input, batch, sI);
  read_weights(weights, batch, sW);
  compute(batch, sI, sW, sO);
  write_output(output, batch, sO);
}

#ifdef __VITIS_CL__ // for lab 3
} // extern
#endif
