#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "CL/opencl.h"
#include "runtime.h"

#include "CL/cl_ext_xilinx.h"
#include "xrt/xclhal2.h"

#define RESOURCE_DEVICES 2

#define ARP_DISCOVERY 0x5500
#define ARP_IP_ADDR_OFFSET 0x5000
#define ARP_MAC_ADDR_OFFSET 0x4800
#define ARP_VALID_OFFSET 0x5400
#define IP_ADDR_OFFSET 0x0088
#define MAC_ADDR_OFFSET 0x0080
#define NUM_SOCKETS_HW 0x2210
#define UDP_OFFSET 0x2000

#define TAR_SIZE_LOCATION 124
#define TAR_SIZE_BYTES 11
#define TAR_BLOCK_SIZE 512

typedef struct {
	uint32_t theirIP;
	uint16_t theirPort;
	uint16_t myPort;
	bool valid;
} socket_type;

/* CL buffer struct (Type). */
struct _cl_buffer {
	cl_command_queue command_queue;
	cl_mem mem;
};

/* CL compute unit struct (Type). */
struct _cl_compute_unit {
	cl_command_queue command_queue;
	cl_kernel kernel;
};

/* CL memory struct (Type). */
struct _cl_memory {
	unsigned int index;
	unsigned int id;
	cl_resource resource;
};

/* CL resource struct (Type). */
struct _cl_resource {
	// Virtual device variables
	cl_platform_id platform_id;
	cl_context context[2];
	cl_device_id device_id[2];
	cl_program program[2];
	cl_kernel cmac[2];
	cl_kernel networklayer[2];
	char *binary_name[2];
	xuid_t xclbin_id[2];

	// Physical device variables
	char *name;
	char *version;
	char *vendor;

	char *root_path[2];
	char *temperature[2];
	char *power[2];
	char *serial_no[2];
	struct mem_topology *topology[2];
};

unsigned int tar_file_size(const char *current_char){
	unsigned int size = TAR_SIZE_BYTES;
	unsigned int output = 0;

	while(size > 0){
		output = output * 8 + *current_char - '0';
		current_char++;
		size--;
	}

	return output;
}

/**
 * Waits on the host thread until all previous copy commands are issued to the associated resource and have completed.
 * @param buffer Refers to a valid buffer object.
 * @return 0 on success; 1 on failure.
 */
int await_buffer_copy(cl_buffer buffer) {
	return inclFinish(buffer->command_queue);
}

/**
 * Waits on the host thread until all previous run commands are issued to the associated resource and have completed.
 * @param buffer Refers to a valid compute unit object.
 * @return 0 on success; 1 on failure.
 */
int await_compute_unit_run(cl_compute_unit compute_unit) {
	return inclFinish(compute_unit->command_queue);
}

/**
 * Commands to read from a buffer object to host memory.
 * @param buffer Refers to a valid buffer object.
 * @return 0 on success; 1 on failure.
 */
int copy_from_buffer(cl_buffer buffer) {
	return inclEnqueueMigrateMemObject(buffer->command_queue, buffer->mem, 1);

}

/**
 * Commands to write to a buffer object from host memory.
 * @param buffer Refers to a valid buffer object.
 * @return 0 on success; 1 on failure.
 */
int copy_to_buffer(cl_buffer buffer) {
	return inclEnqueueMigrateMemObject(buffer->command_queue, buffer->mem, 0);
}

/**
 * Creates a buffer object.
 * @param memory A valid memory used to create the buffer object.
 * @param size The size in bytes of the buffer memory object to be allocated.
 * @param array A pointer to the buffer data that should already be allocated by the application. The size of the buffer that address points to must be greater than or equal to the size bytes.
 * @return The buffer.
 */
cl_buffer create_buffer(cl_memory memory, size_t size, void *array) {
	cl_buffer buffer = (cl_buffer) calloc(1, sizeof(struct _cl_buffer));

	cl_uint CL_MEM_EXT_PTR = 1 << 31;

	typedef struct{
		unsigned flags;
		void *obj;
		void *param;
	} cl_mem_ext_ptr_t;

	cl_uint CL_MEMORY = memory->id | (1 << 31);

	cl_mem_ext_ptr_t ext_ptr;
	ext_ptr.flags = CL_MEMORY;
	ext_ptr.obj = array;
	ext_ptr.param = 0;

	if (!(buffer->mem = inclCreateBuffer(memory->resource->context[memory->index], CL_MEM_EXT_PTR | CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, size, &ext_ptr))) goto CATCH;

	if (!(buffer->command_queue = inclCreateCommandQueue(memory->resource->context[memory->index], memory->resource->device_id[memory->index]))) goto CATCH;

	return buffer;
CATCH:
	free(buffer);

	return NULL;
}

/**
 * Creates a compute unit object.
 * @param resource A valid resource used to create the compute unit object.
 * @param _name_ A function name in the binary executable.
 * @return The compute unit.
 */
cl_compute_unit create_compute_unit(cl_resource resource, const char *_name_) {
	cl_compute_unit compute_unit = (cl_compute_unit) calloc(1, sizeof(struct _cl_compute_unit));

	char *input = strdup(_name_);

	// split on ':' and get the function kernel_name as well as binary_name
	char *kernel_name = strtok(input, "-");
	char *binary_name = strtok(NULL, "-");

	unsigned int index;

	if (!strcmp(binary_name, resource->binary_name[0])) index = 0;
	else if (!strcmp(binary_name, resource->binary_name[1])) index = 1;
	else goto CATCH;

	if (!(compute_unit->kernel = inclCreateKernel(resource->program[index], kernel_name))) goto CATCH;

	if (!(compute_unit->command_queue = inclCreateCommandQueue(resource->context[index], resource->device_id[index]))) goto CATCH;

	free(input);

	return compute_unit;

CATCH:
	free(input);

	free(compute_unit);

	return NULL;
}

/**
 * Creates a memory object.
 * @param resource A valid resource used to create the memory object.
 * @param index The index associated with this memory.
 * @return The memory.
 */
cl_memory create_memory(cl_resource resource, unsigned int index) {
	if (resource->topology[0] && resource->topology[1] && (index < (resource->topology[0]->m_count + resource->topology[1]->m_count))) {
		cl_memory memory = (cl_memory) calloc(1, sizeof(struct _cl_memory));

		memory->resource = resource;

		if (index < resource->topology[0]->m_count) {
			memory->index = 0;
			memory->id = index;
		}
		else {
			memory->index = 1;
			memory->id = index - resource->topology[0]->m_count;
		}

		return memory;
	}

	return NULL;
}

/**
 * Creates a resource object.
 * @param device_id The device associated with this resource.
 * @return The resource.
 */
cl_resource create_resource(unsigned int device_id) {
	cl_resource resource = (cl_resource) calloc(1, sizeof(struct _cl_resource));

	resource->vendor = strdup("xilinx");

	if (!(resource->platform_id = inclGetPlatformID("Xilinx"))) goto CATCH;

	for (int i = 0; i < RESOURCE_DEVICES; i++) {
		if (!(resource->device_id[i] = inclGetDeviceID(resource->platform_id, 2 * device_id + i))) goto CATCH;

		if (!(resource->context[i] = inclCreateContext(resource->device_id[i]))) goto CATCH;
	}

	size_t raw_name_size;
	inclGetDeviceInfo(resource->device_id[0], CL_DEVICE_NAME, 0, NULL, &raw_name_size);

	char *raw_name = (char *) malloc(raw_name_size * sizeof(char));
	if (!raw_name) {
		fprintf(stderr, "Error: malloc\n");
		return resource;
	}

	memset(raw_name, 0, raw_name_size * sizeof(char));

	inclGetDeviceInfo(resource->device_id[0], CL_DEVICE_NAME, raw_name_size, raw_name, NULL);

	char *name = (char *) malloc((raw_name_size + 2) * sizeof(char));
	if (!name) {
		free(raw_name);
		fprintf(stderr, "Error: malloc\n");
		return resource;
	}

	memset(name, 0, (raw_name_size + 2) * sizeof(char));

	char *version = (char *) malloc(raw_name_size * sizeof(char));
	if (!version) {
		free(raw_name);
		free(name);
		fprintf(stderr, "Error: malloc\n");
		return resource;
	}

	memset(version, 0, raw_name_size * sizeof(char));

	const char *regex = "^xilinx_([^_]+)_(.*)_([^_]+)_([^_]+)$";

	regex_t compile;
	regmatch_t group[5];

	if (regcomp(&compile, regex, REG_EXTENDED)) {
		free(raw_name);
		free(name);
		free(version);
		fprintf(stderr, "Error: regcomp\n");
		return resource;
	}

	if (!regexec(&compile, raw_name, 5, group, 0)) {
		name[0] = '2';
		name[1] = 'x';
		strncpy(name + 2, raw_name + group[1].rm_so, group[1].rm_eo - group[1].rm_so);

		strncpy(version, raw_name + group[2].rm_so, group[2].rm_eo - group[2].rm_so);
		strcat(version, "_");
		strncat(version, raw_name + group[3].rm_so, group[3].rm_eo - group[3].rm_so);
		strcat(version, ".");
		strncat(version, raw_name + group[4].rm_so, group[4].rm_eo - group[4].rm_so);
	} else {
		fprintf(stderr, "Error: regexec\n");
		return resource;
	}

	resource->name = name;
	resource->version = version;

	regfree(&compile);

	free(raw_name);

	glob_t dev;
	unsigned idx = 0;

	if (!glob("/sys/bus/pci/drivers/{xocl,xuser}/*:*:*.*", GLOB_BRACE, NULL, &dev)) {
		for (int i = dev.gl_pathc; i > 0; i--) {
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
				if (idx == (2 * device_id)) {
					resource->root_path[0] = strdup(dirname(path));
				}

				if (idx == (2 * device_id + 1)) {
					resource->root_path[1] = strdup(dirname(path));

					break;
				}

				idx++;
			}
		}
	}

	globfree(&dev);

	for (int i = 0; i < RESOURCE_DEVICES; i++) {
		char path[PATH_MAX];
		sprintf(path, "%s/xmc.*", resource->root_path[i]);

		glob_t xmc;
		if (!glob(path, GLOB_NOSORT, NULL, &xmc)) {
			sprintf(path, "%s/%s", xmc.gl_pathv[0], "xmc_fpga_temp");
			resource->temperature[i] = strdup(path);

			sprintf(path, "%s/%s", xmc.gl_pathv[0], "xmc_power");
			resource->power[i] = strdup(path);


			sprintf(path, "%s/%s", xmc.gl_pathv[0], "serial_num");
			FILE *serial = fopen(path, "r");
			if (serial) {
				char serial_no[50];
				serial_no[0] = '\0';

				if (fscanf(serial,"%s", serial_no)) {
					resource->serial_no[i] = strdup(serial_no);
				}

				fclose(serial);
			}
		}
		globfree(&xmc);

		sprintf(path, "%s/icap.*", resource->root_path[i]);

		glob_t icap;
		if (!glob(path, GLOB_NOSORT, NULL, &icap)) {
			sprintf(path, "%s/%s", icap.gl_pathv[0], "mem_topology");

			FILE *mem_topology = fopen(path, "r");
			if (mem_topology) {
				//PATH_MAX bytes should be sufficient for mem_topology
				char *topology = (char *) malloc(PATH_MAX);
				size_t topology_bytes = fread(topology, sizeof(char), PATH_MAX, mem_topology);

				fclose(mem_topology);

				if (!topology_bytes) free(topology);
				else {
					resource->topology[i] = (struct mem_topology *) topology;
				}
			}
		}
		globfree(&icap);
	}

	return resource;
CATCH:
	free(resource);

	return NULL;
}

/**
 * Get the size of a memory.
 * @param memory Refers to a valid memory object.
 * @return The size of the memory in bytes
 */
size_t get_memory_size(cl_memory memory) {
	// Cross-check that the memory still exists in the topology
	if (memory->resource->topology[memory->index]->m_mem_data[memory->id].m_used) {
		return memory->resource->topology[memory->index]->m_mem_data[memory->id].m_size * 1024;
	}

	return 0;
}


/**
* Get the type of a memory.
* @param memory Refers to a valid memory object.
* @return The memory type.
*/
char *get_memory_type(cl_memory memory) {
	if (memory->resource->topology[memory->index]) {
		char *tag = (char *) malloc(strlen((const char *) memory->resource->topology[memory->index]->m_mem_data[memory->id].m_tag));
		strcpy(tag, (const char *) memory->resource->topology[memory->index]->m_mem_data[memory->id].m_tag);

		strtok(tag, "[");

		if (strcmp(tag, "bank0") && strcmp(tag, "bank1") && strcmp(tag, "bank2") && strcmp(tag, "bank3")) {
			return tag;
		} else{
			free(tag);
			return strdup("DDR");
		}

	} else {
		return strdup("-");
	}
}

/**
 * Get the name of a resource.
 * @param resource Refers to a valid resource object.
 * @return The name.
 */
char *get_resource_name(cl_resource resource) {
	if (resource->name)
		return resource->name;
	else
		return strdup("-");
}

/**
 * Get the power consumption of a resource.
 * @param resource Refers to a valid resource object.
 * @return The power consumed.
 */
float get_resource_power(cl_resource resource) {
	float value = 0;

	for (int i = 0; i < RESOURCE_DEVICES; i++) {
		if (resource->power[i]) {
			FILE *power_file = fopen(resource->power[i], "r");

			if (power_file) {
				char tmp[10] = {0};

				int ret = fscanf(power_file, "%s", tmp);
				fclose(power_file);

				if(ret != EOF) {
					// Power is obtained in uWatts so we convert it to Watts
					value += ((double) strtoul(tmp, NULL, 10)) / 1000000;
				}
			}
		}
	}

	return value;
}

/**
 * Get the serial number of a resource.
 * @param resource Refers to a valid resource object.
 * @return The serial number.
 */
char *get_resource_serial_no(cl_resource resource) {
	char *sn = (char *) malloc(1);
	sn[0] = '\0';

	if(resource->serial_no[0]) {
		char *sn_temp = strdup(resource->serial_no[0]);
		sn = (char *) realloc(sn, strlen(sn_temp) + 1 + strlen(sn));

		strcpy(sn, sn_temp);
		sn[strlen(sn_temp)] = ' ';
		sn[strlen(sn_temp) + 1] = '\0';

		free(sn_temp);
	}

	if(resource->serial_no[1]) {
		char *sn_temp = strdup(resource->serial_no[1]);
		sn = (char *) realloc(sn, strlen(sn_temp) + strlen(sn));

		strcat(sn, sn_temp);

		free(sn_temp);
	}

	return sn;
}

/**
 * Get the current temperature of a resource.
 * @param resource Refers to a valid resource object.
 * @return The current temperature of the resource in degrees Celsius.
 */
float get_resource_temperature(cl_resource resource) {
	float value = 0;

	for (int i = 0; i < RESOURCE_DEVICES; i++) {
		if (resource->temperature[i]) {
			FILE *temp_file = fopen(resource->temperature[i], "r");

			if (temp_file) {
				char temperature[4] = {0};

				int ret = fscanf(temp_file, "%s", temperature);
				fclose(temp_file);

				if(ret != EOF) {
					value += strtof(temperature, NULL);
				}
			}
		}
	}

	return value / 2;
}

/**
 * Get the vendor of a resource.
 * @param resource Refers to a valid resource object.
 * @return The vendor.
 */
char *get_resource_vendor(cl_resource resource) {
	if (resource->vendor)
		return resource->vendor;
	else
		return strdup("-");
}

/**
 * Get the version (bsp/shell) of a resource.
 * @param resource Refers to a valid resource object.
 * @return The version.
 */
char *get_resource_version(cl_resource resource) {
	if (resource->version)
		return resource->version;
	else
		return strdup("-");
}

inline void uuid_copy(xuid_t dst, const xuid_t src) {
	memcpy(dst, src, sizeof(xuid_t));
}

int network_setup(cl_resource resource) {
	cl_uint cmac_idx[2], networklayer_idx[2];
	xclDeviceHandle handle[2];
	socket_type sockets[2][16] = {0};

	int ip_address[2] = {0xc0a80004, 0xc0a80005};
	long mac_address[2] = {
		(0xf0f1f2f3f4f5 & 0xFFFFFFFFFF0) + (ip_address[0] & 0xF),
		(0xe0e1e2e3e4e5 & 0xFFFFFFFFFF0) + (ip_address[1] & 0xF)
	};

	unsigned num_sockets_hw = 0, num_sockets_sw = sizeof(sockets[0]) / sizeof(sockets[0][0]);

	sockets[0][0].theirIP = 0xc0a80005;
	sockets[0][0].theirPort = 50000;
	sockets[0][0].myPort = 60000;
	sockets[0][0].valid = true;

	sockets[1][0].theirIP = 0xc0a80004;
	sockets[1][0].theirPort = 60000;
	sockets[1][0].myPort = 50000;
	sockets[1][0].valid = true;

	for (int i = 0; i < RESOURCE_DEVICES; i++) {
		if (!(resource->cmac[i] = inclCreateKernel(resource->program[i], "cmac_1"))) return 0;
		if (!(resource->networklayer[i] = inclCreateKernel(resource->program[i], "networklayer"))) return 0;

		xclGetComputeUnitInfo(resource->cmac[i], 0, XCL_COMPUTE_UNIT_INDEX, sizeof(cmac_idx[i]), &(cmac_idx[i]), NULL);

		xclGetComputeUnitInfo(resource->networklayer[i], 0, XCL_COMPUTE_UNIT_INDEX, sizeof(networklayer_idx[i]), &(networklayer_idx[i]), NULL);

		clGetDeviceInfo(resource->device_id[i], CL_DEVICE_HANDLE, sizeof(handle[i]), &(handle[i]), NULL);
	}

	/*
	############################################################################
	################################## DEV 0 ###################################
	############################################################################
	*/

	xclOpenContext(handle[0], resource->xclbin_id[0], networklayer_idx[0], false);

	xclRegWrite(handle[0], networklayer_idx[0], MAC_ADDR_OFFSET, mac_address[0]);
	xclRegWrite(handle[0], networklayer_idx[0], MAC_ADDR_OFFSET + 4, mac_address[0] >> 32);
	xclRegWrite(handle[0], networklayer_idx[0], IP_ADDR_OFFSET, ip_address[0]);

	xclRegRead(handle[0], networklayer_idx[0], NUM_SOCKETS_HW, &num_sockets_hw);

	if (num_sockets_hw != num_sockets_sw) {
		printf("HW Socket list for device [0] should be [%d], is [%d]\n", num_sockets_sw, num_sockets_hw);
		fflush(stdout);

		xclCloseContext(handle[0], resource->xclbin_id[0], networklayer_idx[0]);

		return 1;
	}

	for (int i = 0; i < num_sockets_hw; i++) {
		uint32_t TI_OFFSET = 0x10 + i * 8;
		uint32_t TP_OFFSET = TI_OFFSET + 16 * 8;
		uint32_t MP_OFFSET = TI_OFFSET + 16 * 8 * 2;
		uint32_t V_OFFSET  = TI_OFFSET + 16 * 8 * 3;

		xclRegWrite(handle[0], networklayer_idx[0], UDP_OFFSET + TI_OFFSET, sockets[0][i].theirIP);
		xclRegWrite(handle[0], networklayer_idx[0], UDP_OFFSET + TP_OFFSET, sockets[0][i].theirPort);
		xclRegWrite(handle[0], networklayer_idx[0], UDP_OFFSET + MP_OFFSET, sockets[0][i].myPort);
		xclRegWrite(handle[0], networklayer_idx[0], UDP_OFFSET + V_OFFSET, sockets[0][i].valid);
	}

	xclCloseContext(handle[0], resource->xclbin_id[0], networklayer_idx[0]);

	/*
	############################################################################
	################################## DEV 1 ###################################
	############################################################################
	*/

	xclOpenContext(handle[1], resource->xclbin_id[1], networklayer_idx[1], false);

	xclRegWrite(handle[1], networklayer_idx[1], MAC_ADDR_OFFSET, mac_address[1]);
	xclRegWrite(handle[1], networklayer_idx[1], MAC_ADDR_OFFSET + 4, mac_address[1] >> 32);
	xclRegWrite(handle[1], networklayer_idx[1], IP_ADDR_OFFSET, ip_address[1]);

	xclRegRead(handle[1], networklayer_idx[1], NUM_SOCKETS_HW, &num_sockets_hw);

	if (num_sockets_hw != num_sockets_sw) {
		printf("HW Socket list for device [1] should be [%d], is [%d]\n", num_sockets_sw, num_sockets_hw);
		fflush(stdout);

		xclCloseContext(handle[1], resource->xclbin_id[1], networklayer_idx[1]);

		return 1;
	}

	for (int i = 0; i < num_sockets_hw; i++) {
		uint32_t TI_OFFSET = 0x10 + i * 8;
		uint32_t TP_OFFSET = TI_OFFSET + 16 * 8;
		uint32_t MP_OFFSET = TI_OFFSET + 16 * 8 * 2;
		uint32_t V_OFFSET  = TI_OFFSET + 16 * 8 * 3;

		xclRegWrite(handle[1], networklayer_idx[1], UDP_OFFSET + TI_OFFSET, sockets[1][i].theirIP);
		xclRegWrite(handle[1], networklayer_idx[1], UDP_OFFSET + TP_OFFSET, sockets[1][i].theirPort);
		xclRegWrite(handle[1], networklayer_idx[1], UDP_OFFSET + MP_OFFSET, sockets[1][i].myPort);
		xclRegWrite(handle[1], networklayer_idx[1], UDP_OFFSET + V_OFFSET, sockets[1][i].valid);
	}

	for(int i = 0; i < 256; i++) {
		xclRegWrite(handle[1], networklayer_idx[1], ARP_VALID_OFFSET + (i / 4) * 4, 0);
	}

	xclRegWrite(handle[1], networklayer_idx[1], ARP_DISCOVERY, 0);
	xclRegWrite(handle[1], networklayer_idx[1], ARP_DISCOVERY, 1);
	xclRegWrite(handle[1], networklayer_idx[1], ARP_DISCOVERY, 0);

	xclCloseContext(handle[1], resource->xclbin_id[1], networklayer_idx[1]);

	/*
	############################################################################
	################################## DEV 0 ###################################
	############################################################################
	*/

	xclOpenContext(handle[0], resource->xclbin_id[0], networklayer_idx[0], false);

	for(int i = 0; i < 256; i++) {
		xclRegWrite(handle[0], networklayer_idx[0], ARP_VALID_OFFSET + (i / 4) * 4, 0);
	}

	xclRegWrite(handle[0], networklayer_idx[0], ARP_DISCOVERY, 0);
	xclRegWrite(handle[0], networklayer_idx[0], ARP_DISCOVERY, 1);
	xclRegWrite(handle[0], networklayer_idx[0], ARP_DISCOVERY, 0);

	xclCloseContext(handle[0], resource->xclbin_id[0], networklayer_idx[0]);

	xclOpenContext(handle[0], resource->xclbin_id[0], cmac_idx[0], false);

	unsigned led_status = 0;
	xclRegRead(handle[0], cmac_idx[0], 0x0080, &led_status);

	xclCloseContext(handle[0], resource->xclbin_id[0], cmac_idx[0]);

	if (led_status & 0x1) {
		printf("Ethernet Connection: Link\n");
		fflush(stdout);

		return 0;
	}
	else {
		printf("Ethernet Connection: No Link\n");
		fflush(stdout);
		return 1;
	}

}

/**
 * Loads the specified binary executable bits into the resource object.
 * @param resource Refers to a valid resource object.
 * @param size The size in bytes of the binary to be loaded.
 * @param binary Pointer to binary to be loaded. The binary specified contains the bits that describe the executable that will be run on the associated resource.
 * @return 0 on success; 1 on failure.
 */
int program_resource_with_binary(cl_resource resource, size_t size, const void *tar) {
	for (int i = 0; i < RESOURCE_DEVICES; i++) {
		if (resource->cmac[i]) if (inclReleaseKernel(resource->cmac[i])) goto CATCH;
		if (resource->networklayer[i]) if (inclReleaseKernel(resource->networklayer[i])) goto CATCH;

		if (resource->program[i]) if (inclReleaseProgram(resource->program[i])) goto CATCH;

		if (resource->binary_name[i]) free(resource->binary_name[i]);
	}

	unsigned int offset = 0;

	for (int i = 0; i < RESOURCE_DEVICES; i++) {
		if (offset >= size) goto CATCH;

		resource->binary_name[i] = strdup(tar + offset);
		offset += TAR_SIZE_LOCATION;
		unsigned int binary_size = tar_file_size(tar + offset);

		void *binary = malloc(binary_size);

		offset += TAR_BLOCK_SIZE - TAR_SIZE_LOCATION;
		memcpy(binary, tar + offset, binary_size);

		uuid_copy(resource->xclbin_id[i], ((struct axlf *) binary)->m_header.uuid);

		printf("Programming using binary: %s\n", resource->binary_name[i]);
		fflush(stdout);

		if (!(resource->program[i] = inclCreateProgramWithBinary(resource->context[i], resource->device_id[i], binary_size, binary))) goto CATCH;

		if (inclBuildProgram(resource->program[i])) goto CATCH;

		// mem_topology changes every time we program the device
		if (resource->topology[i]) free(resource->topology[i]);

		char path[PATH_MAX];
		sprintf(path, "%s/icap.*", resource->root_path[i]);

		glob_t icap;
		if (!glob(path, GLOB_NOSORT, NULL, &icap)) {
			sprintf(path, "%s/%s", icap.gl_pathv[0], "mem_topology");
			globfree(&icap);

			FILE *mem_topology = fopen(path, "r");
			if (mem_topology) {
				//PATH_MAX bytes should be sufficient for mem_topology
				char *topology = (char *) malloc(PATH_MAX);
				size_t topology_bytes = fread(topology, sizeof(char), PATH_MAX, mem_topology);

				fclose(mem_topology);

				if (!topology_bytes) {
					free(binary);
					free(topology);
					goto CATCH;
				}
				else {
					resource->topology[i] = (struct mem_topology *) topology;
				}
			}
		}

		free(binary);

		// Set offset for next binary in tar
		offset += (binary_size + (TAR_BLOCK_SIZE - 1)) & (~(TAR_BLOCK_SIZE - 1));
	}

	if (network_setup(resource)) goto CATCH;

	return EXIT_SUCCESS;

CATCH:
	return EXIT_FAILURE;
}

/**
 * Deletes a buffer object.
 * @param buffer Refers to a valid buffer object.
 */
void release_buffer(cl_buffer buffer) {
	inclReleaseCommandQueue(buffer->command_queue);

	inclReleaseMemObject(buffer->mem);

	free(buffer);
}

/**
 * Deletes a compute unit object.
 * @param compute_unit Refers to a valid compute unit object.
 */
void release_compute_unit(cl_compute_unit compute_unit) {
	inclReleaseCommandQueue(compute_unit->command_queue);

	inclReleaseKernel(compute_unit->kernel);

	free(compute_unit);
}

/**
 * Deletes a memory object.
 * @param memory Refers to a valid memory object.
 */
void release_memory(cl_memory memory) {
	if(memory) free(memory);
}

/**
 * Deletes a resource object.
 * @param resource Refers to a valid resource object.
 */
void release_resource(cl_resource resource) {
	if (resource->name) free(resource->name);
	if (resource->vendor) free(resource->vendor);
	if (resource->version) free(resource->version);

	for (int i = 0; i < RESOURCE_DEVICES; i++) {
		if(resource->binary_name[i]) free(resource->binary_name[i]);
		if (resource->cmac[i]) inclReleaseKernel(resource->cmac[i]);
		if (resource->networklayer[i]) inclReleaseKernel(resource->networklayer[i]);

		if (resource->program[i]) inclReleaseProgram(resource->program[i]);

		inclReleaseContext(resource->context[i]);

		if (resource->root_path[i]) free(resource->root_path[i]);
		if (resource->temperature[i]) free(resource->temperature[i]);
		if (resource->power[i]) free(resource->power[i]);
		if (resource->serial_no[i]) free(resource->serial_no[i]);
		if (resource->topology[i]) free(resource->topology[i]);
	}

	free(resource);
}

/**
 * Command to execute a compute unit on a resource.
 * @param buffer Refers to a valid compute unit object.
 * @return 0 on success; 1 on failure.
 */
int run_compute_unit(cl_compute_unit compute_unit) {
	return inclEnqueueTask(compute_unit->command_queue, compute_unit->kernel);
}

/**
 * Used to set the argument value for a specific argument of a compute unit.
 * @param buffer Refers to a valid compute unit object.
 * @param index The argument index.
 * @param size Specifies the size of the argument value. If the argument is a buffer object, the size is NULL.
 * @param value A pointer to data that should be used for argument specified by index. If the argument is a buffer the value entry will be the appropriate object. The buffer object must be created with the resource associated with the compute unit object.
 * @return 0 on success; 1 on failure.
 */
int set_compute_unit_arg(cl_compute_unit compute_unit, unsigned int index, size_t size, const void *value) {
	if (size) {
		return inclSetKernelArg(compute_unit->kernel, index, size, value);
	} else {
		cl_buffer buffer = (cl_buffer) value;

		return inclSetKernelArg(compute_unit->kernel, index, sizeof(cl_mem), &buffer->mem);
	}
}
