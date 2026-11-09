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
 * based on Figure 9:
 *    C. Zhang, et al., "Optimizing FPGA-based Accelerator
 *    Design for Deep Convolutional Neural Networks," FPGA, 2015.
 ****************************************************************/

/****************************************************************
 * Summary of modifications (Lab 3)
 *
 * Starting point: the Lab 2 cnn_blocked_kernel() (all multipliers
 * unrolled, II=1 over Krow,Kcol,Row,Col, local accumulator). Its
 * body is moved into krnl_cnn() and wrapped as follows.
 *
 * 1. Two images at once, sharing the weights
 *    Two engines compute a group of two images in lockstep from the
 *    same BufW. Each weight is read from DRAM once per group, so the
 *    weight traffic is half of a one-image-at-a-time design with the
 *    same number of multipliers.
 *
 * 2. Whole input images on chip
 *    TR,TC cover the whole 13x13 output map, so both input images are
 *    buffered on chip. Each input value is read from DRAM once.
 *
 * 3. Loop order (per group): to-tile, ti-tile, Krow, Kcol, Row, Col
 *    The accumulators of one to-tile stay on chip until all ti-tiles
 *    are done, so each output value is written to DRAM once and is
 *    never read back.
 *
 * 4. Overlap of compute and data movement
 *    - BufW has two halves: block k is computed from one half while
 *      block k+1 is loaded into the other.
 *    - Outputs are drained during the first pass (Krow=Kcol=0) of the
 *      next to-tile.
 *    - BufI is NOT double buffered: four images do not fit in BRAM.
 *      Compute waits while each group of two images is loaded.
 *
 * 5. DATAFLOW with one AXI master per array
 *    read_input / read_weights / write_output are separate processes
 *    on separate m_axi bundles, connected to the compute process by
 *    FIFOs. All DRAM accesses go through ARRAYi, ARRAYw and ARRAYo.
 *
 * 6. Sequential DRAM layouts (see kernel643.h)
 *    ARRAYw and ARRAYo are changed so that the weights are read and
 *    the outputs are written in exactly the order the datapath uses.
 *
 * 7. 32x32 multiply in 2 DSPs instead of 3 (see mul32lo())
 *    Only the low 32 bits of the product are needed.
 *
 * 8. m_axi sized on purpose
 *    The default 512-bit widening with long bursts and many
 *    outstanding requests costs >140 BRAM_18K per port.
 *
 * 9. Control logic without multipliers or wide counters
 *    Counters are only as wide as their range, and BufI/BufW
 *    addresses are formed by bit concatenation.
 ****************************************************************/

#include "util643.h"
#include "instance643.h"
#include "kernel643.h"
#include "ap_int.h"
#include "hls_stream.h"

// derived sizes
#define K2      (K_WTS*K_WTS)            // weights per (to,ti) pair
#define NT_O    (M_OFM/TM)               // number of to-tiles
#define NT_I    ((N_IFM+TN-1)/TN)        // number of ti-tiles
#define LAST_TN (N_IFM-(NT_I-1)*TN)      // valid channels in the last ti-tile
#define PIX     (TR*TC)                  // output pixels per tile
#define MAIN_N  (K2*PIX)                 // compute cycles per block
#define BLK_W   (TM*TN*K2)               // weights per block
#define IMG_W   (N_IFM*R_IFM*C_IFM)      // input words per image
#define WTS_W   (M_OFM*N_IFM*K2)         // weight words (whole layer)
#define OUT_V   (NT_O*PIX)               // TM-wide output vectors per image
#define GRP_CYC (NT_O*NT_I*MAIN_N)       // compute cycles per group of images

// BufI bank address: {ti-tile, row, col}, with rows and columns
// padded to 16 so the address is a bit concatenation
#define BI_TILE (16*16)
#define BI_WORDS (NT_I*BI_TILE)
// BufW bank address: {half, k}, so each half starts on a power of two
#define BW_HALF (16)

#if (TR != R_OFM) || (TC != C_OFM)
#error "krnl_cnn assumes one tile covers the whole output feature map"
#endif
#if (M_OFM % TM) != 0
#error "M_OFM must be a multiple of TM"
#endif
#if (R_IFM > 16) || (C_IFM > 16) || (K2 > BW_HALF)
#error "address fields are too small"
#endif
#if (BATCH_SIZE % CNN_ENGINES) != 0
#error "the batch must be a multiple of CNN_ENGINES"
#endif
#if (TN >= N_IFM) || (CNN_ENGINES*TN > 31) || (TM > 16) || (NT_I > 31) || (NT_O > 31) || (PIX > 256)
#error "counter widths are too small"
#endif
#if (BLK_W > MAIN_N) || (BLK_W > IMG_W)
#error "prefetch does not fit in the compute time"
#endif

// TM outputs of one pixel of one image
typedef struct {
  cnndata_t d[TM];
} ovec_t;

// the same pixel of both images, read from all accumulator banks at once
typedef struct {
  cnndata_t d[CNN_ENGINES * TM];
} gvec_t;

/*
 * Low 32 bits of a*b using 2 DSP48E2 instead of the 3 HLS uses for a
 * full 32x32 multiply. With a = a1*2^17 + a0 and b = b1*2^26 + b0:
 *   a*b = a0*b0 + a1*b0*2^17 + a0*b1*2^26 (mod 2^32)
 * a0*b0 (17x26) fits one DSP. Of a1*b0 only the low 15 bits matter
 * (15x15, one DSP). Of a0*b1 only the low 6 bits matter (6x6, LUTs).
 */
static inline cnndata_t mul32lo(cnndata_t a, cnndata_t b) {
#pragma HLS INLINE
  ap_uint<32> ua = a, ub = b;
  ap_uint<17> a0 = ua.range(16, 0);
  ap_uint<15> a1 = ua.range(31, 17);
  ap_uint<26> b0 = ub.range(25, 0);
  ap_uint<15> b0l = ub.range(14, 0);
  ap_uint<6>  b1 = ub.range(31, 26);
  ap_uint<6>  a0l = ua.range(5, 0);
  ap_uint<43> p0 = a0 * b0;
  ap_uint<15> p1 = a1 * b0l;
  ap_uint<6>  p2 = a0l * b1;
#pragma HLS BIND_OP variable=p0 op=mul impl=dsp
#pragma HLS BIND_OP variable=p1 op=mul impl=dsp
#pragma HLS BIND_OP variable=p2 op=mul impl=fabric
  ap_uint<32> r = p0.range(31, 0);
  r += ((ap_uint<32>)p1) << 17;
  r += ((ap_uint<32>)p2) << 26;
  return (cnndata_t)r;
}

// x+1, or 0 on the last count
template <int W>
static inline ap_uint<W> inc_wrap(ap_uint<W> x, bool last) {
#pragma HLS INLINE
  ap_uint<W> r = x + 1;
  if (last) r = 0;
  return r;
}

static inline ap_uint<13> bi_addr(ap_uint<5> tile, ap_uint<4> r, ap_uint<4> c) {
#pragma HLS INLINE
  return (tile, r, c);
}

static inline ap_uint<5> bw_addr(ap_uint<1> half, ap_uint<4> k) {
#pragma HLS INLINE
  return (half, k);
}

// DRAM -> FIFO: all images, through ARRAYi
static void read_input(const cnndata_t *input, hls::stream<cnndata_t> &s_in,
                       uint64_t batch_size) {
  index_t batch = batch_size;
  RdImg: for (index_t b = 0; b < batch; b++) {
    RdCh: for (index_t n = 0; n < N_IFM; n++) {
      RdRow: for (index_t r = 0; r < R_IFM; r++) {
        RdCol: for (index_t c = 0; c < C_IFM; c++) {
#pragma HLS PIPELINE II=1
          s_in.write(ARRAYi(input, b, n, r, c, batch, N_IFM, R_IFM, C_IFM));
        }
      }
    }
  }
}

// DRAM -> FIFO: the whole weight array once per group of images, through
// ARRAYw, in the order the blocks are used
static void read_weights(const cnndata_t *weights, hls::stream<cnndata_t> &s_w,
                         uint64_t batch_size) {
  index_t batch = batch_size;
  RdWGrp: for (index_t g = 0; g < batch / CNN_ENGINES; g++) {
    RdWT: for (index_t t = 0; t < NT_O; t++) {
      RdWN: for (index_t n = 0; n < N_IFM; n++) {
        RdWM: for (index_t m = 0; m < TM; m++) {
          RdWR: for (index_t r = 0; r < K_WTS; r++) {
            RdWC: for (index_t c = 0; c < K_WTS; c++) {
#pragma HLS PIPELINE II=1
              s_w.write(ARRAYw(weights, t * TM + m, n, r, c, M_OFM, N_IFM, K_WTS, K_WTS));
            }
          }
        }
      }
    }
  }
}

// serialize one pixel of both images into two TM-wide vectors
static void split_output(hls::stream<gvec_t> &s_out, hls::stream<ovec_t> &s_o8,
                         uint64_t batch_size) {
  index_t batch = batch_size;
  SpGrp: for (index_t g = 0; g < batch / CNN_ENGINES; g++) {
    SpT: for (index_t t = 0; t < NT_O; t++) {
      SpP: for (index_t p = 0; p < PIX; p++) {
#pragma HLS PIPELINE II=2
        gvec_t v = s_out.read();
        ovec_t o0, o1;
        SpTo: for (index_t k = 0; k < TM; k++) {
#pragma HLS UNROLL
          o0.d[k] = v.d[k];
          o1.d[k] = v.d[TM + k];
        }
        s_o8.write(o0);
        s_o8.write(o1);
      }
    }
  }
}

// FIFO -> DRAM, through ARRAYo
static void write_output(hls::stream<ovec_t> &s_o8, cnndata_t *output,
                         uint64_t batch_size) {
  index_t batch = batch_size;
  WrGrp: for (index_t g = 0; g < batch / CNN_ENGINES; g++) {
    WrT: for (index_t t = 0; t < NT_O; t++) {
      WrR: for (index_t r = 0; r < R_OFM; r++) {
        WrC: for (index_t c = 0; c < C_OFM; c++) {
          WrE: for (index_t e = 0; e < CNN_ENGINES; e++) {
#pragma HLS PIPELINE II=1
            ovec_t o = s_o8.read();
            WrTo: for (index_t k = 0; k < TM; k++) {
#pragma HLS UNROLL
              ARRAYo(output, g * CNN_ENGINES + e, t * TM + k, r, c, batch, M_OFM, R_OFM, C_OFM) = o.d[k];
            }
          }
        }
      }
    }
  }
}

// input prefetch position in ARRAYi order; channel = it*TN + ib
typedef struct {
  ap_uint<5> ib; // bank (channel within ti-tile)
  ap_uint<5> it; // ti-tile
  ap_uint<4> ir; // row
  ap_uint<4> ic; // col
} in_pos_t;

static inline void next_in(in_pos_t &q) {
#pragma HLS INLINE
  bool lc = (q.ic == C_IFM - 1);
  bool lr = (q.ir == R_IFM - 1);
  bool lb = (q.ib == TN - 1);
  q.ic = inc_wrap(q.ic, lc);
  if (lc) q.ir = inc_wrap(q.ir, lr);
  if (lc && lr) q.ib = inc_wrap(q.ib, lb);
  if (lc && lr && lb) q.it = q.it + 1;
}

// weight prefetch position in ARRAYw order: ti, to, k.
// wti reaches TN when the block is complete.
typedef struct {
  ap_uint<5> ti;
  ap_uint<4> to;
  ap_uint<4> k;
} w_pos_t;

static inline void next_w(w_pos_t &q) {
#pragma HLS INLINE
  bool lk = (q.k == K2 - 1);
  bool lo = (q.to == TM - 1);
  q.k = inc_wrap(q.k, lk);
  if (lk) q.to = inc_wrap(q.to, lo);
  if (lk && lo) q.ti = q.ti + 1;
}

static void compute(hls::stream<cnndata_t> &s_in, hls::stream<cnndata_t> &s_w,
                    hls::stream<gvec_t> &s_out, uint64_t batch_size) {
  index_t batch = batch_size;

  // Memory: one bank per (engine,ti) for BufI, one per (to,ti) for BufW
  // and one per (engine,to) for acc, so all products are fed every
  // cycle. A BufW bank feeds one multiplier in each engine.
  // BufW holds two halves (current / next), read and written in the
  // same cycle, hence simple dual-port memories.
  cnndata_t BufI[CNN_ENGINES * TN][BI_WORDS];
#pragma HLS ARRAY_PARTITION variable=BufI dim=1 type=complete
#pragma HLS BIND_STORAGE variable=BufI type=ram_s2p impl=bram latency=2
  cnndata_t BufW[TM][TN][2 * BW_HALF];
#pragma HLS ARRAY_PARTITION variable=BufW dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=BufW dim=2 type=complete
#pragma HLS BIND_STORAGE variable=BufW type=ram_s2p impl=lutram
  // running sums of the current to-tile
  cnndata_t acc[CNN_ENGINES][TM][PIX];
#pragma HLS ARRAY_PARTITION variable=acc dim=1 type=complete
#pragma HLS ARRAY_PARTITION variable=acc dim=2 type=complete
#pragma HLS BIND_STORAGE variable=acc type=ram_s2p impl=bram latency=2

  if (batch == 0) return;

  w_pos_t qw = {0, 0, 0};
  ap_uint<4> col = 0, row = 0, kk = 0;
  ap_uint<2> j = 0, i = 0;
  ap_uint<8> p = 0;
  ap_uint<5> u = 0;
  ap_uint<5> t = 0;
  ap_uint<1> cur_w = 0;   // half holding the current block
  bool have_prev = false; // a finished to-tile is waiting in acc

  Grp: for (index_t g = 0; g < batch / CNN_ENGINES; g++) {
    bool last_grp = (g == batch / CNN_ENGINES - 1);

    // Step 1: load both images of the group (compute waits). The first
    // group also loads weight block 0, which covers channels 0..TN-1.
    LdImg: for (index_t e = 0; e < CNN_ENGINES; e++) {
      in_pos_t qi = {0, 0, 0, 0};
      Ld: for (index_t n = 0; n < IMG_W; n++) {
#pragma HLS PIPELINE II=1
        ap_uint<5> bank = (e ? TN : 0) + qi.ib;
        BufI[bank][bi_addr(qi.it, qi.ir, qi.ic)] = s_in.read();
        next_in(qi);
        if (g == 0 && e == 0 && n < BLK_W) {
          BufW[qw.to][qw.ti][bw_addr(0, qw.k)] = s_w.read();
          next_w(qw);
        }
      }
    }
    if (g == 0) { qw.ti = 0; qw.to = 0; qw.k = 0; }

    // Step 2: compute. Each Main iteration takes one cycle and does all
    // products of both engines in parallel. In the same cycle it may
    // also store one word of the next weight block (other BufW half)
    // and send out the finished sums of the previous to-tile.
    Main: for (index_t n = 0; n < GRP_CYC; n++) {
#pragma HLS PIPELINE II=1
      // An acc element is reused only PIX iterations later.
#pragma HLS DEPENDENCE variable=acc type=inter dependent=false
      // BufW is read in the current half and written in the other.
#pragma HLS DEPENDENCE variable=BufW type=inter dependent=false
#pragma HLS DEPENDENCE variable=BufW type=intra dependent=false
      bool lu = (u == NT_I - 1);
      bool lt = (t == NT_O - 1);
      // first pass of a to-tile: start the sums from 0
      bool first = (u == 0) && (i == 0) && (j == 0);

      ap_uint<4> irow = row + i;
      ap_uint<4> icol = col + j;
      ap_uint<13> iaddr = bi_addr(u, irow, icol);
      ap_uint<5> waddr = bw_addr(cur_w, kk);
      gvec_t o;
      En: for (index_t e = 0; e < CNN_ENGINES; e++) {
#pragma HLS UNROLL
        To: for (index_t to_b = 0; to_b < TM; to_b++) {
#pragma HLS UNROLL
          // adder tree over the TN products of this output map
          cnndata_t sum = 0;
          Ti: for (index_t ti_b = 0; ti_b < TN; ti_b++) {
#pragma HLS UNROLL
            sum += mul32lo(BufW[to_b][ti_b][waddr], BufI[e * TN + ti_b][iaddr]);
          }
          cnndata_t old = acc[e][to_b][p];
          o.d[e * TM + to_b] = old;
          acc[e][to_b][p] = (first ? 0 : old) + sum;
        }
      }
      if (first && have_prev) s_out.write(o);

      // prefetch the next weight block; ti beyond the last channel are zero
      if (!(last_grp && lt && lu) && qw.ti < TN) {
        ap_uint<5> wlim = (u == NT_I - 2) ? LAST_TN : TN;
        BufW[qw.to][qw.ti][bw_addr(!cur_w, qw.k)] = (qw.ti < wlim) ? s_w.read() : 0;
        next_w(qw);
      }

      // advance col, then row, j, i, u, t
      bool lc = (col == TC - 1);
      bool lr = (row == TR - 1);
      bool lj = (j == K_WTS - 1);
      bool li = (i == K_WTS - 1);
      bool end_pass = lc && lr;
      bool end_blk = end_pass && lj && li;
      col = inc_wrap(col, lc);
      p = inc_wrap(p, end_pass);
      if (lc) row = inc_wrap(row, lr);
      if (end_pass) {
        j = inc_wrap(j, lj);
        kk = inc_wrap(kk, lj && li);
      }
      if (end_pass && lj) i = inc_wrap(i, li);
      if (end_blk) {
        u = inc_wrap(u, lu);
        cur_w = !cur_w;
        qw.ti = 0; qw.to = 0; qw.k = 0;
      }
      if (end_blk && lu) {
        t = inc_wrap(t, lt);
        have_prev = true;
      }
    }
  }

  // Step 3 (epilogue): send out the sums of the last to-tile
  Epi: for (p = 0; p < PIX; p++) {
#pragma HLS PIPELINE II=1
    gvec_t o;
    EpiE: for (index_t e = 0; e < CNN_ENGINES; e++) {
#pragma HLS UNROLL
      EpiTo: for (index_t to_b = 0; to_b < TM; to_b++) {
#pragma HLS UNROLL
        o.d[e * TM + to_b] = acc[e][to_b][p];
      }
    }
    s_out.write(o);
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
#pragma HLS INTERFACE mode=m_axi port=input offset=slave bundle=gmem0 depth=432000 max_read_burst_length=32 num_read_outstanding=4 max_write_burst_length=2 num_write_outstanding=1
#pragma HLS INTERFACE mode=m_axi port=weights offset=slave bundle=gmem1 depth=221184 max_read_burst_length=32 num_read_outstanding=4 max_write_burst_length=2 num_write_outstanding=1
#pragma HLS INTERFACE mode=m_axi port=output offset=slave bundle=gmem2 depth=216320 max_widen_bitwidth=256 max_write_burst_length=16 num_write_outstanding=4 max_read_burst_length=2 num_read_outstanding=1
#pragma HLS INTERFACE mode=s_axilite port=input bundle=control
#pragma HLS INTERFACE mode=s_axilite port=weights bundle=control
#pragma HLS INTERFACE mode=s_axilite port=output bundle=control
#pragma HLS INTERFACE mode=s_axilite port=batch_size bundle=control
#pragma HLS INTERFACE mode=s_axilite port=return bundle=control
#pragma HLS DATAFLOW

  hls::stream<cnndata_t> s_in("s_in");
  hls::stream<cnndata_t> s_w("s_w");
  hls::stream<gvec_t> s_out("s_out");
  hls::stream<ovec_t> s_o8("s_o8");
  // s_w absorbs the gap between the bursty weight demand and the DRAM
  // read rate; s_out holds one whole to-tile drain (PIX vectors) so
  // the compute pipeline does not stall on a DRAM write.
#pragma HLS STREAM variable=s_in depth=64
#pragma HLS STREAM variable=s_w depth=1024
#pragma HLS STREAM variable=s_out depth=176
#pragma HLS STREAM variable=s_o8 depth=16

  read_input(input, s_in, batch_size);
  read_weights(weights, s_w, batch_size);
  compute(s_in, s_w, s_out, batch_size);
  split_output(s_out, s_o8, batch_size);
  write_output(s_o8, output, batch_size);
}

#ifdef __VITIS_CL__ // for lab 3
} // extern
#endif
