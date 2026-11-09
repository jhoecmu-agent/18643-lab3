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
#define TR (13) // output row
#define TC (13) // output column
#define TM (128) // output depth
#define TN (14) // input depth (the last ti block has N_IFM%TN channels, zero padded)
#define TU (8) // output depth computed per cycle (TU*TN multipliers)
#else
// flat
#define TR R_OFM // output row
#define TC C_OFM // output column
#define TM M_OFM // output depth
#define TN N_IFM  // input depth
#endif

/*
 * Access macros for the input and output buffers.
 * You can change this from standard natural layout
 */
#define ARRAYi(ptr,iB,iN,iR,iC,dB,dN,dR,dC) ((ptr)[(iB)*(dN)*(dR)*(dC)+(iN)*(dR)*(dC)+(iR)*(dC)+(iC)])
#define ARRAYo(ptr,iB,iM,iR,iC,dB,dM,dR,dC) ((ptr)[(iB)*(dM)*(dR)*(dC)+(iM)*(dR)*(dC)+(iR)*(dC)+(iC)])
// width of the ti block holding input channel iN: TN, or dN%TN for a partial last block
#define TNW(iN,dN) ((iN)/(TN) < (dN)/(TN) ? (TN) : (dN)%(TN))
// layout [N/TN][M][TN][K][K]: the weights of one ti block are contiguous
// (a partial last block is [M][dN%TN][K][K], so the layout stays M*N*K*K words)
#define ARRAYw(ptr,iM,iN,iR,iC,dM,dN,dR,dC) ((ptr)[ \
    ((iN)/(TN)*(dM)*(TN)*(dR)*(dC)) \
    +(iM)*TNW(iN,dN)*(dR)*(dC) \
    +((iN)%(TN))*(dR)*(dC) \
    +(iR)*(dC) \
    +(iC) \
])

#endif
