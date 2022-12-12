intel-fpga = inaccel/runtime/intercept INCL/opencl runtime
intel-fpga_CFLAGS = -O3 -Wall -DNDEBUG -fPIC
intel-fpga_LDFLAGS = -shared -Wl,--allow-multiple-definition
