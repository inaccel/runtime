## InAccel Coral - Default runtimes

InAccel provides a default Coral runtime for each of the two major FPGA platforms (Intel, Xilinx). Both runtimes depend on the [OpenCL](https://khronos.org/opencl) (Open Computing Language) standard that the two vendors implement, and are shipped along with InAccel container-runtime, inside `inaccel` package.

* Build **Intel** FPGA runtime

```bash
make /etc/inaccel/runtimes/libIntel.so CPPFLAGS="-DIntel -IOpenCL-Headers"
```

* Build **Xilinx** FPGA runtime

```bash
make /etc/inaccel/runtimes/libXilinx.so CPPFLAGS="-DXilinx -IOpenCL-Headers"
```
