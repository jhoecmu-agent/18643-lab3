#ifndef KERNEL643_H
#define KERNEL643_H

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
 * The parameters in this file sets the CNN kernel on FPGA
 *
 * You can edit this file
 */
#include "instance643.h"

typedef unsigned int index_t;

/*
 * Tile: one tile is the whole 13 x 13 output map (TR = R_OFM, TC = C_OFM),
 * so every input word loaded on chip is used for the whole layer.  TM x TN
 * multiply-adds run per clock; TM must divide M_OFM and TN must divide N_IFM
 * (no partial tiles, no padding), and both must be multiples of VEC.
 * The / and % in the layout macros are evaluated only once per block (the
 * kernel indexes inside a block relative to its start), so TN need not be a
 * power of two.
 */
#define TR R_OFM // output row
#define TC C_OFM // output column
#define TM (8)   // output depth
#define TN (24)  // input depth

// Each multiply-add uses one DSP for its main piece and one more for its
// "mid" piece, except the first N_LUT_MULS (in tm, tn order), whose mid
// piece is built from LUTs: 2 x 192 - N_LUT_MULS DSPs for TM x TN = 192
#define N_LUT_MULS (34)

// 32-bit words moved per DRAM access in the load/store loops (128-bit bus)
#define VEC (4)

/*
 * DRAM layouts.  The host fills and checks the buffers through these macros,
 * so they decide the order of words in DRAM.  Each is chosen so that the
 * kernel reads or writes every buffer front to back in long bursts:
 *
 *   ARRAYi  per image [r][c][n]: all input maps of a pixel are adjacent, so
 *           VEC consecutive words go to VEC different input-map banks.
 *   ARRAYw  per (to, ti) block of TM x TN maps, [i][j][tm][tn]: one block is
 *           one contiguous run, read once per image in kernel order.
 *   ARRAYo  per image [m/TM][r][c][m%TM]: one block of TM output maps is one
 *           contiguous run, written once.
 *
 * The batch index stays the outermost stride of ARRAYi and ARRAYo, and dB
 * is never used: cnn.cpp's verify() indexes the output of image b as
 * &ARRAYo(out, b, 0,0,0, ...) followed by ARRAYo(base, 0, m, r, c, 0, ...).
 */
#define ARRAYi(ptr,iB,iN,iR,iC,dB,dN,dR,dC) \
  ((ptr)[(iB)*(dN)*(dR)*(dC)+((iR)*(dC)+(iC))*(dN)+(iN)])
#define ARRAYo(ptr,iB,iM,iR,iC,dB,dM,dR,dC) \
  ((ptr)[(iB)*(dM)*(dR)*(dC)+(((iM)/TM)*(dR)*(dC)+(iR)*(dC)+(iC))*TM+(iM)%TM])
#define ARRAYw(ptr,iM,iN,iR,iC,dM,dN,dR,dC) \
  ((ptr)[((((iM)/TM)*((dN)/TN)+(iN)/TN)*(dR)*(dC)+(iR)*(dC)+(iC))*TM*TN \
         +((iM)%TM)*TN+(iN)%TN])

static_assert(TR == R_OFM && TC == C_OFM, "the kernel assumes one tile covers the output map");
static_assert(M_OFM % TM == 0 && N_IFM % TN == 0, "TM must divide M_OFM, TN must divide N_IFM");
static_assert(TM % VEC == 0 && TN % VEC == 0 && N_IFM % VEC == 0, "VEC must divide TM, TN, N_IFM");

#endif
