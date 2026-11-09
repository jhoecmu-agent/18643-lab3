#ifndef KERNEL643_H
#define KERNEL643_H

/****************************************************************
 * Copyright (c) 2020~2021, 18-643 Course Staff, CMU
 * All rights reserved.
 ****************************************************************/

/*
 * CMU 18643 Lab 3
 *
 * The parameters in this file sets the CNN kernel on FPGA
 *
 * You can edit this file
 */
#include "instance643.h"

typedef unsigned int index_t;

#if 1
// Blocked configuration:
// TR=13, TC=13 covers the full 13x13 spatial plane.
// TM=16 (divides M_OFM=128) and TN=4 (divides N_IFM=192) are powers of 2,
// instantiating 16 * 4 = 64 parallel MACs/cycle (192 DSP48E2 slices).
#define TR (13) // output row
#define TC (13) // output column
#define TM (16) // output depth
#define TN (4)  // input depth
#else
// flat
#define TR R_OFM // output row
#define TC C_OFM // output column
#define TM M_OFM // output depth
#define TN N_IFM // input depth
#endif

/*
 * Access macros for the input, output, and weight buffers in DRAM.
 * - ARRAYi and ARRAYo use natural layout [B][N/M][R][C], which is 100%
 *   contiguous for full spatial planes (TR=R_OFM, TC=C_OFM) and preserves
 *   host verify() compatibility.
 * - ARRAYw uses tiled layout [M/TM][N/TN][TM][TN][K][K] so every weight tile
 *   is read in a single linear AXI burst; since TM=16 and TN=4 are powers of 2,
 *   division and modulo compile to simple bit-shifts and masks.
 */
#define ARRAYi(ptr,iB,iN,iR,iC,dB,dN,dR,dC) \
  ((ptr)[(iB)*(dN)*(dR)*(dC) + (iN)*(dR)*(dC) + (iR)*(dC) + (iC)])

#define ARRAYo(ptr,iB,iM,iR,iC,dB,dM,dR,dC) \
  ((ptr)[(iB)*(dM)*(dR)*(dC) + (iM)*(dR)*(dC) + (iR)*(dC) + (iC)])

#define ARRAYw(ptr,iM,iN,iR,iC,dM,dN,dR,dC) \
  ((ptr)[((iM)/TM)*((dN)*(dR)*(dC)*TM) + \
         ((iN)/TN)*(TM*TN*(dR)*(dC)) + \
         ((iM)%TM)*(TN*(dR)*(dC)) + \
         ((iN)%TN)*((dR)*(dC)) + \
         (iR)*(dC) + (iC)])

#endif