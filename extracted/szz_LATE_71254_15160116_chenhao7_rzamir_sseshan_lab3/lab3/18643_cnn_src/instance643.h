#ifndef INSTANCE643_H
#define INSTANCE643_H
typedef int cnndata_t;

static inline int nearlyEqual(cnndata_t a, cnndata_t b) { return a==b; }

#define BATCH_SIZE 10

#if 1
/* 
 * weights parameters
 */
#define K_WTS (3) // weight width and height (square)
                   // same depth as output
#define S_WTS (1) // sliding stride

/* 
 * output feature map paramters
 */
#define R_OFM (13) // height
#define C_OFM (13) // width
#define M_OFM (128) // depth

/*
 * input feature map paramters
 */
#define N_IFM (192) // depth

#else
// alt define for testing

#define K_WTS (4) // weight width and height (square)
#define S_WTS (1) // sliding stride

#define R_OFM (16) // height
#define C_OFM (16) // width
#define M_OFM (128) // depth

#define N_IFM (256) // depth

#endif

#define R_IFM (R_OFM*S_WTS+K_WTS-S_WTS) // derived height
#define C_IFM (C_OFM*S_WTS+K_WTS-S_WTS) // derived width

#endif
