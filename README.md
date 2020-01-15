## InAccel Coral - Runtime specification

[InAccel](https://inaccel.com) provides a runtime specification that you can use to advertise system hardware resources to [Coral](https://inaccel.com/coral).

### Abstract

The InAccel Coral runtime specification aims to specify the configuration, and execution interface for the efficient management of any accelerator-based hardware resource.

Without customizing the code for InAccel Coral itself, vendors can implement a custom runtime that you deploy as a platform, using InAccel container-runtime. The targeted cases include custom (non-OpenCL) implementations, new FPGA families, and other similar computing resources that may require vendor specific initialization and setup.

### Use Cases

To provide more context for users the following section gives example use cases for each part of the repository.

#### Hook Developers

Hook developers can extend the functionality of a compliant runtime by hooking into an accelerators's lifecycle with an external application. Example use cases include sophisticated hardware configuration, advanced logging, IP licensing (hardware identification - authentication - decryption), etc.

#### Platform Developers

Platform developers can build runtime implementations that expose diverse hardware resources and system configuration, containing low-level OS and hardware-specific details, on a particular platform.

## InAccel Coral - Runtime options

InAccel Coral relies on the provided compliant runtimes (loaded via `inaccel-runc`) as its interface to the target `device`, `memory`, and `accelerators`.

Runtimes can be registered with Coral, as independent platforms, via the configuration file.

By default, the InAccel container-runtime uses the configuration at `/etc/inaccel/coral.json`. The `--config-file` flag can be used to specify a non-default location:

**docker/daemon.json**
```json
{
	"default-runtime": "inaccel",
	"runtimes": {
		"inaccel": {
			"path": "inaccel-runc",
			"runtimeArgs": [
				"--config-file /path/to/inaccel/coral.json"
			]
		}
	}
}
```

The following is an example using the [default](https://github.com/inaccel/runtime/tree/default) `libXilinx.so` runtime, that depends on [XRT](https://github.com/Xilinx/XRT) OpenCL API:

**inaccel/coral.json**
```json
{
	"platforms": {
		"xilinx": {
			"runtime": "/etc/inaccel/runtimes/libXilinx.so",
			"dependencies": ["xilinxopencl"],
			"environment": ["XILINX_XRT=$ORIGIN"],
			"binaries": ["/opt/xilinx/xrt/bin"],
			"libraries": ["/opt/xilinx/xrt/lib"]
		}
	}
}
```
