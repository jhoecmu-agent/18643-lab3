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

#include "kernel643.h"
#ifdef CNN_NATIVE_TEST
#include "native_stream.h"
#include <thread>
#else
#include <hls_stream.h>
#endif

namespace {
constexpr int INPUT_WORDS = TN * R_IFM * C_IFM;
constexpr int WEIGHT_WORDS = TM * TN * K_WTS * K_WTS;
constexpr int OUTPUT_WORDS = TM * TR * TC;
constexpr int N_TILES = N_IFM / TN;
static_assert(N_IFM % TN == 0, "TN must divide N_IFM; host must use the same TN");
static_assert(TR == R_OFM && TC == C_OFM && TM == M_OFM,
              "This implementation requires full spatial/output-channel tiles");
static_assert(S_WTS == 1 && K_WTS == 3, "Banking assumes stride 1 and a 3x3 filter");
static_assert(sizeof(cnndata_t) == 4, "Expected 32-bit data");
static_assert(TN == 8, "ensure correct dimensions not stale");

// Only these loader functions access input/weight DRAM. Fixed-length, linear
// inner loops expose bursts independently of buffer indexing and compute.
void load_inputs(const cnndata_t* input, hls::stream<cnndata_t>& fifo,
                 unsigned long batch_size) {
#pragma HLS INLINE off
    for (unsigned long b = 0; b < batch_size; ++b) {
        for (int tile = 0; tile < N_TILES; ++tile) {
            const unsigned long base = b * (N_IFM * R_IFM * C_IFM)
                                     + tile * INPUT_WORDS;
            InputBurst: for (int p = 0; p < INPUT_WORDS; ++p) {
#pragma HLS PIPELINE II=1
                fifo.write(input[base + p]);
            }
        }
    }
}

void load_weights(const cnndata_t* weights, hls::stream<cnndata_t>& fifo,
                  unsigned long batch_size) {
#pragma HLS INLINE off
    for (unsigned long b = 0; b < batch_size; ++b) {
        for (int tile = 0; tile < N_TILES; ++tile) {
            const int base = tile * WEIGHT_WORDS;
            WeightBurst: for (int p = 0; p < WEIGHT_WORDS; ++p) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_FLATTEN off
                fifo.write(weights[base + p]);
            }
        }
    }
}

void compute_tiles(hls::stream<cnndata_t>& inputs,
                   hls::stream<cnndata_t>& weights,
                   hls::stream<cnndata_t>& results,
                   unsigned long batch_size) {
#pragma HLS INLINE off
    cnndata_t BufI[2][TN][R_IFM][C_IFM];
#pragma HLS ARRAY_PARTITION variable=BufI complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufI complete dim=2
#pragma HLS ARRAY_PARTITION variable=BufI cyclic factor=K_WTS dim=3
#pragma HLS ARRAY_PARTITION variable=BufI cyclic factor=K_WTS dim=4
    // Each partition is only 25 x 32 bits. Use distributed RAM to preserve
    // BRAM capacity for weights, output accumulation, and the platform.
#pragma HLS BIND_STORAGE variable=BufI type=ram_1p impl=lutram latency=1
    cnndata_t BufW[2][TM][TN][K_WTS][K_WTS];
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=3
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=4
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=5
#pragma HLS BIND_STORAGE variable=BufW type=ram_1p impl=bram
    // One owner and one accumulation buffer, shared across all channel tiles.
    cnndata_t BufO[OUTPUT_WORDS];
#pragma HLS BIND_STORAGE variable=BufO type=ram_s2p impl=bram

    for (unsigned long b = 0; b < batch_size; ++b) {
        Phase: for (int phase = 0; phase <= N_TILES; ++phase) {
            const int load_bank = phase & 1;
            const int compute_bank = load_bank ^ 1;
            const bool load_enabled = phase < N_TILES;
            const bool compute_enabled = phase > 0;
            int ni = 0, ri = 0, ci = 0;
            int mw = 0, nw = 0, iw = 0, jw = 0;
            int mc = 0, rc = 0, cc = 0;
            int inputs_loaded = 0, weights_loaded = 0, outputs_done = 0;
            bool input_done = !load_enabled;
            bool weight_done = !load_enabled;
            bool compute_done = !compute_enabled;

            // Three independent progress counters. Empty loader FIFOs must not
            // freeze computation on the previous tile.
            // Keep wide counter comparisons out of the loop-exit/FIFO-enable
            // path. Each completion flag is latched by its own final transfer.
            Step: while (!(input_done && weight_done && compute_done)) {
#pragma HLS PIPELINE II=1
                // Each active compute iteration advances outputs_done exactly
                // once. Iterations after OUTPUT_WORDS do not access BufO.
                // Thus no BufO address is revisited within this Step loop.
                // Keep the read-before-write dependence WITHIN an iteration;
                // remove only the false carried dependence BETWEEN iterations.
                // Phase is sequential and drains Step before resetting counters.
#pragma HLS DEPENDENCE variable=BufO inter false
                cnndata_t value;
                if (!input_done && inputs.read_nb(value)) {
                    BufI[load_bank][ni][ri][ci] = value;
                    input_done = inputs_loaded == INPUT_WORDS - 1;
                    ++inputs_loaded;
                    if (++ci == C_IFM) {
                        ci = 0;
                        if (++ri == R_IFM) { ri = 0; ++ni; }
                    }
                }
                if (!weight_done && weights.read_nb(value)) {
                    BufW[load_bank][mw][nw][iw][jw] = value;
                    weight_done = weights_loaded == WEIGHT_WORDS - 1;
                    ++weights_loaded;
                    if (++jw == K_WTS) {
                        jw = 0;
                        if (++iw == K_WTS) {
                            iw = 0;
                            if (++nw == TN) { nw = 0; ++mw; }
                        }
                    }
                }
                if (!compute_done) {
                    cnndata_t tile_sum = 0;
                    InputChannels: for (int n = 0; n < TN; ++n) {
#pragma HLS UNROLL
                        FilterRows: for (int i = 0; i < K_WTS; ++i) {
#pragma HLS UNROLL
                            FilterCols: for (int j = 0; j < K_WTS; ++j) {
#pragma HLS UNROLL
                                tile_sum += BufW[compute_bank][mc][n][i][j] *
                                       BufI[compute_bank][n][rc + i][cc + j];
                            }
                        }
                    }
                    const cnndata_t previous = phase == 1 ? 0 : BufO[outputs_done];
                    const cnndata_t acc = previous + tile_sum;
                    // Final results leave in the packed [row][col][channel]
                    // order. No separate zeroing or output-buffer sweep.
                    if (phase == N_TILES)
                        results.write(acc);
                    else
                        BufO[outputs_done] = acc;
                    compute_done = outputs_done == OUTPUT_WORDS - 1;
                    ++outputs_done;
                    if (++mc == TM) {
                        mc = 0;
                        if (++cc == TC) { cc = 0; ++rc; }
                    }
                }
            }
            // Both loading and compute are finished before either bank is reused.
        }
    }
}

void write_outputs(hls::stream<cnndata_t>& fifo, cnndata_t* output,
                   unsigned long batch_size) {
#pragma HLS INLINE off
    for (unsigned long b = 0; b < batch_size; ++b) {
        const unsigned long base = b * OUTPUT_WORDS;
        OutputBurst: for (int p = 0; p < OUTPUT_WORDS; ++p) {
#pragma HLS PIPELINE II=1
            output[base + p] = fifo.read();
        }
    }
}
} // namespace

extern "C" void krnl_cnn(const cnndata_t* input, const cnndata_t* weights,
                          cnndata_t* output, unsigned long batch_size) {
// Separate adapters let DATAFLOW processes issue independent transactions.
// They may still share physical DDR; that mapping belongs in the link config.
#pragma HLS INTERFACE m_axi port=input offset=slave bundle=gmem_input depth=432000 max_read_burst_length=64 num_read_outstanding=4
#pragma HLS INTERFACE m_axi port=weights offset=slave bundle=gmem_weights depth=221184 max_read_burst_length=64 num_read_outstanding=4
#pragma HLS INTERFACE m_axi port=output offset=slave bundle=gmem_output depth=216320 max_write_burst_length=64 num_write_outstanding=4
#pragma HLS INTERFACE s_axilite port=input bundle=control
#pragma HLS INTERFACE s_axilite port=weights bundle=control
#pragma HLS INTERFACE s_axilite port=output bundle=control
#pragma HLS INTERFACE s_axilite port=batch_size bundle=control
#pragma HLS INTERFACE s_axilite port=return bundle=control
    hls::stream<cnndata_t> input_fifo("input_fifo");
    hls::stream<cnndata_t> weight_fifo("weight_fifo");
    hls::stream<cnndata_t> output_fifo("output_fifo");
#pragma HLS STREAM variable=input_fifo depth=256
#pragma HLS STREAM variable=weight_fifo depth=256
#pragma HLS STREAM variable=output_fifo depth=256
#pragma HLS BIND_STORAGE variable=input_fifo type=fifo impl=bram
#pragma HLS BIND_STORAGE variable=weight_fifo type=fifo impl=bram
#pragma HLS BIND_STORAGE variable=output_fifo type=fifo impl=bram
#ifdef CNN_NATIVE_TEST
    // Bounded concurrent software harness, never compiled into the FPGA.
    std::thread in_thread(load_inputs, input, std::ref(input_fifo), batch_size);
    std::thread weight_thread(load_weights, weights, std::ref(weight_fifo), batch_size);
    std::thread out_thread(write_outputs, std::ref(output_fifo), output, batch_size);
    compute_tiles(input_fifo, weight_fifo, output_fifo, batch_size);
    in_thread.join(); weight_thread.join(); out_thread.join();
#else
#pragma HLS DATAFLOW
    load_inputs(input, input_fifo, batch_size);
    load_weights(weights, weight_fifo, batch_size);
    compute_tiles(input_fifo, weight_fifo, output_fifo, batch_size);
    write_outputs(output_fifo, output, batch_size);
#endif
}
