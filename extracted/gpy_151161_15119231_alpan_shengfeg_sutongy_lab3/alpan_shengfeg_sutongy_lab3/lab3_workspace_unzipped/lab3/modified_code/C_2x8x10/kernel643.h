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
/*
 * Summary of modifications (Lab 3)
 *
 * 1. Tile sizes TM,TN set to 8,10, with two engines (see below).
 * 2. ARRAYw and ARRAYo use a tiled layout so that krnl_cnn() reads
 *    the weights and writes the outputs strictly sequentially, in
 *    the order the datapath consumes/produces them (long bursts).
 *    ARRAYi keeps the natural layout, which is already in order.
 */
#include "instance643.h"

typedef unsigned int index_t;

#if 1
// blocked
/*
 * Tile sizes.
 *
 * Two engines compute two images at once from the same weights, so
 * CNN_ENGINES*TM*TN = 160 parallel multipliers, 2 DSPs each (see
 * mul32lo() in krnl_cnn.cpp), so 320 of 360 DSPs.
 * TM=8 divides M_OFM=128 exactly; TN=10 covers N_IFM=192 in 20 tiles
 * (200 slots, 96% useful).
 *
 * TR and TC cover the whole 13x13 output feature map, so a whole
 * input image is kept on chip and each image is read only once.
 */
#define TR (13) // output row
#define TC (13) // output column
#define TM (8) // output depth
#define TN (10) // input depth
#define CNN_ENGINES (2) // images computed at once
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
 *
 * ARRAYi: natural layout [image][channel][row][col].
 *
 * ARRAYw: [to-tile][ti][to%TM][krow][kcol]. All weights of one
 *   (to-tile, ti-tile) block are contiguous, and the blocks are
 *   stored in the order krnl_cnn() uses them (to-tile major).
 *
 * ARRAYo: [image/2][to-tile][row][col][image%2][to%TM]. The outputs
 *   of the two images of a group are interleaved, in the order the
 *   two engines produce them. Requires M_OFM % TM == 0. The image
 *   term stays additive because verify() in cnn.cpp offsets by image
 *   and by (to,row,col) separately.
 */
#define ARRAYi(ptr,iB,iN,iR,iC,dB,dN,dR,dC) ((ptr)[(iB)*(dN)*(dR)*(dC)+(iN)*(dR)*(dC)+(iR)*(dC)+(iC)])
#define ARRAYo(ptr,iB,iM,iR,iC,dB,dM,dR,dC) ((ptr)[((iB)/CNN_ENGINES)*CNN_ENGINES*(dM)*(dR)*(dC)+((iM)/TM)*CNN_ENGINES*TM*(dR)*(dC)+((iR)*(dC)+(iC))*CNN_ENGINES*TM+((iB)%CNN_ENGINES)*TM+((iM)%TM)])
#define ARRAYw(ptr,iM,iN,iR,iC,dM,dN,dR,dC) ((ptr)[(((iM)/TM)*(dN)+(iN))*TM*(dR)*(dC)+((iM)%TM)*(dR)*(dC)+(iR)*(dC)+(iC)])

#endif
