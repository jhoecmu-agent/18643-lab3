18-643 Lab 3 Workspace (Group GPY)
Team: Shengfei Gu, Sutong Yao, Andrew Pan

Final design: two engines sharing one set of weights, with Tm=8, Tn=10,
Tr=Tc=13, running at 200 MHz.


LAYOUT

  modified_code/C_2x8x10/   Final design source. The krnl_cnn component
                            builds from here.
  krnl_cnn/                 HLS kernel component.
  cnn_host/                 Host component, with its own copy of the final
                            host sources.
  cnn_system/               System component (link and package).
  18643_cnn_src/            Original starter code, unmodified.


BUILD SETTINGS

  HLS (krnl_cnn/hls_config.cfg):
    part xczu3eg-sbva484-1-i, clock 3.3ns, clock uncertainty 27%

  Link (cnn_system/hw_link/cnn-link.cfg):
    [clock]
    id=4:krnl_cnn_1        (selects PL4, 200 MHz)

  The HLS clock is only a scheduling target. The real kernel clock is the one
  set at link time.


BUILD AND RUN

  1. Open this workspace in Vitis Unified IDE 2023.2.
  2. In the FLOW panel, select cnn_system and run Build All under Hardware.
  3. Write cnn_system/build/hw/package/package/sd_card.img to the SD card.
  4. On the board:
       cd /run/media/mmcblk0p1
       ./cnn_host cnn.xclbin

  Source paths: krnl_cnn/hls_config.cfg uses absolute paths under
  /scratch/643_vitis_shengfeg/lab3/modified_code/C_2x8x10/. If the workspace
  is moved, update the syn.file entries to the new location.


RESULTS (this workspace, on the Ultra96v2)

  Test                          TEST PASSED
  Runtime, batch of 10          17.041 ms
  Throughput                    43.87 GOPS
  Kernel-only ideal throughput  61.44 GOPS
  Routed timing at 200 MHz      Met, WNS +0.486 ns
  Kernel utilization            LUT 20.90%, FF 16.16%, DSP 92.78%, B18K 69.91%
