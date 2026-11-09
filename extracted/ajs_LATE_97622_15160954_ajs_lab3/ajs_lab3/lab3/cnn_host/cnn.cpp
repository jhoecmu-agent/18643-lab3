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
 * You cannot edit this file
 */

#define OCL_CHECK(error, call)						\
  call;									\
  if (error != CL_SUCCESS) {						\
    printf("%s:%d Error calling " #call ", error code is: %d\n", __FILE__, __LINE__, error); \
    exit(EXIT_FAILURE);							\
  }

#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <iostream>
#include <sys/time.h>
#include "util643.h"
#include "instance643.h"
#include "kernel643.h"
#ifdef __VITIS_CL__ // lab3
#include "cnn.h"
#endif

// Function prototypes
void ZhangIsfpga15_1_fp(cnndata_t *input, cnndata_t *output, cnndata_t *weights);
int verify(cnndata_t *ref, cnndata_t *checkit);
void print_params();

#ifndef __VITIS_CL__ // lab 2
void krnl_cnn(const cnndata_t* input, const cnndata_t* weights, cnndata_t* output, uint64_t batch_size);
#endif

int main(int argc, char* argv[]) {

  print_params();

  // Compute the size of array in bytes
  uint64_t num_elem_inputs = BATCH_SIZE * N_IFM * R_IFM * C_IFM;
  uint64_t num_elem_weights = M_OFM * N_IFM * K_WTS * K_WTS;
  uint64_t num_elem_outputs = BATCH_SIZE * M_OFM * R_OFM * C_OFM;

  printf("\n===== preparing to run ======\n\n");


#ifdef __VITIS_CL__	  // lab 3
  //TARGET_DEVICE macro needs to be passed from gcc command line
  if(argc != 2) {
    std::cout << "Usage: " << argv[0] <<" <xclbin>" << std::endl;
    return EXIT_FAILURE;
  }

  std::string xclbinFilename = argv[1];
    
  // Prepare platform, kernel and buffer
  // Xilinx vadd example boilerplate
  std::vector<cl::Device> devices;
  cl_int err;
  cl::Context context;
  cl::CommandQueue q;
  cl::Kernel krnl_blk_cnn;
  cl::Program program;
  std::vector<cl::Platform> platforms;
  bool found_device = false;

  //traversing all Platforms To find Xilinx Platform and targeted
  //Device in Xilinx Platform
  cl::Platform::get(&platforms);
  for(size_t i = 0; (i < platforms.size() ) & (found_device == false) ;i++){
    cl::Platform platform = platforms[i];
    std::string platformName = platform.getInfo<CL_PLATFORM_NAME>();
    if ( platformName == "Xilinx"){
      devices.clear();
      platform.getDevices(CL_DEVICE_TYPE_ACCELERATOR, &devices);
      if (devices.size()){
	found_device = true;
	break;
      }
    }
  }
  if (found_device == false){
    std::cout << "Error: Unable to find Target Device " << std::endl;
    return EXIT_FAILURE;
  }

  std::cout << "INFO: Reading " << xclbinFilename << std::endl;
  FILE* fp;
  if ((fp = fopen(xclbinFilename.c_str(), "r")) == nullptr) {
    printf("ERROR: %s xclbin not available please build\n", xclbinFilename.c_str());
    exit(EXIT_FAILURE);
  }
  // Load xclbin
  std::cout << "Loading: '" << xclbinFilename << "'\n";
  std::ifstream bin_file(xclbinFilename, std::ifstream::binary);
  bin_file.seekg (0, bin_file.end);
  unsigned nb = bin_file.tellg();
  bin_file.seekg (0, bin_file.beg);
  char *buf = new char [nb];
  bin_file.read(buf, nb);

  // Creating Program from Binary File
  cl::Program::Binaries bins;
  bins.push_back({buf,nb});
  bool valid_device = false;
  for (unsigned int i = 0; i < devices.size(); i++) {
    auto device = devices[i];
    // Creating Context and Command Queue for selected Device
    OCL_CHECK(err, context = cl::Context(device, nullptr, nullptr, nullptr, &err));
    OCL_CHECK(err, q = cl::CommandQueue(context, device, CL_QUEUE_PROFILING_ENABLE, &err));
    std::cout << "Trying to program device[" << i << "]: " << device.getInfo<CL_DEVICE_NAME>() << std::endl;
    cl::Program program(context, {device}, bins, nullptr, &err);
    if (err != CL_SUCCESS) {
      std::cout << "Failed to program device[" << i << "] with xclbin file!\n";
    } else {
      std::cout << "Device[" << i << "]: program successful!\n";
      OCL_CHECK(err, krnl_blk_cnn = cl::Kernel(program, "krnl_cnn", &err));
      valid_device = true;
      break; // we break because we found a valid device
    }
  }
  if (!valid_device) {
    std::cout << "Failed to program any device found, exit!\n";
    exit(EXIT_FAILURE);
  }
    
  // These commands will allocate memory on the Device. The cl::Buffer objects can
  // be used to reference the memory locations on the device. 
  OCL_CHECK(err, cl::Buffer buffer_input(context, CL_MEM_READ_ONLY, num_elem_inputs*sizeof(cnndata_t), NULL, &err));
  OCL_CHECK(err, cl::Buffer buffer_weights(context, CL_MEM_READ_ONLY, num_elem_weights*sizeof(cnndata_t), NULL, &err));
  OCL_CHECK(err, cl::Buffer buffer_output(context, CL_MEM_READ_WRITE, num_elem_outputs*sizeof(cnndata_t), NULL, &err));

  // set the kernel Arguments
  int narg=0;
  OCL_CHECK(err, err = krnl_blk_cnn.setArg(narg++,buffer_input));
  OCL_CHECK(err, err = krnl_blk_cnn.setArg(narg++,buffer_weights));
  OCL_CHECK(err, err = krnl_blk_cnn.setArg(narg++,buffer_output));
  OCL_CHECK(err, err = krnl_blk_cnn.setArg(narg++,(uint64_t)BATCH_SIZE));

  // We then need to map our OpenCL buffers to get the pointers
  cnndata_t* dt_weights;
  cnndata_t* dt_input;
  cnndata_t* dt_output;
  OCL_CHECK(err,
	    dt_input = (cnndata_t *) q.enqueueMapBuffer(buffer_input, CL_TRUE, CL_MAP_WRITE, 0, num_elem_inputs*sizeof(cnndata_t), NULL, NULL, &err));
  OCL_CHECK(err,
	    dt_output = (cnndata_t *) q.enqueueMapBuffer(buffer_output, CL_TRUE, CL_MAP_READ, 0, num_elem_outputs*sizeof(cnndata_t), NULL, NULL, &err));
  OCL_CHECK(err,
	    dt_weights = (cnndata_t *) q.enqueueMapBuffer(buffer_weights, CL_TRUE, CL_MAP_WRITE, 0, num_elem_weights*sizeof(cnndata_t), NULL, NULL, &err));
#else // lab 2
  // just allocate input and output buffers normally
  cnndata_t* dt_input                     = NULL;
  cnndata_t* dt_output                    = NULL;
  cnndata_t* dt_weights                   = NULL;

  if ((dt_output = (cnndata_t*)malloc(num_elem_outputs * sizeof(cnndata_t))) == NULL) {
    perror("Failed malloc of output matrix");
    exit(1);
  }
  if ((dt_input = (cnndata_t*)malloc(num_elem_inputs * sizeof(cnndata_t))) == NULL) {
    perror("Failed malloc of input matrix");
    exit(1);
  }
  if ((dt_weights = (cnndata_t*)malloc(num_elem_weights * sizeof(cnndata_t))) == NULL) {
    perror("Failed malloc of weights matrix");
    exit(1);
  }
#endif

  // Reference shadow buffers
  cnndata_t* ref_input                    = NULL;
  cnndata_t* ref_output                   = NULL;
  cnndata_t* ref_weights                  = NULL;

  if ((ref_output = (cnndata_t*)malloc(num_elem_outputs * sizeof(cnndata_t))) == NULL) {
    perror("Failed malloc of reference output matrix");
    exit(1);
  }
  if ((ref_input = (cnndata_t*)malloc(num_elem_inputs * sizeof(cnndata_t))) == NULL) {
    perror("Failed malloc of reference input matrix");
    exit(1);
  }
  if ((ref_weights = (cnndata_t*)malloc(num_elem_weights * sizeof(cnndata_t))) == NULL) {
    perror("Failed malloc of reference weights matrix");
    exit(1);
  }

  {
    uint64_t row, col, to, ti, iter;
    // initilaize buffers with test values

    // Set the actual output and reference output matrices to 0.
    for(iter=0;iter<BATCH_SIZE;iter++) {
      for(row = 0; row < R_OFM; row++) {
	for(col = 0; col < C_OFM ; col++) {
	  for(to = 0; to < M_OFM; to++) {
	    ARRAYo(dt_output, iter, to, row, col, BATCH_SIZE, M_OFM,
		   R_OFM, C_OFM) = 0;
	    ARRAY4(ref_output, iter, to, row, col, BATCH_SIZE, M_OFM,
		   R_OFM, C_OFM) = 0;
	  }
	}
      }
    }

    // Generate the input matrix
    for(iter=0;iter<BATCH_SIZE;iter++) {
      for(row = 0; row < R_IFM; row++) {
	for(col = 0; col < C_IFM ; col++) {
	  for(ti = 0; ti < N_IFM; ti++) {
	    cnndata_t val=rand()%VRANGE;
	    ARRAY4(ref_input, iter, ti, row, col, BATCH_SIZE, N_IFM, R_IFM, C_IFM) = val;
	    ARRAYi(dt_input, iter, ti, row, col, BATCH_SIZE, N_IFM, R_IFM, C_IFM) = val;
	  }
	}
      }
    }

    // Generate the weight matrix
    for(to = 0; to < M_OFM; to++) {
      for(ti = 0; ti < N_IFM; ti++) {
	for(row = 0; row < K_WTS; row++) {
	  for(col=0; col < K_WTS; col++) {
	    cnndata_t val=rand()%VRANGE;
	    ARRAY4(ref_weights, to, ti, row, col, M_OFM, N_IFM,
		   K_WTS, K_WTS) = val;
	    ARRAYw(dt_weights, to, ti, row, col, M_OFM, N_IFM,
		   K_WTS, K_WTS) = val;
	  }
	}
      }
    }
  }

  struct timeval start_time, end_time;
  printf("\n===== execution starts ======\n\n");
  gettimeofday(&start_time, NULL);

#ifdef __VITIS_CL__  // lab3
  // Data will be migrated to kernel space
  OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_input,buffer_output,buffer_weights},0/* 0 means from host*/));

  //Launch the Kernel
  OCL_CHECK(err, err = q.enqueueTask(krnl_blk_cnn));

  // The result of the previous kernel execution will need to be retrieved in
  // order to view the results. This call will transfer the data from FPGA to
  // source_results vector
  OCL_CHECK(err, err = q.enqueueMigrateMemObjects({buffer_output},CL_MIGRATE_MEM_OBJECT_HOST));

  OCL_CHECK(err, err = q.finish());
#else // lab2
  krnl_cnn(dt_input, dt_weights, dt_output, (uint64_t)BATCH_SIZE);
#endif

  gettimeofday(&end_time, NULL);
  printf(" *** execution finished!\n");

  {
    printf("\n===== Reporting measured throughput ======\n\n");
    float timeusec=(end_time.tv_sec - start_time.tv_sec)*1e6 + (end_time.tv_usec - start_time.tv_usec);
    printf("  runtime = %0.1f (microsec) \n\n", timeusec);
    double num_operations = BATCH_SIZE * (double)2.0 * M_OFM * R_OFM * C_OFM * N_IFM * K_WTS * K_WTS;
    printf("  num operations = %.0f\n", num_operations );
    printf("  throughput: %.5f GigaOP/sec\n", (double)1.0e-3 * num_operations / timeusec);
  }

  //Verify the result
  int mismatch = 0;
  {
    uint64_t iter;
    printf("\n===== Comparing accelerator results to golden reference ======\n\n");

    for(iter=0;iter < BATCH_SIZE; iter++) {
      ZhangIsfpga15_1_fp(&ARRAY4(ref_input, iter, 0, 0, 0,
				 BATCH_SIZE, N_IFM, R_IFM,
				 C_IFM),
			 &ARRAY4(ref_output, iter, 0, 0, 0, BATCH_SIZE, M_OFM,
				 R_OFM, C_OFM),
			 ref_weights);
      if (!verify(&ARRAY4(ref_output, iter, 0, 0, 0, BATCH_SIZE, M_OFM, R_OFM, C_OFM),
		  &ARRAYo(dt_output, iter, 0, 0, 0, BATCH_SIZE, M_OFM, R_OFM, C_OFM))) {
	mismatch=1;
	break;
      }
    }
  }

#ifdef __VITIS_CL__ // lab 3
  OCL_CHECK(err, err = q.enqueueUnmapMemObject(buffer_input , dt_input));
  OCL_CHECK(err, err = q.enqueueUnmapMemObject(buffer_output , dt_output));
  OCL_CHECK(err, err = q.enqueueUnmapMemObject(buffer_weights , dt_weights));
  OCL_CHECK(err, err = q.finish());
#else // lab 2
  free(dt_input);
  free(dt_output);
  free(dt_weights);
#endif
  free(ref_input);
  free(ref_output);
  free(ref_weights);

  printf("\n==== ALL DONE ====\n");

  std::cout << "TEST " << (mismatch ? "FAILED" : "PASSED") << std::endl;

  return (mismatch ? EXIT_FAILURE :  EXIT_SUCCESS);
}


void print_params() {
  printf("\n===== printing the CNN parameters ======\n\n");

  printf("Batch size: %lu\n\n", (uint64_t) BATCH_SIZE);

  printf("Layer Parameters: \nK_wts: \t%d\tS_wts:\t%d\nR_ofm:\t%d\tC_ofm:\t%d\tM_ofm:\t%d\tN_ifm:\t%d\n\n",
         K_WTS, S_WTS, R_OFM, C_OFM, M_OFM, N_IFM);

  printf("Kernel Parameters: \nTr: \t%d\tTc:\t%d\tTm:\t%d\tTn:\t%d\n",
         TR, TC, TM, TN);
}

void ZhangIsfpga15_1_fp(cnndata_t *input, cnndata_t *output, cnndata_t *weights) {
  uint64_t row, col, to, ti;

  for(row = 0; row < R_OFM; row++) {
    for(col = 0; col < C_OFM; col++) {
      for(to = 0; to < M_OFM; to++) {
        for(ti = 0; ti < N_IFM; ti++) {
          uint64_t i, j;
          for(i = 0; i < K_WTS; i++) {
            for(j = 0; j < K_WTS; j++) {
              ARRAY4(output, 0, to, row, col, 0, M_OFM, R_OFM, C_OFM) +=
                ARRAY4(weights, to, ti, i, j, M_OFM, N_IFM, K_WTS, K_WTS)*
                ARRAY4(input, 0, ti, S_WTS *row + i, S_WTS * col + j,
                       0, N_IFM, R_IFM, C_IFM);
            }
          }
        }
      }
    }
  }
}

int verify(cnndata_t *ref, cnndata_t *checkit) {
  uint64_t row, col, to;
  int bad = 0;
  for(to = 0; to < M_OFM; to++)
    for(row = 0; row < R_OFM; row++)
      for(col = 0; col < C_OFM; col++) {
        cnndata_t got = ARRAYo(checkit, 0, to, row, col, 0, M_OFM, R_OFM, C_OFM);
        cnndata_t exp = ARRAY4(ref, 0, to, row, col, 0, M_OFM, R_OFM, C_OFM);
        if (got != exp && bad++ < 15) {
          cnndata_t prev_same = (to >= TM) ? ARRAY4(ref, 0, to-TM, row, col, 0, M_OFM, R_OFM, C_OFM) : 0;
          cnndata_t prev_last = (to >= TM) ? ARRAY4(ref, 0, to-TM, R_OFM-1, C_OFM-1, 0, M_OFM, R_OFM, C_OFM) : 0;
          printf("layer=%lu row=%lu col=%lu got=%d exp=%d diff=%d prev_same=%d prev_last=%d\n",
                 to, row, col, got, exp, got - exp, prev_same, prev_last);
        }
      }
  if (bad) printf("total mismatches: %d of %d\n", bad, M_OFM*R_OFM*C_OFM);
  return bad == 0;
}




