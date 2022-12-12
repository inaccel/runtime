xilinx-fpga = inaccel/runtime/intercept INCL/opencl runtime
xilinx-fpga_CFLAGS = -O3 -Wall -DNDEBUG -fPIC
xilinx-fpga_LDFLAGS = -shared -Wl,--allow-multiple-definition
