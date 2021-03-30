## InAccel Coral - Runtime options

InAccel Coral relies on the provided compliant runtimes (loaded via the
[inaccel/mkrt](https://github.com/inaccel/mkrt) command) as its interface to the
target **`resource`**, **`memory`**, **`buffer`** and **`compute unit`**
entities.

*mkrt* retrieves information about installed runtimes from special metadata
directories. These directories are named after the runtime, and contain the
shared object (`a.out`) implementation of the spec. A file in the *pkg-config*
format is used by *mkrt* to link against the runtime.

The following is an example of the `inaccel.pc` file for the default
[Xilinx FPGA](src/xilinx-fpga) runtime, that depends on
[Xilinx/XRT](https://github.com/Xilinx/XRT) OpenCL API:

> **xilinx-fpga/inaccel.pc**

```
XILINX_XRT=$ORIGIN

Name: Xilinx FPGA
Version: 2
Description: The default 'Xilinx FPGA' runtime
URL: https://github.com/inaccel/runtime
Libs: -L/opt/xilinx/xrt/lib -lxilinxopencl -lxrt_core
```

By default, *mkrt* searches the directory tree rooted at
`/etc/inaccel/runtimes` for these files. The *`MKRT_CONFIG_PATH`* environment
variable can be used to specify a non-default location.

### Default runtimes

InAccel provides a default Coral runtime for each of the two major FPGA
platforms (Intel, Xilinx). Both runtimes depend on the
[OpenCL](https://khronos.org/opencl) (Open Computing Language) standard that the
two vendors implement.

* Build **Intel FPGA** runtime

```sh
make /path/to/intel-fpga/a.out
```

* Build **Xilinx FPGA** runtime

```sh
make /path/to/xilinx-fpga/a.out
```
