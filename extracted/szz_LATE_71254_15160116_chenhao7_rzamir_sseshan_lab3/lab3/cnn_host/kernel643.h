#ifndef KERNEL643_H
#define KERNEL643_H
#include "instance643.h"

typedef unsigned int index_t;

#if 1
// blocked
#define TR R_OFM // output row
#define TC C_OFM // output column
#define TM (7) // output depth
#define TN (16) // input depth
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
// Weight layout: [N/TN][M][TN][K][K]. Each loaded weight tile is contiguous.
// No channel padding is allocated by the host.
#if (N_IFM % TN) != 0
#error "Blocked weight layout requires N_IFM to be divisible by TN."
#endif
#define ARRAYw(ptr,iM,iN,iR,iC,dM,dN,dR,dC) ((ptr)[((iN)/TN)*(dM)*TN*(dR)*(dC)+(iM)*TN*(dR)*(dC)+((iN)%TN)*(dR)*(dC)+(iR)*(dC)+(iC)])

#endif
