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
#include <hls_stream.h>
#include <ap_int.h>

#define NB      ((N_IFM+TN-1)/TN)           // input-channel blocks (14)
#define WTILE   (TM*TN*K_WTS*K_WTS)         // words per weight tile (1008)
#define IMG_PIX (R_IFM*C_IFM)               // input pixels per channel (225)
#define IMG_W   (N_IFM*R_IFM*C_IFM)         // words per image in DRAM (43200)
#define IMG_PAD (NB*TN*R_IFM*C_IFM)         // padded to NB*TN channels (44100)
#define OBLK    (TM*R_OFM*C_OFM)            // words per output block (1352)

struct obeat_t { cnndata_t v[TM]; };        // one output pixel, all TM channels

// Fill positions for the idle halves of BufI/BufW. Narrow counters that
// wrap on a compare of the old value keep the refill logic off the
// critical path.
struct refill_t {
  bool        more_w;          // another weight tile follows this call
  ap_uint<16> in_left;         // words of the next image still to load
  ap_uint<8>  pix;             // next image: pixel (row*C_IFM + col)
  ap_uint<4>  bank;            //             bank (= ch % TN)
  ap_uint<12> base;            //             (ch / TN) * IMG_PIX
  ap_uint<4>  m, t;            // next tile: output and input channel
  ap_uint<2>  i, j;            //            kernel row and column
};
static_assert(IMG_PIX <= 256 && TN <= 16 && NB*IMG_PIX <= 4096 && TM <= 16 &&
              K_WTS <= 4 && IMG_PAD < 65536, "refill_t fields too narrow");

// Image stream order: channel (ch = blk*TN + bank), then pixel.
// Each bank is one flat dimension (half, block, pixel).
static inline void put_in(cnndata_t BufI[TN][2*NB*IMG_PIX],
                          index_t half, cnndata_t v, refill_t &rf) {
#pragma HLS INLINE
  BufI[rf.bank][half*NB*IMG_PIX + rf.base + rf.pix] = v;
  const bool pw = (rf.pix == IMG_PIX-1), bw = (rf.bank == TN-1);
  const bool kw = (rf.base == (NB-1)*IMG_PIX);
  if (pw) rf.pix = 0; else rf.pix++;
  if (pw) {
    if (bw) rf.bank = 0; else rf.bank++;
    if (bw) { if (kw) rf.base = 0; else rf.base += IMG_PIX; }
  }
}

// Weight stream order:
//  input channel, kernel row, kernel column, output channel.
// BufW keeps (half, i, j) in one flat dimension
static inline void put_w(cnndata_t BufW[TM][TN][2*K_WTS*K_WTS],
                         index_t half, cnndata_t v, refill_t &rf) {
#pragma HLS INLINE
  BufW[rf.m][rf.t][half*K_WTS*K_WTS + rf.i*K_WTS + rf.j] = v;
  const bool mw = (rf.m == TM-1), jw = (rf.j == K_WTS-1);
  const bool iw = (rf.i == K_WTS-1), tw = (rf.t == TN-1);
  if (mw) rf.m = 0; else rf.m++;
  if (mw) {
    if (jw) rf.j = 0; else rf.j++;
    if (jw) {
      if (iw) rf.i = 0; else rf.i++;
      if (iw) { if (tw) rf.t = 0; else rf.t++; }
    }
  }
}

void cnn_blocked_kernel(cnndata_t BufI[TN][2*NB*IMG_PIX],
                        cnndata_t BufO[TM][TR][TC],
                        cnndata_t BufW[TM][TN][2*K_WTS*K_WTS],
                        index_t q, index_t tb, index_t wb, bool first_blk, bool last_blk,
                        refill_t &rf, hls::stream<cnndata_t> &is,
                        hls::stream<cnndata_t> &ws, hls::stream<obeat_t> &os);

/****************************************************************
 * krnl_cnn() is a DATAFLOW region of four processes:
 *
 *   rd_input   --is-->  
 *                      cnn_compute  --os-->  wr_output
 *   rd_weights --ws-->
 ****************************************************************/
static void rd_input(const cnndata_t *input, hls::stream<cnndata_t> &is, uint64_t batch_size);
static void rd_weights(const cnndata_t *weights, hls::stream<cnndata_t> &ws, uint64_t batch_size);
static void cnn_compute(hls::stream<cnndata_t> &is, hls::stream<cnndata_t> &ws,
                        hls::stream<obeat_t> &os, uint64_t batch_size);
static void wr_output(cnndata_t *output, hls::stream<obeat_t> &os, uint64_t batch_size);

#ifdef __VITIS_CL__ // for lab 3
extern "C" {
void krnl_cnn(const cnndata_t* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size) {
#else
void krnl_cnn(const cnndata_t input[BATCH_SIZE * N_IFM * R_IFM * C_IFM],
 const cnndata_t weights[M_OFM * N_IFM * K_WTS * K_WTS],
 cnndata_t output[BATCH_SIZE * M_OFM * R_OFM * C_OFM], uint64_t batch_size) {
#endif
// One AXI master per array so the three streams don't contend for a port.
// read buffer is width x burst x outstanding; capped so it doesn't take too much BRAM.
#pragma HLS INTERFACE m_axi port=input   offset=slave bundle=gmem0 depth=BATCH_SIZE*N_IFM*R_IFM*C_IFM max_read_burst_length=256 num_read_outstanding=16
#pragma HLS INTERFACE m_axi port=weights offset=slave bundle=gmem1 depth=M_OFM*N_IFM*K_WTS*K_WTS max_widen_bitwidth=128 max_read_burst_length=64 num_read_outstanding=8
#pragma HLS INTERFACE m_axi port=output  offset=slave bundle=gmem2 depth=BATCH_SIZE*M_OFM*R_OFM*C_OFM max_write_burst_length=256 num_write_outstanding=16
#pragma HLS INTERFACE s_axilite port=batch_size
#pragma HLS INTERFACE s_axilite port=return
#pragma HLS DATAFLOW

  hls::stream<cnndata_t> is("is");
  hls::stream<cnndata_t> ws("ws");
  hls::stream<obeat_t>   os("os");
#pragma HLS STREAM variable=is depth=1024
#pragma HLS STREAM variable=ws depth=1024
#pragma HLS STREAM variable=os depth=TR*TC

  rd_input(input, is, batch_size);
  rd_weights(weights, ws, batch_size);
  cnn_compute(is, ws, os, batch_size);
  wr_output(output, os, batch_size);
}

#ifdef __VITIS_CL__ // for lab 3
} // extern
#endif

// Single-pass needs distinct first and last kernel taps.
#define SINGLE_PASS (BUFO_SINGLE_PASS && (K_WTS*K_WTS > 1))
static_assert(SINGLE_PASS, "cnn_blocked_kernel() requires SINGLE_PASS");
static_assert(WTILE <= K_WTS*K_WTS*TR*TC, "next weight tile must fit inside one call");

#define NPIX  (TR*TC)             // output pixels per tile
#define NITER (K_WTS*K_WTS*NPIX)  // one iteration per (kernel tap, pixel)

/*
 * Index tables for the flat Conv loop.
 * Iteration n visits (i, j, row, col) in the order
 *   Krow -> Kcol -> Row -> Col, i.e. n = ((i*K_WTS + j)*TR + row)*TC + col.
 * A single counter n replaces four nested loop counters, whose chained
 * wrap-around logic cannot be pipelined and would limit the clock.
 * The tables are compile-time constants, so HLS maps each to a ROM
 */
struct iter_tab_t { unsigned char v[NITER]; };

constexpr iter_tab_t make_iter_tab(int which) {
  iter_tab_t t = {};
  for (int n = 0; n < NITER; n++) {
    int col = n % TC;
    int row = (n / TC) % TR;
    int j   = (n / NPIX) % K_WTS;
    int i   = (n / NPIX) / K_WTS;
    t.v[n] = (unsigned char)(which==0 ? i : which==1 ? j : which==2 ? row : col);
  }
  return t;
}

static constexpr iter_tab_t TAB_I   = make_iter_tab(0);
static constexpr iter_tab_t TAB_J   = make_iter_tab(1);
static constexpr iter_tab_t TAB_ROW = make_iter_tab(2);
static constexpr iter_tab_t TAB_COL = make_iter_tab(3);

void cnn_blocked_kernel(cnndata_t BufI[TN][2*NB*IMG_PIX],
                        cnndata_t BufO[TM][TR][TC],
                        cnndata_t BufW[TM][TN][2*K_WTS*K_WTS],
                        index_t q, index_t tb, index_t wb, bool first_blk, bool last_blk,
                        refill_t &rf, hls::stream<cnndata_t> &is,
                        hls::stream<cnndata_t> &ws, hls::stream<obeat_t> &os) {
// Keep the per-call Conv pipeline separate from cnn_compute()'s loops:
// flattening them made the loop-index wrap logic the critical path.
#pragma HLS INLINE off

// One memory bank per unrolled channel: TN banks of BufI, TM of BufO and
// TM*TN of BufW. Every multiplier then gets its operands in the same
// cycle, while each bank still sees one access per cycle (ram_1p).
#pragma HLS ARRAY_PARTITION variable=BufI dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=BufO dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=BufW dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=BufW dim=2 type=complete

  index_t to_b, ti_b, row_b, col_b;
  index_t n;

  // Running sums are kept in a local dual-port buffer, because the
  // single-port BufO cannot be read and written in the same cycle.
  // One bank per output channel; the registered read (latency=2) keeps
  // the memory-to-adder path short.
  cnndata_t acc[TM][TR][TC];
#pragma HLS ARRAY_PARTITION variable=acc dim=1 type=complete
#pragma HLS BIND_STORAGE variable=acc type=ram_2p impl=bram latency=2

  refill_t st = rf;   // local copy of the refill state, written back below
  const index_t ibase = (q*NB + tb)*IMG_PIX;   // this call's image/block in BufI

#if !SINGLE_PASS
 LoadRow: for(row_b=0; row_b<TR; row_b++) {
  LoadCol: for(col_b=0; col_b<TC; col_b++) {
#pragma HLS PIPELINE II=1
    LoadTo: for(to_b=0; to_b<TM; to_b++) {
#pragma HLS UNROLL
        acc[to_b][row_b][col_b] = BufO[to_b][row_b][col_b];
      }
    }
  }
#endif

  /*
   * Main loop: kernel taps outermost, pixels next, channels innermost
   * and fully unrolled. Each iteration handles one (tap, pixel) pair and
   * performs all TM*TN multiply-accumulates for it in parallel.
   */
 Conv: for(n=0; n<NITER; n++) {
#pragma HLS PIPELINE II=1
// The same acc[.][row][col] entry is revisited only every NPIX (=169)
// iterations, far more than the pipeline depth, so there is no real
// loop-carried dependence. Not valid if TR*TC is made very small.
#pragma HLS DEPENDENCE variable=acc type=inter dependent=false
// BufI/BufW are refilled only in the idle half and read only in the
// active half, so reads and writes never touch the same word.
#pragma HLS DEPENDENCE variable=BufI type=inter dependent=false
#pragma HLS DEPENDENCE variable=BufI type=intra dependent=false
#pragma HLS DEPENDENCE variable=BufW type=inter dependent=false
#pragma HLS DEPENDENCE variable=BufW type=intra dependent=false

          // refill the idle halves: at most one DRAM word per cycle
          if (n < WTILE) {
            if (st.more_w) put_w(BufW, 1-wb, ws.read(), st);
          } else if (st.in_left != 0) {
            put_in(BufI, 1-q, is.read(), st);
            st.in_left--;
          }

          const index_t i = TAB_I.v[n];
          const index_t j = TAB_J.v[n];
          row_b = TAB_ROW.v[n];
          col_b = TAB_COL.v[n];

          // first/last kernel tap: BufO is read on the first tap only
          // and written on the last tap only.
          bool last  = (n >= NITER-NPIX);
#if SINGLE_PASS
          bool first = (n < NPIX);
#endif
          obeat_t ob;  // finished pixel
          // BufW/BufI addresses, all TM*TN multipliers
          const index_t wa = wb*K_WTS*K_WTS + i*K_WTS + j;
          const index_t ia = ibase + (S_WTS*row_b + i)*C_IFM + (S_WTS*col_b + j);

        To: for(to_b=0; to_b<TM; to_b++) {
#pragma HLS UNROLL
            cnndata_t sum = 0;

          Ti: for(ti_b=0; ti_b<TN; ti_b++) {
#pragma HLS UNROLL
              cnndata_t p;
#pragma HLS BIND_OP variable=p op=mul impl=dsp latency=4
              p = BufW[to_b][ti_b][wa]*
                  BufI[ti_b][ia];
              sum += p;
            }

#if SINGLE_PASS
            // tap 1:     acc  = BufO + sum   (BufO read; 0 for the first block)
            // taps 2..8: acc += sum
            // tap 9:     BufO = acc + sum    (BufO write; streamed out for the last block)
            if (first) {
              acc[to_b][row_b][col_b] = (first_blk ? (cnndata_t)0 : BufO[to_b][row_b][col_b]) + sum;
            } else {
              cnndata_t v = acc[to_b][row_b][col_b] + sum;
              if (last) {
                if (last_blk) ob.v[to_b] = v;
                else          BufO[to_b][row_b][col_b] = v;
              }
              else      acc[to_b][row_b][col_b] = v;
            }
#else
            cnndata_t v = acc[to_b][row_b][col_b] + sum;
            if (last) BufO[to_b][row_b][col_b] = v;
            else      acc[to_b][row_b][col_b] = v;
#endif
          }
          if (last && last_blk) os.write(ob);
        }

  rf = st;
}

/****************************************************************
 * cnn_compute(): the Lab 2 krnl_cnn() loop nest.
 ****************************************************************/
static void cnn_compute(hls::stream<cnndata_t> &is, hls::stream<cnndata_t> &ws,
                        hls::stream<obeat_t> &os, uint64_t batch_size) {
  index_t iter;
  index_t row, col, to, ti;

  // two whole images and two weight tiles (current + next)
  cnndata_t BufI[TN][2*NB*IMG_PIX];
  cnndata_t BufO[TM][TR][TC];
  cnndata_t BufW[TM][TN][2*K_WTS*K_WTS];
// (banked by the ARRAY_PARTITION pragmas in cnn_blocked_kernel())
#pragma HLS BIND_STORAGE variable=BufI type=ram_s2p impl=bram
#pragma HLS BIND_STORAGE variable=BufW type=ram_s2p impl=lutram

  if (batch_size == 0) return;

  refill_t rf = {};

  // prologue: the first image and the first weight tile
 PreIn: for (index_t x = 0; x < IMG_PAD; x++) {
#pragma HLS PIPELINE II=1
    put_in(BufI, 0, is.read(), rf);
  }
 PreW: for (index_t x = 0; x < WTILE; x++) {
#pragma HLS PIPELINE II=1
    put_w(BufW, 0, ws.read(), rf);
  }

  index_t wb = 0;

  for(iter = 0; iter < batch_size; iter++) {
    rf.in_left = (iter + 1 < batch_size) ? IMG_PAD : 0;

    for(row=0; row<R_OFM; row+=TR) {
      for(col=0; col<C_OFM; col+=TC) {
        for(to=0; to<M_OFM; to+=TM) {
          index_t tb;  // input block number, ti/TN

          for(ti=0,tb=0; ti<N_IFM; ti+=TN,tb++) {
            rf.more_w = !(iter+1 == batch_size && to+TM >= M_OFM && ti+TN >= N_IFM);

            /*
             * call the blocked cnn kernel
             */
            cnn_blocked_kernel(BufI, BufO, BufW, iter & 1, tb, wb,
                               ti == 0, ti+TN >= N_IFM, rf, is, ws, os);
            wb = 1 - wb;
          }
        }
      }
    }
  }
}

// Each image: IMG_W words in ARRAYi order, then zeros for the padding
// channels N_IFM..NB*TN-1.
static void rd_input(const cnndata_t *input, hls::stream<cnndata_t> &is,
                     uint64_t batch_size) {
 RdInImg: for (uint64_t iter = 0; iter < batch_size; iter++) {
    const cnndata_t *src = &ARRAYi(input, iter, 0, 0, 0, 0, N_IFM, R_IFM, C_IFM);
  RdIn: for (index_t x = 0; x < IMG_W; x++) {
#pragma HLS PIPELINE II=1
      is.write(src[x]);
    }
  RdInPad: for (index_t x = IMG_W; x < IMG_PAD; x++) {
#pragma HLS PIPELINE II=1
      is.write(0);
    }
  }
}

// Per call (iter, to, ti): one WTILE-word tile, zero padded when the last
// input block has fewer than TN channels.
static void rd_weights(const cnndata_t *weights, hls::stream<cnndata_t> &ws,
                       uint64_t batch_size) {
 RdWImg: for (uint64_t iter = 0; iter < batch_size; iter++) {
  RdWTo: for (index_t to = 0; to < M_OFM; to += TM) {
   RdWTi: for (index_t ti = 0; ti < N_IFM; ti += TN) {
        const cnndata_t *src = &ARRAYw(weights, to, ti, 0, 0, M_OFM, N_IFM, K_WTS, K_WTS);
        const index_t nval = MIN(TN, N_IFM - ti) * K_WTS*K_WTS*TM;
      RdW: for (index_t x = 0; x < nval; x++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=WTILE max=WTILE
          ws.write(src[x]);
        }
      RdWPad: for (index_t x = nval; x < WTILE; x++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=0 max=WTILE
          ws.write(0);
        }
      }
    }
  }
}

// Per output block: OBLK contiguous words in ARRAYo order.
static void wr_output(cnndata_t *output, hls::stream<obeat_t> &os,
                      uint64_t batch_size) {
 WrOImg: for (uint64_t iter = 0; iter < batch_size; iter++) {
  WrOTo: for (index_t to = 0; to < M_OFM; to += TM) {
      cnndata_t *dst = &ARRAYo(output, iter, to, 0, 0, 0, M_OFM, R_OFM, C_OFM);
      obeat_t ob = {};
    WrO: for (index_t x = 0; x < OBLK; x++) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_FLATTEN off  // flattened index wrap logic missed 3.33 ns
        if (x % TM == 0) ob = os.read();
        dst[x] = ob.v[x % TM];
      }
    }
  }
}
