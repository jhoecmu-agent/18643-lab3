# Quick development loops for the lab 3 CNN, run before a hardware build
# (build_cli.sh). Each step is slower and closer to hardware than the one
# before it; move on once it passes.
#
#   make sw        plain C++ on this machine, __VITIS_CL__ off (lab 2 mode).
#                  Correctness and debugging; needs only g++.
#   make sw_emu    Vitis software emulation: the real OpenCL host code on x86,
#                  with the kernel compiled as C++. Correctness only; its
#                  runtime means nothing.
#   make csynth    HLS synthesis of the kernel: latency, II, resources, bursts.
#   make gui       open this folder in the Vitis IDE (in the background)
#   make clean     delete this NAME's sw, sw_emu and csynth output
#
# Options (same meaning as in build_cli.sh):
#   VARIANT=dir    take krnl_cnn.cpp / kernel643.h (or any source) from dir
#   NAME=name      output goes to build/NAME/ (default: VARIANT's name, or cnn)
#   HLS_CLOCK=ns   csynth clock target (default: krnl_cnn/hls_config.cfg)
#   OPT=-O0        optimization for sw (default -O2; use -O0 to step in gdb)
#   SAN=1          sw with AddressSanitizer + UBSan (catches buffer overruns)
#
# sw_emu, csynth and gui need Vitis 2023.2 in the shell first:
#   source ~/setup_vitis_local.sh 2023.2

SHELL := /bin/bash
.SHELLFLAGS := -eu -o pipefail -c
.DEFAULT_GOAL := help

SELF := $(abspath $(lastword $(MAKEFILE_LIST)))
WS := $(patsubst %/,%,$(dir $(SELF)))

course_root := /afs/ece.cmu.edu/class/ece643/software/xilinxVitis
PLATFORM := $(course_root)/Vitis/2023.2/base_platforms/u96v2_sbc_base_2023_2/export/u96v2_sbc_base_2023_2-vitis-platform/u96v2_sbc_base_2023_2-vitis-platform.xpfm
# HLS headers (ap_int.h, hls_stream.h) so sw still compiles if the kernel uses them
HLS_INC := $(course_root)/Vitis_HLS/2023.2/include

# --- sources ------------------------------------------------------------------
# Same files as build_cli.sh and the Vitis IDE, all staged into one directory
# so the kernel and the host always compile against the same headers.
HOST_FILES := cnn.cpp cnn.h util643.h kernel643.h instance643.h
KERNEL_FILES := krnl_cnn.cpp util643.h kernel643.h instance643.h
DEFAULT_SRC := $(WS)/18643_cnn_src/krnl_cnn.cpp $(addprefix $(WS)/cnn_host/,$(HOST_FILES))

ifneq ($(VARIANT),)
  VARIANT_DIR := $(patsubst %/,%,$(firstword $(wildcard $(abspath $(VARIANT))/ $(WS)/$(VARIANT)/)))
  $(if $(VARIANT_DIR),,$(error no such variant directory: $(VARIANT)))
endif
# A variant's copy of a file, if it has one, replaces the default
pick = $(or $(and $(VARIANT_DIR),$(wildcard $(VARIANT_DIR)/$(notdir $1))),$1)
SRC := $(foreach f,$(DEFAULT_SRC),$(call pick,$f))
OVERRIDES := $(if $(VARIANT_DIR),$(notdir $(filter $(VARIANT_DIR)/%,$(SRC))))
ifneq ($(VARIANT_DIR),)
  $(if $(OVERRIDES),,$(error $(VARIANT_DIR) contains none of: krnl_cnn.cpp $(HOST_FILES)))
endif
NOT_ALLOWED := $(filter-out krnl_cnn.cpp kernel643.h,$(OVERRIDES))

# Only a NAME from the command line counts; some systems export NAME (the hostname)
ifneq ($(origin NAME),command line)
  NAME := $(if $(VARIANT_DIR),$(notdir $(VARIANT_DIR)),cnn)
endif
# NAME becomes build/NAME, which clean deletes from
ifneq ($(shell [[ '$(NAME)' =~ ^[A-Za-z0-9][A-Za-z0-9._-]*$$ ]] && echo ok),ok)
  $(error invalid NAME '$(NAME)': use letters, digits, '.', '_', '-')
endif
ifeq ($(NAME),deploy)
  $(error NAME 'deploy' is reserved for build/deploy/)
endif

OUT := $(WS)/build/$(NAME)
SW_DIR := $(OUT)/sw
EMU_DIR := $(OUT)/sw_emu
SYN_DIR := $(OUT)/csynth

# $(call stage,DIR): fresh copy of every source in DIR
define stage
@rm -rf $1 && mkdir -p $1 && cp -- $(SRC) $1/
@$(if $(OVERRIDES),echo '  $(VARIANT_DIR) overrides: $(OVERRIDES)')
@$(if $(NOT_ALLOWED),echo '  warning: $(NOT_ALLOWED) may not change in the final submission' >&2)
endef

# $(call step,TEXT): progress line
step = @printf '\n\e[1m[%s] %s\e[0m\n' '$(NAME)' '$1'

.PHONY: help sw sw_emu csynth gui clean check-vitis

help:
	@sed -n '1,/^$$/{s/^# \{0,1\}//;p}' $(SELF)

# --- plain C++ -----------------------------------------------------------------
OPT ?= -O2
# The system g++ lacks the sanitizer runtimes; the GCC bundled with Vitis HLS has them
SAN_GCC := $(course_root)/Vitis_HLS/2023.2/tps/lnx64/gcc-8.3.0
SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -Wl,-rpath,$(SAN_GCC)/lib64

sw:
	$(call stage,$(SW_DIR)/src)
	@# Lab 2 mode: the handout's "comment out __VITIS_CL__ in util643.h", on the copy
	@sed -i -E 's|^([[:space:]]*#[[:space:]]*define[[:space:]]+__VITIS_CL__)|// \1|' $(SW_DIR)/src/util643.h
	@! grep -qE '^[[:space:]]*#[[:space:]]*define[[:space:]]+__VITIS_CL__' $(SW_DIR)/src/util643.h || \
	    { echo 'could not turn off __VITIS_CL__ in the staged util643.h' >&2; exit 1; }
	$(call step,sw: compiling with g++ $(OPT)$(if $(SAN), + sanitizers))
	$(if $(SAN),$(SAN_GCC)/bin/g++,g++) $(OPT) -g -std=c++14 -Wall -Wno-unknown-pragmas -Wno-unused-label -isystem $(HLS_INC) \
	    $(if $(SAN),$(SAN_FLAGS)) \
	    -o $(SW_DIR)/cnn_sw $(SW_DIR)/src/cnn.cpp $(SW_DIR)/src/krnl_cnn.cpp
	$(call step,sw: running)
	@# krnl_cnn's buffers live on the stack here; large tiles can outgrow 8 MB
	cd $(SW_DIR) && { ulimit -s 1048576 2>/dev/null || true; } && ./cnn_sw 2>&1 | tee run.log

# --- Vitis software emulation ----------------------------------------------------
# The same steps the Vitis IDE runs for the cnn system's SW_EMU target.
sw_emu: check-vitis
	$(call stage,$(EMU_DIR)/src)
	$(call step,sw_emu 1/4: compiling krnl_cnn.xo)
	cd $(EMU_DIR) && v++ -c -k krnl_cnn --target sw_emu -g \
	    --platform $(PLATFORM) --temp_dir compile -o krnl_cnn.xo \
	    $(addprefix src/,$(KERNEL_FILES))
	$(call step,sw_emu 2/4: linking and packaging cnn.xclbin)
	@rm -rf $(EMU_DIR)/link $(EMU_DIR)/package && mkdir -p $(EMU_DIR)/link $(EMU_DIR)/package
	@# cnn-link.cfg uses relative temp/report/log dirs, so run from link/
	cd $(EMU_DIR)/link && v++ --link --target sw_emu --platform $(PLATFORM) \
	    --config $(WS)/cnn/hw_link/cnn-link.cfg -o cnn.xclbin ../krnl_cnn.xo
	cd $(EMU_DIR)/package && v++ --package --target sw_emu --platform $(PLATFORM) \
	    --save-temps --temp_dir package.build -o cnn.xclbin ../link/cnn.xclbin
	$(call step,sw_emu 3/4: compiling the x86 host)
	g++ -O2 -g -std=c++14 -Wall -Wno-unknown-pragmas -Wno-deprecated-declarations \
	    -Wno-ignored-attributes -D__USE_XOPEN2K8 \
	    -isystem $(XILINX_XRT)/include -isystem $(HLS_INC) \
	    -o $(EMU_DIR)/cnn_host $(EMU_DIR)/src/cnn.cpp \
	    -L$(XILINX_XRT)/lib -Wl,-rpath,$(XILINX_XRT)/lib \
	    -lxilinxopencl -lxrt_coreutil -lpthread -lrt
	cd $(EMU_DIR) && emconfigutil --platform $(PLATFORM) --od . --nd 1 >/dev/null
	$(call step,sw_emu 4/4: running)
	cd $(EMU_DIR) && XCL_EMULATION_MODE=sw_emu ./cnn_host package/cnn.xclbin 2>&1 | tee run.log

# --- HLS synthesis -------------------------------------------------------------
HLS_CLOCK ?=

csynth: check-vitis
	$(if $(HLS_CLOCK),@[[ '$(HLS_CLOCK)' =~ ^[0-9]+(\.[0-9]+)?$$ ]] || { echo 'HLS_CLOCK takes a period in ns (e.g. 5 or 6.6)' >&2; exit 1; })
	$(call stage,$(SYN_DIR)/src)
	@# Keep every IDE setting (part, clock, uncertainty, top) but point the
	@# source list at the staged copies.
	@awk -v src=$(SYN_DIR)/src -v clk='$(HLS_CLOCK)' ' \
	    /^[[:space:]]*(syn\.file|package\.output\.file)[[:space:]]*=/ { next } \
	    clk != "" && /^[[:space:]]*clock[[:space:]]*=/ { next } \
	    { print } \
	    /^[[:space:]]*\[hls\]/ { \
	        n = split("$(KERNEL_FILES)", f, " "); \
	        for (i = 1; i <= n; i++) print "syn.file=" src "/" f[i]; \
	        if (clk != "") print "clock=" clk "ns" \
	    }' $(WS)/krnl_cnn/hls_config.cfg > $(SYN_DIR)/hls_config.cfg
	@grep -q '^syn\.file=' $(SYN_DIR)/hls_config.cfg || { echo 'krnl_cnn/hls_config.cfg has no [hls] section' >&2; exit 1; }
	$(call step,csynth: $(shell grep -m1 -E '^clock=' $(WS)/krnl_cnn/hls_config.cfg)$(if $(HLS_CLOCK), -> clock=$(HLS_CLOCK)ns))
	cd $(SYN_DIR) && v++ --compile --mode hls --config hls_config.cfg --work_dir work
	@r=$(SYN_DIR)/work/hls/syn/report; \
	sed -n '/^+ Timing:/,/^$$/p' $$r/krnl_cnn_csynth.rpt; \
	awk '/^\+ Performance & Resource Estimates/ || /^\* M_AXI$$/ || /^\* Inferred Burst Summary/ { p = 1 } \
	     /^== HW Interfaces/ || /^\* S_AXILITE Interfaces/ || /^\* All M_AXI Variable Accesses/ { p = 0 } \
	     p && !/^=+$$/' $$r/csynth.rpt; \
	printf 'Full reports: %s/\n  csynth.rpt (summary)  krnl_cnn_csynth.rpt (loops, resources)\n' $$r

# --- Vitis IDE -------------------------------------------------------------------
gui: check-vitis
	@[[ -n $${DISPLAY:-} ]] || { echo 'No display: run this from a desktop session (or ssh -X / FastX)' >&2; exit 1; }
	@mkdir -p $(WS)/build
	nohup vitis -w $(WS) > $(WS)/build/vitis-gui.log 2>&1 &
	@echo 'Vitis IDE starting in the background (log: build/vitis-gui.log)'

check-vitis:
	@[[ $${XILINX_VITIS:-} == */2023.2 && -n $${XILINX_XRT:-} ]] || \
	    { echo 'Vitis 2023.2 is not set up in this shell. Run: source ~/setup_vitis_local.sh 2023.2' >&2; exit 1; }

clean:
	rm -rf $(SW_DIR) $(EMU_DIR) $(SYN_DIR)
	@rmdir --ignore-fail-on-non-empty $(OUT) 2>/dev/null || true
