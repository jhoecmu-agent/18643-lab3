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
#define TR R_OFM // output row    (full 13x13 tile)
#define TC C_OFM // output column
#define TM (8) // output depth  (parallel output channels)
#define TN (12) // input depth   (parallel input channels)
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
// Input groups, four ints per read
#define INDEXi(iB,iN,iR,iC,dN,dR,dC) (((((iB)*((dN)/TN)+(iN)/TN)*(dR)+(iR))*(dC)+(iC))*TN+(iN)%TN)
#define ARRAYi(ptr,iB,iN,iR,iC,dB,dN,dR,dC) ((ptr)[INDEXi(iB,iN,iR,iC,dN,dR,dC)])
// Eight output channels per write
#define ARRAYo(ptr,iB,iM,iR,iC,dB,dM,dR,dC) ((ptr)[(((((iB)*((dM)/TM)+(iM)/TM)*(dR)+(iR))*(dC)+(iC))*TM+(iM)%TM)])
// Eight outputs per weight word
#define ARRAYw(ptr,iM,iN,iR,iC,dM,dN,dR,dC) ((ptr)[((((((iN)/TN)*((dM)/TM)+(iM)/TM)*TN+(iN)%TN)*(dR)+(iR))*(dC)+(iC))*TM+(iM)%TM])

#endif
