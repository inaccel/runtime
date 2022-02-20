#include <glob.h>
#include <inaccel/runtime.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "INCL/opencl.h"

struct mem_data {
	uint8_t m_type;
	uint8_t m_used;
	uint8_t padding[6];
	union {
		uint64_t m_size;
		uint64_t route_id;
	};
	union {
		uint64_t m_base_address;
		uint64_t flow_id;
	};
	unsigned char m_tag[16];
};

struct mem_topology {
	int32_t m_count;
	struct mem_data m_mem_data[0];
};

struct _cl_buffer {
	cl_memory memory;
	size_t size;
	void *host;

	cl_command_queue command_queue;
	cl_mem mem;
};

struct _cl_compute_unit {
	cl_resource resource;
	char *name;

	cl_command_queue command_queue;
	cl_kernel kernel;

	cl_memory *memory;
};

struct _cl_memory {
	cl_resource resource;
	unsigned int index;

	size_t size;
	char *type;

	cl_mem page;
};

struct _cl_resource {
	unsigned int index;

	char *name;
	float power;
	char *serial_no;
	float temperature;
	char *version;
	char *vendor;

	cl_context context;
	cl_device_id device_id;
	cl_platform_id platform_id;
	cl_program program;

	pthread_t thread;
	unsigned char release;
	char *root_path;

	struct mem_topology *mem_topology;
};

float get_power(char *power_path) {
	FILE *power_stream = fopen(power_path, "r");
	if (power_stream) {
		unsigned int power;
		int ret = fscanf(power_stream, "%u", &power);
		fclose(power_stream);
		if (ret != EOF) {
			return power / 1000000.0f;
		}
	}
	return 0.0f;
}

float get_temperature(char *temperature_path) {
	FILE *temperature_stream = fopen(temperature_path, "r");
	if (temperature_stream) {
		float temperature;
		int ret = fscanf(temperature_stream, "%f", &temperature);
		fclose(temperature_stream);
		if (ret != EOF) {
			return temperature;
		}
	}
	return 0.0f;
}

void *sensor_routine(void *arg) {
	cl_resource resource = (cl_resource) arg;

	char power_path[PATH_MAX] = {0};
	char temperature_path[PATH_MAX] = {0};

	char xmc_pattern[PATH_MAX];
	if (sprintf(xmc_pattern, "%s/xmc.*", resource->root_path) >= 0) {
		glob_t xmc;
		if (!glob(xmc_pattern, GLOB_NOSORT, NULL, &xmc)) {
			sprintf(power_path, "%s/xmc_power", xmc.gl_pathv[0]);
			sprintf(temperature_path, "%s/xmc_fpga_temp", xmc.gl_pathv[0]);
			globfree(&xmc);
		}
	}

	if (!strlen(power_path) && !strlen(temperature_path)) {
		return NULL;
	}

	for (;;) {
		if (resource->release) {
			return NULL;
		}

		#define SEC_TO_USEC(sec) ((sec) * 1000000)
		#define NSEC_TO_USEC(nsec) ((nsec) / 1000)

		struct timespec tp;

		clock_gettime(CLOCK_MONOTONIC, &tp);
		unsigned long start = SEC_TO_USEC((unsigned long) tp.tv_sec) + NSEC_TO_USEC((unsigned long) tp.tv_nsec);

		if (strlen(power_path)) {
			resource->power = get_power(power_path);
		}
		if (strlen(temperature_path)) {
			resource->temperature = get_temperature(temperature_path);
		}

		clock_gettime(CLOCK_MONOTONIC, &tp);
		unsigned long stop = SEC_TO_USEC((unsigned long) tp.tv_sec) + NSEC_TO_USEC((unsigned long) tp.tv_nsec);

		unsigned long usec = stop - start;
		if (usec < 999999) {
			usleep(999999 - usec);
		}
	}

	return NULL;
}

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
	cl_buffer buffer = (cl_buffer) calloc(1, sizeof(struct _cl_buffer));
	if (!buffer) {
		perror("Error: calloc");

		return INACCEL_FAILED;
	}

	buffer->memory = memory;
	buffer->size = size;
	buffer->host = host;

	#define CL_MEM_EXT_PTR_XILINX (1 << 31)
	struct cl_mem_ext_ptr_t {
		unsigned int flags;
		void *obj;
		void *param;
	} ext_ptr = {
		memory->index | CL_MEM_EXT_PTR_XILINX,
		buffer->host,
		0
	};
	if (!(buffer->mem = inclCreateBuffer(memory->resource->context, CL_MEM_EXT_PTR_XILINX | CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, buffer->size, &ext_ptr))) {
		free(buffer);

		return NULL;
	}

	if (!(buffer->command_queue = inclCreateCommandQueue(memory->resource->context, memory->resource->device_id))) {
		inclReleaseMemObject(buffer->mem);

		free(buffer);

		return INACCEL_FAILED;
	}

	return buffer;
}

cl_compute_unit create_compute_unit(cl_resource resource, const char *name) {
	cl_compute_unit compute_unit = (cl_compute_unit) calloc(1, sizeof(struct _cl_compute_unit));
	if (!compute_unit) {
		perror("Error: calloc");

		return INACCEL_FAILED;
	}

	compute_unit->resource = resource;
	if (!(compute_unit->name = strdup(name))) {
		perror("Error: strdup");

		free(compute_unit);

		return INACCEL_FAILED;
	}

	if (!(compute_unit->kernel = inclCreateKernel(compute_unit->resource->program, compute_unit->name))) {
		free(compute_unit->name);
		free(compute_unit);

		return NULL;
	}

	if (!(compute_unit->command_queue = inclCreateCommandQueue(resource->context, resource->device_id))) {
		inclReleaseKernel(compute_unit->kernel);

		free(compute_unit->name);
		free(compute_unit);

		return INACCEL_FAILED;
	}

	unsigned int num_args;
	if (inclGetKernelInfo(compute_unit->kernel, CL_KERNEL_NUM_ARGS, sizeof(unsigned int), &num_args, NULL)) {
		inclReleaseCommandQueue(compute_unit->command_queue);
		inclReleaseKernel(compute_unit->kernel);

		free(compute_unit->name);
		free(compute_unit);

		return INACCEL_FAILED;
	}

	if (!(compute_unit->memory = (cl_memory *) calloc(num_args, sizeof(cl_memory)))) {
		perror("Error: calloc");

		inclReleaseCommandQueue(compute_unit->command_queue);
		inclReleaseKernel(compute_unit->kernel);

		free(compute_unit->name);
		free(compute_unit);

		return INACCEL_FAILED;
	}

	return compute_unit;
}

cl_memory create_memory(cl_resource resource, unsigned int index) {
	if (index < resource->mem_topology->m_count && resource->mem_topology->m_mem_data[index].m_used) {
		cl_memory memory = (cl_memory) calloc(1, sizeof(struct _cl_memory));
		if (!memory) {
			perror("Error: calloc");

			return INACCEL_FAILED;
		}

		memory->resource = resource;
		memory->index = index;

		#define CL_MEM_EXT_PTR_XILINX (1 << 31)
		struct cl_mem_ext_ptr_t {
			unsigned int flags;
			void *obj;
			void *param;
		} ext_ptr = {
			memory->index | CL_MEM_EXT_PTR_XILINX,
			NULL,
			0
		};
		if (!(memory->page = inclCreateBuffer(memory->resource->context, CL_MEM_EXT_PTR_XILINX | CL_MEM_WRITE_ONLY, 4096, &ext_ptr))) {
			free(memory);

			return INACCEL_FAILED;
		}

		memory->size = memory->resource->mem_topology->m_mem_data[memory->index].m_size * 1024 - 4096;

		if (!strncmp("bank", (char *) memory->resource->mem_topology->m_mem_data[memory->index].m_tag, strlen("bank"))) {
			if (!(memory->type = strdup("DDR"))) {
				perror("Error: strdup");

				inclReleaseMemObject(memory->page);

				free(memory);

				return INACCEL_FAILED;
			}
		} else {
			if (!(memory->type = strdup((char *) memory->resource->mem_topology->m_mem_data[memory->index].m_tag))) {
				perror("Error: strdup");

				inclReleaseMemObject(memory->page);

				free(memory);

				return INACCEL_FAILED;
			}

			char *brackets = strpbrk(memory->type, "[]");
			if (brackets) {
				*brackets = 0;
			}
		}

		return memory;
	}
	return NULL;
}

cl_resource create_resource(unsigned int index) {
	cl_resource resource = (cl_resource) calloc(1, sizeof(struct _cl_resource));
	if (!resource) {
		perror("Error: calloc");

		return INACCEL_FAILED;
	}

	resource->index = index;

	if (!(resource->platform_id = inclGetPlatformID("Xilinx"))) {
		free(resource);

		return NULL;
	}

	if (!(resource->device_id = inclGetDeviceID(resource->platform_id, resource->index))) {
		free(resource);

		return NULL;
	}

	if (!(resource->context = inclCreateContext(resource->device_id))) {
		free(resource);

		return INACCEL_FAILED;
	}

	if (!(resource->vendor = strdup("xilinx"))) {
		perror("Error: strdup");

		inclReleaseContext(resource->context);

		free(resource);

		return INACCEL_FAILED;
	}

	size_t raw_name_size;
	if (inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, 0, NULL, &raw_name_size)) {
		inclReleaseContext(resource->context);

		free(resource);

		return INACCEL_FAILED;
	}

	char *raw_name = (char *) calloc(raw_name_size, sizeof(char));
	if (!raw_name) {
		perror("Error: calloc");

		inclReleaseContext(resource->context);

		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}

	if (inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, raw_name_size, raw_name, NULL)) {
		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}

	if (!(resource->name = (char *) calloc(raw_name_size, sizeof(char)))) {
		perror("Error: calloc");

		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}

	if (!(resource->version = (char *) calloc(raw_name_size, sizeof(char)))) {
		perror("Error: calloc");

		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}

	const char *regex = "^xilinx_([^_]+)_(.*)_([^_]+)_([^_]+)$";

	regex_t compile;
	regmatch_t group[5];

	if (regcomp(&compile, regex, REG_EXTENDED)) {
		perror("Error: regcomp");

		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	if (regexec(&compile, raw_name, 5, group, 0)) {
		perror("Error: regexec");

		regfree(&compile);

		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	strncpy(resource->name, raw_name + group[1].rm_so, group[1].rm_eo - group[1].rm_so);

	strncpy(resource->version, raw_name + group[2].rm_so, group[2].rm_eo - group[2].rm_so);
	strcat(resource->version, "_");
	strncat(resource->version, raw_name + group[3].rm_so, group[3].rm_eo - group[3].rm_so);
	strcat(resource->version, ".");
	strncat(resource->version, raw_name + group[4].rm_so, group[4].rm_eo - group[4].rm_so);

	regfree(&compile);

	free(raw_name);

	#define CL_DEVICE_PCIE_BDF 0x1120

	size_t pcie_bdf_size;
	if (inclGetDeviceInfo(resource->device_id, CL_DEVICE_PCIE_BDF, 0, NULL, &pcie_bdf_size)) {
		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	char *pcie_bdf = (char *) calloc(pcie_bdf_size, sizeof(char));
	if (!pcie_bdf) {
		perror("Error: calloc");

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	if (inclGetDeviceInfo(resource->device_id, CL_DEVICE_PCIE_BDF, pcie_bdf_size, pcie_bdf, NULL)) {
		free(pcie_bdf);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	char root_path[PATH_MAX];
	if (sprintf(root_path, "/sys/bus/pci/devices/%s", pcie_bdf) < 0) {
		perror("Error: sprintf");

		free(pcie_bdf);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	free(pcie_bdf);

	if (!(resource->root_path = strdup(root_path))) {
		perror("Error: strdup");

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	char xmc_pattern[PATH_MAX];
	if (sprintf(xmc_pattern, "%s/xmc.*", resource->root_path) >= 0) {
		glob_t xmc;
		if (!glob(xmc_pattern, GLOB_NOSORT, NULL, &xmc)) {
			char serial_num_path[PATH_MAX];
			if (sprintf(serial_num_path, "%s/serial_num", xmc.gl_pathv[0]) > 0) {
				FILE *serial_num_stream = fopen(serial_num_path, "r");
				if (serial_num_stream) {
					fseek(serial_num_stream, 0, SEEK_END);

					char *serial_no = (char *) calloc(1, ftell(serial_num_stream) + 1);
					if (serial_no) {
						fseek(serial_num_stream, 0, SEEK_SET);

						if (fscanf(serial_num_stream, "%s", serial_no) != EOF) {
							resource->serial_no = strdup(serial_no);
						}
					}

					fclose(serial_num_stream);
				}
			}

			globfree(&xmc);
		}
	}

	if (pthread_create(&resource->thread, NULL, &sensor_routine, resource)) {
		perror("Error: pthread_create");

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->root_path);
		free(resource->serial_no);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	return resource;
}

size_t get_memory_size(cl_memory memory) {
	return memory->size;
}

char *get_memory_type(cl_memory memory) {
	return memory->type;
}

char *get_resource_name(cl_resource resource) {
	return resource->name;
}

float get_resource_power(cl_resource resource) {
	return resource->power;
}

char *get_resource_serial_no(cl_resource resource) {
	return resource->serial_no;
}

float get_resource_temperature(cl_resource resource) {
	return resource->temperature;
}

char *get_resource_vendor(cl_resource resource) {
	return resource->vendor;
}

char *get_resource_version(cl_resource resource) {
	return resource->version;
}

int program_resource_with_binary(cl_resource resource, size_t size, const void *binary) {
	if (resource->program) {
		inclReleaseProgram(resource->program);
		resource->program = NULL;
	}

	free(resource->mem_topology);
	resource->mem_topology = NULL;

	if (!(resource->program = inclCreateProgramWithBinary(resource->context, resource->device_id, size, binary))) {
		return EXIT_FAILURE;
	}

	if (inclBuildProgram(resource->program)) {
		inclReleaseProgram(resource->program);
		resource->program = NULL;

		return EXIT_FAILURE;
	}

	char icap_pattern[PATH_MAX];
	if (sprintf(icap_pattern, "%s/icap.*", resource->root_path) < 0) {
		perror("Error: sprintf");

		inclReleaseProgram(resource->program);
		resource->program = NULL;

		return EXIT_FAILURE;
	}

	glob_t icap;
	if (glob(icap_pattern, GLOB_NOSORT, NULL, &icap)) {
		perror("Error: glob");

		inclReleaseProgram(resource->program);
		resource->program = NULL;

		return EXIT_FAILURE;
	}

	char mem_topology_path[PATH_MAX];
	if (sprintf(mem_topology_path, "%s/mem_topology", icap.gl_pathv[0]) < 0) {
		perror("Error: sprintf");

		globfree(&icap);

		inclReleaseProgram(resource->program);
		resource->program = NULL;

		return EXIT_FAILURE;
	}

	globfree(&icap);

	FILE *mem_topology_stream = fopen(mem_topology_path, "r");
	if (!mem_topology_stream) {
		perror("Error: fopen");

		inclReleaseProgram(resource->program);
		resource->program = NULL;

		return EXIT_FAILURE;
	}

	int32_t m_count;
	if (fread(&m_count, sizeof(int32_t), 1, mem_topology_stream) != 1) {
		perror("Error: fread");

		fclose(mem_topology_stream);

		inclReleaseProgram(resource->program);
		resource->program = NULL;

		return EXIT_FAILURE;
	}

	fseek(mem_topology_stream, 0, SEEK_SET);

	if (!(resource->mem_topology = (struct mem_topology *) calloc(1, offsetof(struct mem_topology, m_mem_data) + m_count * sizeof(struct mem_data)))) {
		perror("Error: calloc");

		fclose(mem_topology_stream);

		inclReleaseProgram(resource->program);
		resource->program = NULL;

		return EXIT_FAILURE;
	}

	if (fread(resource->mem_topology, offsetof(struct mem_topology, m_mem_data) + m_count * sizeof(struct mem_data), 1, mem_topology_stream) != 1) {
		perror("Error: fread");

		fclose(mem_topology_stream);

		inclReleaseProgram(resource->program);
		resource->program = NULL;

		free(resource->mem_topology);
		resource->mem_topology = NULL;

		return EXIT_FAILURE;
	}

	fclose(mem_topology_stream);

	return EXIT_SUCCESS;
}

void release_buffer(cl_buffer buffer) {
	inclReleaseCommandQueue(buffer->command_queue);
	inclReleaseMemObject(buffer->mem);

	free(buffer);
}

void release_compute_unit(cl_compute_unit compute_unit) {
	inclReleaseCommandQueue(compute_unit->command_queue);
	inclReleaseKernel(compute_unit->kernel);

	free(compute_unit->memory);
	free(compute_unit->name);
	free(compute_unit);
}

void release_memory(cl_memory memory) {
	inclReleaseMemObject(memory->page);

	free(memory->type);
	free(memory);
}

void release_resource(cl_resource resource) {
	resource->release = 1;
	pthread_join(resource->thread, NULL);

	if (resource->program) {
		inclReleaseProgram(resource->program);
	}
	inclReleaseContext(resource->context);

	free(resource->mem_topology);
	free(resource->name);
	free(resource->root_path);
	free(resource->serial_no);
	free(resource->vendor);
	free(resource->version);
	free(resource);
}

int run_compute_unit(cl_compute_unit compute_unit) {
	if (inclEnqueueTask(compute_unit->command_queue, compute_unit->kernel)) {
		return EXIT_FAILURE;
	}

	unsigned int num_args;
	if (inclGetKernelInfo(compute_unit->kernel, CL_KERNEL_NUM_ARGS, sizeof(unsigned int), &num_args, NULL)) {
		return EXIT_FAILURE;
	}

	unsigned int index;
	for (index = 0; index < num_args; index++) {
		if (compute_unit->memory[index]) {
			if (inclSetKernelArg(compute_unit->kernel, index, sizeof(cl_mem), &compute_unit->memory[index]->page)) {
				return EXIT_FAILURE;
			};
		}
	}

	return EXIT_SUCCESS;
}

int set_compute_unit_arg(cl_compute_unit compute_unit, unsigned int index, size_t size, const void *value) {
	if (size) {
		return inclSetKernelArg(compute_unit->kernel, index, size, value);
	} else {
		cl_buffer buffer = (cl_buffer) value;

		compute_unit->memory[index] = buffer->memory;

		return inclSetKernelArg(compute_unit->kernel, index, sizeof(cl_mem), &buffer->mem);
	}
}
