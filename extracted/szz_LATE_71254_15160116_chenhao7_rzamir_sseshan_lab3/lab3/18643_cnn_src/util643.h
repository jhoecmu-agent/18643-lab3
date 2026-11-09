#ifndef UTIL643_H
#define UTIL643_H

// #define __VITIS_CL__     // comment out for Lab 2; keep for Lab 3

typedef unsigned long uint64_t;

#define VRANGE 256

// 4D array in natural layout
#define ARRAY4(ptr,iB,iN,iR,iC,dB,dN,dR,dC) ((ptr)[(iB)*(dN)*(dR)*(dC)+(iN)*(dR)*(dC)+(iR)*(dC)+(iC)])

#define MIN(a,b) (((a)<(b))?(a):(b))

#endif
