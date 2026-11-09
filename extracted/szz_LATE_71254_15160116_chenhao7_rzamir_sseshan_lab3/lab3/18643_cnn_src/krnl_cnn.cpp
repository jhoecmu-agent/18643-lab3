#include "util643.h"
#include "instance643.h"
#include "kernel643.h"

void cnn_blocked_kernel(cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1], cnndata_t BufO[TM][TR][TC], cnndata_t BufW[TM][TN][K_WTS][K_WTS]);


// initialize BufO to zero
void init_BufO(cnndata_t BufO[TM][TR][TC]) {
  index_t ioo, irr, icc;
  for (ioo=0; ioo<TM; ioo++) {
    for(irr=0; irr<TR; irr++) {
      for(icc=0; icc<TC; icc++) {
#pragma HLS PIPELINE II=1
        BufO[ioo][irr][icc]=0;
      }
    }
  }
}


// Copy one input tile from DRAM into the on-chip buffer.
void load_BufI(const cnndata_t* input,cnndata_t BufI[TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1], index_t iter, index_t row, index_t col, index_t ti) {
#if (TR == R_OFM) && (TC == C_OFM) && (S_WTS == 1)
  // channels sit next to each other in memory, so we can read max of TN in one loop.
  const index_t count = MIN(TN, N_IFM-ti)*R_IFM*C_IFM;
  // Start at the first value of channel ti in image iter.
  const cnndata_t* tile = &ARRAYi(input,iter,ti,row,col,0,N_IFM,R_IFM,C_IFM);
  for(index_t idx=0; idx<count; idx++) {
#pragma HLS PIPELINE II=1
    // Convert the flat index to a position in BufI: channel, row, and column.
    const index_t iii = idx/(R_IFM*C_IFM);
    const index_t irr = (idx/C_IFM)%R_IFM;
    const index_t icc = idx%C_IFM;
    BufI[iii][irr][icc]=tile[idx];
  }
#else
  index_t irr, icc, iii, xrr, xcc, tii;

  for(tii=ti,iii=0; tii<MIN(ti+TN,N_IFM); tii++,iii++) {
    for(xrr=row*S_WTS,irr=0; xrr<(MIN(row+TR,R_OFM)*S_WTS+K_WTS-1); xrr++,irr++) {
      for(xcc=col*S_WTS,icc=0; xcc<(MIN(col+TC,C_OFM)*S_WTS+K_WTS-1); xcc++,icc++) {
#pragma HLS PIPELINE II=1
        BufI[iii][irr][icc]=ARRAYi(input,iter,tii,xrr,xcc,0,N_IFM,R_IFM,C_IFM);
      }
    }
  }
#endif
}


// Copy one weight tile from DRAM into the on-chip buffer.
void load_BufW(const cnndata_t* weights,cnndata_t BufW[TM][TN][K_WTS][K_WTS], index_t to, index_t ti) {
  // The last tile may have fewer than TM output channels.
  const index_t count = MIN(TM, M_OFM-to)*TN*K_WTS*K_WTS;
  // Start at the first kernel weight for output channel to and input channel ti.
  const cnndata_t* tile = &ARRAYw(weights,to,ti,0,0,M_OFM,N_IFM,K_WTS,K_WTS);

  for(index_t idx=0; idx<count; idx++) {
#pragma HLS PIPELINE II=1
    const index_t ioo = idx/(TN*K_WTS*K_WTS);
    const index_t iii = (idx/(K_WTS*K_WTS))%TN;
    const index_t irr = (idx/K_WTS)%K_WTS;
    const index_t icc = idx%K_WTS;
    BufW[ioo][iii][irr][icc]=tile[idx];
  }
}

// Prepare one group weights for the compute.
void load_group_weights(const cnndata_t* weights, cnndata_t tileW[TM][TN][K_WTS][K_WTS], index_t to, index_t ti, int group) {
#pragma HLS INLINE off
  // group 0 starts at output channel to.
  const index_t channel = to+group*TM;
  load_BufW(weights, tileW, channel, ti);
#if (M_OFM % TM) != 0
  // last group is short fill unused output slots with 0.
  for(index_t m=MIN(TM, M_OFM-channel); m<TM; m++) {
    for(index_t n=0; n<TN; n++) {
      for(index_t r=0; r<K_WTS; r++) {
        for(index_t c=0; c<K_WTS; c++) {
#pragma HLS PIPELINE II=1
          tileW[m][n][r][c]=0;
        }
      }
    }
  }
#endif
}

void compute_output_group(cnndata_t BufI[][TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1], cnndata_t BufO[][TM][TR][TC], cnndata_t tileW[TM][TN][K_WTS][K_WTS], int group, int batches, int output_tiles) {
#pragma HLS INLINE off
#pragma HLS ALLOCATION function instances=cnn_blocked_kernel limit=1
  // Compute processes the images in turn, using the same weights.
  for(int batch=0; batch<batches; batch++) {
    // batch*output_tiles gives image first slot in BufO. Group is to select the tile.
    cnn_blocked_kernel(BufI[batch], BufO[batch*output_tiles+group], tileW);
  }
}

void process_output_groups(const cnndata_t* weights, cnndata_t BufI[][TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1], cnndata_t BufO[][TM][TR][TC], index_t to, index_t ti, int groups, int batches, int output_tiles) {
#pragma HLS INLINE off
  for(int group=0; group<groups; group++) {
#pragma HLS DATAFLOW
    // compute reads BufI and updates the partial sums in BufO.
#pragma HLS STABLE variable=BufI
#pragma HLS STABLE variable=BufO
    // Two weight slots and fill one for the next group compute reads the other.
    cnndata_t tileW[TM][TN][K_WTS][K_WTS];
#pragma HLS STREAM variable=tileW type=pipo depth=2
    load_group_weights(weights, tileW, to, ti, group);
    compute_output_group(BufI, BufO, tileW, group, batches, output_tiles);
  }
}


// Copy one finished output tile from BufO back to DRAM.
void store_BufO(cnndata_t BufO[TM][TR][TC], cnndata_t* output, index_t iter, index_t row, index_t col, index_t to) {
#if (TR == R_OFM) && (TC == C_OFM)
  // Full output planes sit next to each other in memory. Write only the channels that exist.
  const index_t count = MIN(TM, M_OFM-to)*R_OFM*C_OFM;
  cnndata_t* tile = &ARRAYo(output,iter,to,row,col,0,M_OFM,R_OFM,C_OFM);
  for(index_t idx=0; idx<count; idx++) {
#pragma HLS PIPELINE II=1
    // Split idx into the channel, row, and column to read from BufO.
    const index_t ioo = idx/(R_OFM*C_OFM);
    const index_t irr = (idx/C_OFM)%R_OFM;
    const index_t icc = idx%C_OFM;
    tile[idx]=BufO[ioo][irr][icc];
  }
#else
  index_t ioo, irr, icc, trr, tcc, too;
  for(too=to,ioo=0; too<MIN(to+TM,M_OFM); too++,ioo++) {
    for(trr=row,irr=0; trr<MIN(row+TR,R_OFM); trr++,irr++) {
      for(tcc=col,icc=0; tcc<MIN(col+TC,C_OFM); tcc++,icc++) {
#pragma HLS PIPELINE II=1
        ARRAYo(output,iter,too,trr,tcc,0,M_OFM,R_OFM,C_OFM)=BufO[ioo][irr][icc];
      }
    }
  }
#endif
}

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
  // image input tile is reused across output_tiles output groups
  const index_t output_tiles = 19;
  // process up to this many images with the same weights
  const index_t batch_tiles = 5;

  // Each image has one input slot in BufI and one partial sum slot per output group in BufO.
  cnndata_t BufI[batch_tiles][TN][TR*S_WTS+K_WTS-1][TC*S_WTS+K_WTS-1];
  cnndata_t BufO[batch_tiles*output_tiles][TM][TR][TC];

  for(iter = 0; iter < batch_size; iter+=batch_tiles) {
    const index_t batches = MIN(batch_tiles, batch_size-iter);
    // pick the output tile top left corner (row, col) and first output channel (to).
    for(row=0; row<R_OFM; row+=TR) {
      for(col=0; col<C_OFM; col+=TC) {
        for(to=0; to<M_OFM; to+=output_tiles*TM) {
          const index_t groups = MIN(output_tiles, (M_OFM-to+TM-1)/TM);
          // init each output tile to 0.
          for(index_t batch=0; batch<batches; batch++) {
            for(index_t group=0; group<groups; group++) {
              init_BufO(BufO[batch*output_tiles+group]);
            }
          }

          for(ti=0; ti<N_IFM; ti+=TN) {
            // load the next TN input channels for each image
            for(index_t batch=0; batch<batches; batch++) {
              load_BufI(input,BufI[batch],iter+batch,row,col,ti);
            }
            // add channel contributions to output sums
            process_output_groups(weights, BufI, BufO, to, ti, groups, batches, output_tiles);
          }

          // all input channels done. Write the finished result.
          for(index_t batch=0; batch<batches; batch++) {
            for(index_t group=0; group<groups; group++) {
              store_BufO(BufO[batch*output_tiles+group], output, iter+batch, row, col, to+group*TM);
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
#pragma HLS INLINE off
  cnndata_t bufO[TM][TR][TC];
  cnndata_t tmp[TN];
  cnndata_t accumulator;
  index_t i, j, row_b, col_b, to_b, ti_b; 
  bool first_step;
  // Request single-port memory for inputs and weights, and separate read/write ports for BufO.
#pragma HLS INTERFACE mode=ap_memory port=BufI storage_type=ram_1p
// we are allowed to use ram_s2p here right?> I want it since lab2
#pragma HLS INTERFACE mode=ap_memory port=BufO storage_type=ram_s2p
#pragma HLS INTERFACE mode=ap_memory port=BufW storage_type=ram_1p
  // Split the buffers by channel so different channels can be accessed in parallel.
#pragma HLS ARRAY_PARTITION variable=BufI type=complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW type=complete dim=1
#pragma HLS ARRAY_PARTITION variable=BufW type=complete dim=2
#pragma HLS ARRAY_PARTITION variable=BufO type=complete dim=1
#pragma HLS ARRAY_PARTITION variable=bufO type=complete dim=1
  // Keep the local running sums in block RAM with one read port and one write port.
#pragma HLS BIND_STORAGE variable=bufO type=ram_s2p impl=bram
#pragma HLS ARRAY_PARTITION variable=tmp type=complete

  // For each filter position (i, j), visit every output pixel in the tile.
  Krow: for (i = 0; i < K_WTS; i++) {
    Kcol: for (j = 0; j < K_WTS; j++) {
#pragma HLS loop_flatten off
      Row: for (row_b = 0; row_b < TR; row_b++) {
        // Aim to start the next output column every clock cycle.
        Col: for (col_b = 0; col_b < TC; col_b++) {
#pragma HLS loop_flatten off
#pragma HLS PIPELINE II=1
          first_step = (i == 0 && j == 0);
          // Read one value per input channel. Every output channel reuses these values.
          Ld: for (ti_b = 0; ti_b < TN; ti_b++) {
            tmp[ti_b] = BufI[ti_b][S_WTS * row_b + i][S_WTS * col_b + j];
          }
          To: for (to_b = 0; to_b < TM; to_b++) {
            // Start from BufO (zero or a sum from earlier input tiles).
            // Later filter positions continue from the local sum in bufO.
            if (first_step == true) {
              accumulator = BufO[to_b][row_b][col_b];
            } else {
              accumulator = bufO[to_b][row_b][col_b];
            }
            // Add each input value times its weight to this output channel's sum.
            Ti: for (ti_b = 0; ti_b < TN; ti_b++) {
              accumulator += BufW[to_b][ti_b][i][j] * tmp[ti_b];
            }
            // Keep the sum local until the last filter position, then write it back to BufO.
            if (i == K_WTS-1 && j == K_WTS-1) {
              BufO[to_b][row_b][col_b] = accumulator;
            } else {
              bufO[to_b][row_b][col_b] = accumulator;
            }
          }
        }
      }
    }
  }
}
