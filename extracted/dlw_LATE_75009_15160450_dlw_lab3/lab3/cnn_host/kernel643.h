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

#if 1
// blocked
//
// Tile sizes:
//  - TR = TC = 13 covers the whole 13x13 output map in one call, so the
//    pipeline fill is paid once per 1521 iterations. 13 is prime, so a
//    smaller tile would waste cycles on out-of-range rows/cols.
//  - TM*TN = 112 parallel 32x32 multipliers at 3 DSPs each = 336 DSPs,
//    plus 1 DSP for an address multiply-add = 337. TN = 15 would need 361.
//  - TM = 8 divides M_OFM (128) exactly; TN = 14 needs 14 blocksN for
//    N_IFM (192), so only the last input block is zero-padded.
#define TR (13) // output row
#define TC (13) // output column
#define TM (8)  // output depth
#define TN (14) // input depth
#else
// flat
#define TR R_OFM // output row
#define TC C_OFM // output column
#define TM M_OFM // output depth
#define TN N_IFM  // input depth
#endif

// 1: BufO is read on the first kernel tap and written on the last one,
//    inside the single pipelined loop (no separate load pass).
#define BUFO_SINGLE_PASS 1

/*
 * Access macros for the input and output buffers.
 * You can change this from standard natural layout
 */
#define ARRAYi(ptr,iB,iN,iR,iC,dB,dN,dR,dC) ((ptr)[(iB)*(dN)*(dR)*(dC)+(iN)*(dR)*(dC)+(iR)*(dC)+(iC)])
// Blocked layouts so each transfer is one sequential burst.
//  ARRAYo [B][M/TM][R][C][TM]: one TM-channel output block is contiguous.
//  ARRAYw [M/TM][N][K][K][TM]: the (TM x TN x K x K) tile for (to, ti)
//         is contiguous from ARRAYw(w,to,ti,0,0).
//  ARRAYi is already contiguous.
#define ARRAYo(ptr,iB,iM,iR,iC,dB,dM,dR,dC) ((ptr)[((((iB)*((dM)/TM)+(iM)/TM)*(dR)+(iR))*(dC)+(iC))*TM+(iM)%TM])
#define ARRAYw(ptr,iM,iN,iR,iC,dM,dN,dR,dC) ((ptr)[((((iM)/TM)*(dN)+(iN))*(dR)+(iR))*(dC)*TM+(iC)*TM+(iM)%TM])

static_assert(M_OFM % TM == 0, "blocked ARRAYo/ARRAYw need M_OFM % TM == 0");
static_assert(TR == R_OFM && TC == C_OFM, "Lab 3 krnl_cnn() needs TR == R_OFM, TC == C_OFM");

#endif
