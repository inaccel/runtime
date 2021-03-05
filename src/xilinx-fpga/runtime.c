#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "INCL/opencl.h"
#include <inaccel/runtime.h>

struct mem_data {
	uint8_t m_type;          // enum corresponding to mem_type.
	uint8_t m_used;          // if 0 this bank is not present
	uint8_t padding[6];      // 8 Byte alignment padding (initialized to zeros)
	union {
		uint64_t m_size;     // if mem_type DDR, then size in KB;
		uint64_t route_id;   // if streaming then "route_id"
	};
	union {
		uint64_t m_base_address; // if DDR then the base address;
		uint64_t flow_id;        // if streaming then "flow id"
	};
	unsigned char m_tag[16]; // DDR: BANK0,1,2,3, has to be null terminated; if streaming then stream0, 1 etc
};

struct mem_topology {
	int32_t m_count; //Number of mem_data
	struct mem_data m_mem_data[1]; //Should be sorted on mem_type
};

typedef struct{
	unsigned flags;
	void *obj;
	void *param;
} cl_mem_ext_ptr_t;

struct _cl_buffer {
	cl_command_queue command_queue;
	cl_mem mem;
	cl_memory memory;
};

struct _cl_compute_unit {
	cl_command_queue command_queue;
	cl_kernel kernel;
	cl_mem *args;
};

struct _cl_memory {
	unsigned int id;
	cl_resource resource;
	char *type;
	cl_mem page_buf;
};

struct _cl_resource {
	// Virtual device variables
	cl_context context;
	cl_device_id device_id;
	cl_platform_id platform_id;
	cl_program program;

	// Physical device variables
	char *name;
	char *root_path;
	char *vendor;
	char *version;
	struct mem_topology *topology;

	char *temperature;
	char *power;
	char *serial_no;
};

int await_buffer_copy(cl_buffer buffer) {
	return inclFinish(buffer->command_queue);
}

int await_compute_unit_run(cl_compute_unit compute_unit) {
	return inclFinish(compute_unit->command_queue);
}

int copy_from_buffer(cl_buffer buffer) {
	return inclEnqueueMigrateMemObject(buffer->command_queue, buffer->mem, 1);
}

int copy_to_buffer(cl_buffer buffer) {
	return inclEnqueueMigrateMemObject(buffer->command_queue, buffer->mem, 0);
}

cl_buffer create_buffer(cl_memory memory, size_t size, void *host) {
	cl_uint CL_MEM_EXT_PTR = 1 << 31;
	cl_uint CL_MEMORY = memory->id | (1 << 31);

	cl_mem_ext_ptr_t ext_ptr;
	ext_ptr.flags = CL_MEMORY;
	ext_ptr.obj = host;
	ext_ptr.param = 0;

	cl_buffer buffer = (cl_buffer) calloc(1, sizeof(struct _cl_buffer));
	if (!buffer) return NULL;

	if (!(buffer->mem = inclCreateBuffer(memory->resource->context, CL_MEM_EXT_PTR | CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, size, &ext_ptr))) {
		free(buffer);
		return NULL;
	}

	if (!(buffer->command_queue = inclCreateCommandQueue(memory->resource->context, memory->resource->device_id))) {
		inclReleaseMemObject(buffer->mem);
		free(buffer);
		return NULL;
	}

	buffer->memory = memory;

	return buffer;
}

cl_compute_unit create_compute_unit(cl_resource resource, const char *name) {
	cl_compute_unit compute_unit = (cl_compute_unit) calloc(1, sizeof(struct _cl_compute_unit));
	if(!compute_unit) return NULL;

	if (!(compute_unit->kernel = inclCreateKernel(resource->program, name))) {
		free(compute_unit);
		return NULL;
	}

	if (!(compute_unit->command_queue = inclCreateCommandQueue(resource->context, resource->device_id))) {
		inclReleaseKernel(compute_unit->kernel);
		free(compute_unit);
		return NULL;
	}

	cl_uint num_arguments;
	inclGetKernelInfo(compute_unit->kernel, CL_KERNEL_NUM_ARGS, sizeof(cl_uint), &num_arguments, NULL);

	if (!(compute_unit->args = (cl_mem *) calloc(num_arguments, sizeof(cl_mem)))) {
		inclReleaseCommandQueue(compute_unit->command_queue);
		inclReleaseKernel(compute_unit->kernel);
		free(compute_unit);
		return NULL;
	}

	return compute_unit;
}

cl_memory create_memory(cl_resource resource, unsigned int index) {
	if (resource->topology && (index < resource->topology->m_count)) {
		cl_memory memory = (cl_memory) calloc(1, sizeof(struct _cl_memory));
		if (!memory) return NULL;

		memory->id = index;
		memory->resource = resource;

		cl_uint CL_MEM_EXT_PTR = 1 << 31;
		cl_uint CL_MEMORY = memory->id | (1 << 31);

		cl_mem_ext_ptr_t ext_ptr;
		ext_ptr.flags = CL_MEMORY;
		ext_ptr.obj = NULL;
		ext_ptr.param = 0;

		if (!(memory->page_buf = inclCreateBuffer(memory->resource->context, CL_MEM_EXT_PTR | CL_MEM_WRITE_ONLY, 4096, &ext_ptr))) {
			free(memory);
			return NULL;
		}

		// Type is optional (at least for now)
		char *type = (char *) malloc(strlen((const char *) memory->resource->topology->m_mem_data[memory->id].m_tag));
		if (type) {
			strcpy(type, (const char *) memory->resource->topology->m_mem_data[memory->id].m_tag);

			strtok(type, "[");

			if (strcmp(type, "bank0") && strcmp(type, "bank1") && strcmp(type, "bank2") && strcmp(type, "bank3")) {
				memory->type = type;
			} else{
				free(type);
				memory->type = strdup("DDR");
			}
		}

		return memory;
	} else return NULL;
}

cl_resource create_resource(unsigned int index) {
	cl_resource resource = (cl_resource) calloc(1, sizeof(struct _cl_resource));
	if (!resource) return NULL;

	if (!(resource->platform_id = inclGetPlatformID("Xilinx"))) {
		free(resource);
		return NULL;
	}

	if (!(resource->device_id = inclGetDeviceID(resource->platform_id, index))) {
		free(resource);
		return NULL;
	}

	if (!(resource->context = inclCreateContext(resource->device_id))) {
		free(resource);
		return NULL;
	}

	if (!(resource->vendor = strdup("xilinx"))) {
		perror("Error: strdup");
		inclReleaseContext(resource->context);
		free(resource);
		return NULL;
	}

	size_t raw_name_size;
	inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, 0, NULL, &raw_name_size);

	char *raw_name = (char *) calloc(raw_name_size, sizeof(char));
	if (!raw_name) {
		perror("Error: calloc");
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return NULL;
	}

	inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, raw_name_size, raw_name, NULL);

	resource->name = (char *) calloc(raw_name_size, sizeof(char));
	if (!resource->name) {
		perror("Error: calloc");
		free(raw_name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return NULL;
	}

	resource->version = (char *) calloc(raw_name_size, sizeof(char));
	if (!resource->version) {
		perror("Error: calloc");
		free(resource->name);
		free(raw_name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return NULL;
	}

	const char *regex = "^xilinx_([^_]+)_(.*)_([^_]+)_([^_]+)$";

	regex_t compile;
	regmatch_t group[5];

	if (regcomp(&compile, regex, REG_EXTENDED)) {
		perror("Error: regcomp");
		free(resource->version);
		free(resource->name);
		free(raw_name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return NULL;
	}

	if (!regexec(&compile, raw_name, 5, group, 0)) {
		strncpy(resource->name, raw_name + group[1].rm_so, group[1].rm_eo - group[1].rm_so);

		strncpy(resource->version, raw_name + group[2].rm_so, group[2].rm_eo - group[2].rm_so);
		strcat(resource->version, "_");
		strncat(resource->version, raw_name + group[3].rm_so, group[3].rm_eo - group[3].rm_so);
		strcat(resource->version, ".");
		strncat(resource->version, raw_name + group[4].rm_so, group[4].rm_eo - group[4].rm_so);
	} else {
		perror("Error: regexec");
		free(resource->version);
		free(resource->name);
		free(raw_name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return NULL;
	}

	regfree(&compile);

	free(raw_name);

	glob_t dev;
	unsigned idx = 0;

	if (!glob("/sys/bus/pci/drivers/{xocl,xuser}/*:*:*.*", GLOB_BRACE, NULL, &dev)) {
		size_t i;
		for (i = dev.gl_pathc; i > 0; i--) {
			char temp_dev[PATH_MAX / 2] = {0};
			if (readlink(dev.gl_pathv[i - 1], temp_dev, sizeof(temp_dev) - 1) == 1)
				return resource;

			char path[PATH_MAX];
			sprintf(path, "%s/%s/ready", dirname(dev.gl_pathv[i - 1]), temp_dev);

			FILE *ready_file = fopen(path, "r");
			if (!ready_file) continue;

			unsigned ready;
			if(fscanf(ready_file, "0x%d", &ready) <= 0) continue;

			fclose(ready_file);

			if (ready) {
				if (idx == index) {
					resource->root_path = strdup(dirname(path));

					break;
				}
				else idx++;
			}
		}

		globfree(&dev);
	} else perror("Error: glob");

	if (!resource->root_path) {
		// No device found in the filesystem or error in glob
		fprintf(stderr, "Error: NULL root_path\n");
		free(resource->version);
		free(resource->name);
		free(raw_name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return NULL;
	}

	char path[PATH_MAX];
	if (sprintf(path, "%s/xmc.*", resource->root_path) < 0) return resource;

	glob_t xmc;
	if (!glob(path, GLOB_NOSORT, NULL, &xmc)) {
		sprintf(path, "%s/%s", xmc.gl_pathv[0], "xmc_fpga_temp");
		resource->temperature = strdup(path);

		sprintf(path, "%s/%s", xmc.gl_pathv[0], "xmc_power");
		resource->power = strdup(path);

		sprintf(path, "%s/%s", xmc.gl_pathv[0], "serial_num");
		FILE *serial = fopen(path, "r");
		if (serial) {
			char serial_no[50];
			serial_no[0] = '\0';

			if (fscanf(serial,"%s", serial_no)) {
				resource->serial_no = strdup(serial_no);
			}

			fclose(serial);
		}

		globfree(&xmc);
	}

	return resource;
}

size_t get_memory_size(cl_memory memory) {
	if (memory->resource->topology->m_mem_data[memory->id].m_used) {
		return memory->resource->topology->m_mem_data[memory->id].m_size * 1024 - 4096; // minus page_buf size
	} else return 0;
}

char *get_memory_type(cl_memory memory) {
	return memory->type;
}

char *get_resource_name(cl_resource resource) {
	return resource->name;
}

float get_resource_power(cl_resource resource) {
	if (resource->power) {
		FILE *power_file = fopen(resource->power, "r");

		if (power_file) {
			char tmp[10] = {0};

			int ret = fscanf(power_file, "%s", tmp);
			fclose(power_file);

			if(ret != EOF) {
				// Power is obtained in uWatts so we convert it to Watts
				float value = ((double) strtoul(tmp, NULL, 10)) / 1000000;

				return value;
			}
		}
	}

	return -1.0f;
}

char *get_resource_serial_no(cl_resource resource) {
	return resource->serial_no;
}

float get_resource_temperature(cl_resource resource) {
	if (resource->temperature) {
		FILE *temp_file = fopen(resource->temperature, "r");

		if (temp_file) {
			char temperature[4] = {0};

			int ret = fscanf(temp_file, "%s", temperature);
			fclose(temp_file);

			if(ret != EOF) {
				return strtof(temperature, NULL);
			}
		}
	}

	return -1;
}

char *get_resource_vendor(cl_resource resource) {
	return resource->vendor;
}

char *get_resource_version(cl_resource resource) {
	return resource->version;
}

int program_resource_with_binary(cl_resource resource, size_t size, const void *binary) {
	if (resource->program) if (inclReleaseProgram(resource->program)) return EXIT_FAILURE;

	if (!(resource->program = inclCreateProgramWithBinary(resource->context, resource->device_id, size, binary))) return EXIT_FAILURE;

	if (inclBuildProgram(resource->program)) {
		inclReleaseProgram(resource->program);
		return EXIT_FAILURE;
	}

	// mem_topology changes every time we program the device
	if (resource->topology) free(resource->topology);

	char path[PATH_MAX];
	if(sprintf(path, "%s/icap.*", resource->root_path) < 0) {
		inclReleaseProgram(resource->program);
		return EXIT_FAILURE;
	}

	glob_t icap;
	if (!glob(path, GLOB_NOSORT, NULL, &icap)) {
		sprintf(path, "%s/%s", icap.gl_pathv[0], "mem_topology");
		globfree(&icap);

		FILE *mem_topology = fopen(path, "r");
		if (mem_topology) {
			//PATH_MAX bytes should be sufficient for mem_topology
			resource->topology = (struct mem_topology *) malloc(PATH_MAX);
			size_t topology_bytes = fread(resource->topology, sizeof(char), PATH_MAX, mem_topology);

			fclose(mem_topology);

			if (topology_bytes) return EXIT_SUCCESS;
		}
	}

	perror("Error: glob");
	inclReleaseProgram(resource->program);
	free(resource->topology);
	return EXIT_FAILURE;
}

void release_buffer(cl_buffer buffer) {
	inclReleaseCommandQueue(buffer->command_queue);

	inclReleaseMemObject(buffer->mem);

	free(buffer);
}

void release_compute_unit(cl_compute_unit compute_unit) {
	inclReleaseCommandQueue(compute_unit->command_queue);

	inclReleaseKernel(compute_unit->kernel);

	free(compute_unit->args);

	free(compute_unit);
}

void release_memory(cl_memory memory) {
	inclReleaseMemObject(memory->page_buf);

	free(memory->type);

	free(memory);
}

void release_resource(cl_resource resource) {
	if (resource->program) {
		free(resource->topology);
		inclReleaseProgram(resource->program);
	}

	if (resource->temperature) free(resource->temperature);
	if (resource->power) free(resource->power);
	if (resource->serial_no) free(resource->serial_no);

	free(resource->name);
	free(resource->root_path);
	free(resource->vendor);
	free(resource->version);

	inclReleaseContext(resource->context);

	free(resource);
}

int run_compute_unit(cl_compute_unit compute_unit) {
	int ret = inclEnqueueTask(compute_unit->command_queue, compute_unit->kernel);

	cl_uint num_arguments;
	inclGetKernelInfo(compute_unit->kernel, CL_KERNEL_NUM_ARGS, sizeof(cl_uint), &num_arguments, NULL);

	int index;
	for (index = 0; index < num_arguments; index++) {
		if (compute_unit->args[index]) {
			inclSetKernelArg(compute_unit->kernel, index, sizeof(cl_mem), &compute_unit->args[index]);
		}
	}

	return ret;
}

int set_compute_unit_arg(cl_compute_unit compute_unit, unsigned int index, size_t size, const void *value) {
	if (size) {
		return inclSetKernelArg(compute_unit->kernel, index, size, value);
	} else {
		cl_buffer buffer = (cl_buffer) value;

		if(!compute_unit->args[index]) compute_unit->args[index] = buffer->memory->page_buf;

		return inclSetKernelArg(compute_unit->kernel, index, sizeof(cl_mem), &buffer->mem);
	}
}
