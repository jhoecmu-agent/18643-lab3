/****************************************************************
 * CMU 18643 Lab 3: Optimized Blocked CNN Layer Implementation
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

  // On-chip buffers:
  // BufI caches the entire single-image input feature map across all N_IFM channels
  // (192 * 15 * 15 = 43,200 ints = 172.8 KB -> 88 BRAM_18K blocks) so each input
  // image is read from DRAM only ONCE.
  cnndata_t BufI[N_IFM / TN][TN][TR * S_WTS + K_WTS - 1][TC * S_WTS + K_WTS - 1];
#pragma HLS ARRAY_PARTITION variable=BufI complete dim=2

  cnndata_t BufO[TM][TR][TC];
#pragma HLS ARRAY_PARTITION variable=BufO complete dim=1

  cnndata_t BufW[TM][TN][K_WTS][K_WTS];
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW complete dim=2

  Batch_Loop: for (index_t iter = 0; iter < batch_size; iter++) {

    // 1. Burst-load the entire image's Input Feature Map once per batch item
    Load_I_Tile: for (index_t ti_tile = 0; ti_tile < N_IFM / TN; ti_tile++) {
      Load_I_Ch: for (index_t iii = 0; iii < TN; iii++) {
        Load_I_Row: for (index_t irr = 0; irr < R_IFM; irr++) {
          Load_I_Col: for (index_t icc = 0; icc < C_IFM; icc++) {
#pragma HLS PIPELINE II=1
            index_t tii = ti_tile * TN + iii;
            BufI[ti_tile][iii][irr][icc] =
              ARRAYi(input, iter, tii, irr, icc, BATCH_SIZE, N_IFM, R_IFM, C_IFM);
          }
        }
      }
    }

    Row_Tile_Loop: for (index_t row = 0; row < R_OFM; row += TR) {
      Col_Tile_Loop: for (index_t col = 0; col < C_OFM; col += TC) {
        To_Tile_Loop: for (index_t to = 0; to < M_OFM; to += TM) {

          // 2. Zero BufO in 169 cycles (all TM banks cleared in parallel)
          Zero_O_Row: for (index_t irr = 0; irr < TR; irr++) {
            Zero_O_Col: for (index_t icc = 0; icc < TC; icc++) {
#pragma HLS PIPELINE II=1
              Zero_O_Ch: for (index_t ioo = 0; ioo < TM; ioo++) {
#pragma HLS UNROLL
                BufO[ioo][irr][icc] = 0;
              }
            }
          }

          // 3. Iterate over input channel tiles using the on-chip BufI
          Ti_Tile_Loop: for (index_t ti_tile = 0; ti_tile < N_IFM / TN; ti_tile++) {
            index_t ti = ti_tile * TN;

            // Burst-load active weight tile (576 contiguous words)
            Load_W_To: for (index_t ioo = 0; ioo < TM; ioo++) {
              Load_W_Ti: for (index_t iii = 0; iii < TN; iii++) {
                Load_W_Row: for (index_t irr = 0; irr < K_WTS; irr++) {
                  Load_W_Col: for (index_t icc = 0; icc < K_WTS; icc++) {
#pragma HLS PIPELINE II=1
                    index_t too = to + ioo;
                    index_t tii = ti + iii;
                    BufW[ioo][iii][irr][icc] =
                      ARRAYw(weights, too, tii, irr, icc, M_OFM, N_IFM, K_WTS, K_WTS);
                  }
                }
              }
            }

            // Execute blocked convolution on the cached input tile
            cnn_blocked_kernel(BufI[ti_tile], BufO, BufW);
          }

          // 4. Burst-write finished output feature map tile (2,704 contiguous words)
          Store_O_Ch: for (index_t ioo = 0; ioo < TM; ioo++) {
            Store_O_Row: for (index_t irr = 0; irr < TR; irr++) {
              Store_O_Col: for (index_t icc = 0; icc < TC; icc++) {
#pragma HLS PIPELINE II=1
                index_t too = to + ioo;
                index_t trr = row + irr;
                index_t tcc = col + icc;
                ARRAYo(output, iter, too, trr, tcc, BATCH_SIZE, M_OFM, R_OFM, C_OFM) =
                  BufO[ioo][irr][icc];
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
#pragma HLS INLINE
  Krow: for (index_t i = 0; i < K_WTS; i++) {
    Kcol: for (index_t j = 0; j < K_WTS; j++) {
      Row: for (index_t row_b = 0; row_b < TR; row_b++) {
        Col: for (index_t col_b = 0; col_b < TC; col_b++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=BufO inter false
          To: for (index_t to_b = 0; to_b < TM; to_b++) {
#pragma HLS UNROLL
            cnndata_t sum = 0;
            Ti: for (index_t ti_b = 0; ti_b < TN; ti_b++) {
#pragma HLS UNROLL
              sum += BufW[to_b][ti_b][i][j] *
                     BufI[ti_b][S_WTS * row_b + i][S_WTS * col_b + j];
            }
            BufO[to_b][row_b][col_b] += sum;
          }
        }
      }
    }
  }
}