#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>

#include "CL/opencl.h"
#include "runtime.h"

/* CL buffer struct (Type). */
struct _cl_buffer {
	cl_command_queue command_queue;
	void *host_ptr;
	cl_mem mem;
	size_t size;
};

/* CL compute unit struct (Type). */
struct _cl_compute_unit {
	cl_command_queue command_queue;
	cl_kernel kernel;
};

/* CL resource struct (Type). */
struct _cl_resource {
	cl_context context;
	cl_device_id device_id;
	cl_platform_id platform_id;
	cl_program program;
};

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
#if Intel
	return inclEnqueueReadBuffer(buffer->command_queue, buffer->mem, 0, buffer->size, buffer->host_ptr);
#endif
#if Xilinx
	return inclEnqueueMigrateMemObject(buffer->command_queue, buffer->mem, 1);
#endif
}

/**
 * Commands to write to a buffer object from host memory.
 * @param buffer Refers to a valid buffer object.
 * @return 0 on success; 1 on failure.
 */
int copy_to_buffer(cl_buffer buffer) {
#ifdef Intel
	return inclEnqueueWriteBuffer(buffer->command_queue, buffer->mem, 0, buffer->size, buffer->host_ptr);
#endif
#ifdef Xilinx
	return inclEnqueueMigrateMemObject(buffer->command_queue, buffer->mem, 0);
#endif
}

/**
 * Creates a buffer object.
 * @param resource A valid resource used to create the buffer object.
 * @param size The size in bytes of the buffer memory object to be allocated.
 * @param host_ptr A pointer to the buffer data that should already be allocated by the application. The size of the buffer that address points to must be greater than or equal to the size bytes.
 * @param memory_id The memory associated with this buffer.
 * @return The buffer.
 */
cl_buffer create_buffer(cl_resource resource, size_t size, void *host_ptr, unsigned int memory_id) {
	cl_buffer buffer = (cl_buffer) calloc(1, sizeof(struct _cl_buffer));

	buffer->size = size;
	buffer->host_ptr = host_ptr;

#ifdef Intel
	cl_uint CL_MEMORY = memory_id << 16;

	if (!(buffer->mem = inclCreateBuffer(resource->context, CL_MEM_READ_WRITE | CL_MEMORY, size, NULL))) goto CATCH;
#endif
#ifdef Xilinx
	cl_uint CL_MEM_EXT_PTR = 1 << 31;

	typedef struct{
		unsigned flags;
		void *obj;
		void *param;
	} cl_mem_ext_ptr_t;

	cl_uint CL_MEMORY = memory_id | (1 << 31);

	cl_mem_ext_ptr_t ext_ptr;
	ext_ptr.flags = CL_MEMORY;
	ext_ptr.obj = host_ptr;
	ext_ptr.param = 0;

	if (!(buffer->mem = inclCreateBuffer(resource->context, CL_MEM_EXT_PTR | CL_MEM_USE_HOST_PTR | CL_MEM_WRITE_ONLY, size, &ext_ptr))) goto CATCH;
#endif

	if (!(buffer->command_queue = inclCreateCommandQueue(resource->context, resource->device_id))) goto CATCH;

	return buffer;
CATCH:
	free(buffer);

	return NULL;
}

/**
 * Creates a compute unit object.
 * @param resource A valid resource used to create the compute unit object.
 * @param name A function name in the binary executable.
 * @return The compute unit.
 */
cl_compute_unit create_compute_unit(cl_resource resource, const char *name) {
	cl_compute_unit compute_unit = (cl_compute_unit) calloc(1, sizeof(struct _cl_compute_unit));

	if (!(compute_unit->kernel = inclCreateKernel(resource->program, name))) goto CATCH;

	if (!(compute_unit->command_queue = inclCreateCommandQueue(resource->context, resource->device_id))) goto CATCH;

	return compute_unit;
CATCH:
	free(compute_unit);

	return NULL;
}

/**
 * Creates a resource object.
 * @param device_id The device associated with this resource.
 * @return The resource.
 */
cl_resource create_resource(unsigned int device_id) {
	cl_resource resource = (cl_resource) calloc(1, sizeof(struct _cl_resource));

#ifdef Intel
	if (!(resource->platform_id = inclGetPlatformID("Intel"))) goto CATCH;
#endif
#ifdef Xilinx
	if (!(resource->platform_id = inclGetPlatformID("Xilinx"))) goto CATCH;
#endif

	if (!(resource->device_id = inclGetDeviceID(resource->platform_id, device_id))) goto CATCH;

	if (!(resource->context = inclCreateContext(resource->device_id))) goto CATCH;

	return resource;
CATCH:
	free(resource);

	return NULL;
}

/**
 * Used to get information that is common to all buffer objects.
 * @param buffer Specifies the buffer object being queried.
 * @return Return the host_ptr argument value specified when buffer is created.
 */
void *get_buffer_host_ptr(cl_buffer buffer) {
	return buffer->host_ptr;
}

/**
 * Used to get information that is common to all buffer objects.
 * @param buffer Specifies the buffer object being queried.
 * @return Return actual size of buffer in bytes.
 */
size_t get_buffer_size(cl_buffer buffer) {
	return buffer->size;
}

/**
 * Get specific information about a resource.
 * @param resource Refers to a valid resource object.
 * @return A "vendor | name | version" string.
 */
char *get_resource_info(cl_resource resource) {
#ifdef Intel
	size_t valid;
	inclGetPlatformInfo(resource->platform_id, CL_PLATFORM_NAME, 0, NULL, &valid);

	size_t raw_name_size;
	inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, 0, NULL, &raw_name_size);

	char *raw_name = (char *) malloc(raw_name_size * sizeof(char));
	if (!raw_name) {
		fprintf(stderr, "Error: malloc\n");
		return strdup("intel | - | -");
	}

	memset(raw_name, 0, raw_name_size * sizeof(char));

	inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, raw_name_size, raw_name, NULL);

	char *name = (char *) malloc(raw_name_size * sizeof(char));
	if (!name) {
		free(raw_name);
		fprintf(stderr, "Error: malloc\n");
		return strdup("intel | - | -");
	}

	memset(name, 0, raw_name_size * sizeof(char));

	long int major, minor;

	const char *regex = "^([^ : ]+) : .*_(..)0000(.).$";

	regex_t compile;
	regmatch_t group[4];

	if (regcomp(&compile, regex, REG_EXTENDED)) {
		free(raw_name);
		free(name);
		fprintf(stderr, "Error: regcomp\n");
		return strdup("intel | - | -");
	}

	if (!regexec(&compile, raw_name, 4, group, 0)) {
		strncpy(name, raw_name + group[1].rm_so, group[1].rm_eo - group[1].rm_so);

		char *tmp;

		tmp = strndup(raw_name + group[2].rm_so, group[2].rm_eo - group[2].rm_so);
		if (!tmp) {
			free(raw_name);
			free(name);
			fprintf(stderr, "Error: strndup\n");
			return strdup("intel | - | -");
		}
		major = strtol(tmp, NULL, 16);
		free(tmp);

		tmp = strndup(raw_name + group[3].rm_so, group[3].rm_eo - group[3].rm_so);
		if (!tmp) {
			free(raw_name);
			free(name);
			fprintf(stderr, "Error: strndup\n");
			return strdup("intel | - | -");
		}
		minor = strtol(tmp, NULL, 16);
		free(tmp);
	} else {
		free(raw_name);
		free(name);
		fprintf(stderr, "Error: regexec\n");
		return strdup("intel | - | -");
	}

	regfree(&compile);

	free(raw_name);

	size_t UUID = 32;

	char *version = (char *) malloc((UUID + 1) * sizeof(char));
	if (!version) {
		free(name);
		fprintf(stderr, "Error: malloc\n");
		return strdup("intel | - | -");
	}

	memset(version, 0, (UUID + 1) * sizeof(char));

	glob_t dev;
	if (!glob("/sys/devices/pci*/*/*/fpga/intel-fpga-dev.*/intel-fpga-port.*/dev", GLOB_NOSORT, NULL, &dev)) {
		size_t i;
		for (i = 0; i < dev.gl_pathc; i++) {
			long int dev_major;
			long int dev_minor;

			FILE *dev_file = fopen(dev.gl_pathv[i], "r");
			if (!dev_file) {
				continue;
			}
			if (fscanf(dev_file, "%ld:%ld", &dev_major, &dev_minor) == EOF) {
				continue;
			}
			if (fclose(dev_file) == EOF) {
				continue;
			}

			if ((major == dev_major) && (minor == dev_minor)) {
				char path[PATH_MAX];
				sprintf(path, "%s/intel-fpga-fme.*/pr/interface_id", dirname(dirname(dev.gl_pathv[i])));

				glob_t interface_id;
				if (!glob(path, GLOB_NOSORT, NULL, &interface_id)) {
					size_t j;
					for (j = 0; j < interface_id.gl_pathc; j++) {
						FILE *interface_id_file = fopen(interface_id.gl_pathv[j], "r");
						if (!interface_id_file) {
							continue;
						}
						if (!fgets(version, UUID + 1, interface_id_file)) {
							continue;
						}
						if (fclose(interface_id_file) == EOF) {
							continue;
						}
					}
					globfree(&interface_id);
				}

				break;
			}
		}
		globfree(&dev);
	}

	if (!strlen(version)) {
		free(name);
		free(version);
		fprintf(stderr, "Error: strlen\n");
		return strdup("intel | - | -");
	}

	char *identifier = (char *) malloc(strlen("intel") + 3 + strlen(name) + 3 + strlen(version) + 1);
	if (!identifier) {
		free(name);
		free(version);
		fprintf(stderr, "Error: malloc\n");
		return strdup("intel | - | -");
	}

	memset(identifier, 0, strlen("intel") + 3 + strlen(name) + 3 + strlen(version) + 1);

	strcpy(identifier, "intel");
	strcat(identifier, " | ");
	strcat(identifier, name);
	strcat(identifier, " | ");
	strcat(identifier, version);

	free(name);
	free(version);

	return identifier;
#endif
#ifdef Xilinx
	size_t valid;
	inclGetPlatformInfo(resource->platform_id, CL_PLATFORM_NAME, 0, NULL, &valid);

	size_t raw_name_size;
	inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, 0, NULL, &raw_name_size);

	char *raw_name = (char *) malloc(raw_name_size * sizeof(char));
	if (!raw_name) {
		fprintf(stderr, "Error: malloc\n");
		return strdup("xilinx | - | -");
	}

	memset(raw_name, 0, raw_name_size * sizeof(char));

	inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, raw_name_size, raw_name, NULL);

	char *name = (char *) malloc(raw_name_size * sizeof(char));
	if (!name) {
		free(raw_name);
		fprintf(stderr, "Error: malloc\n");
		return strdup("xilinx | - | -");
	}

	memset(name, 0, raw_name_size * sizeof(char));

	char *version = (char *) malloc(raw_name_size * sizeof(char));
	if (!version) {
		free(raw_name);
		free(name);
		fprintf(stderr, "Error: malloc\n");
		return strdup("xilinx | - | -");
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
		return strdup("xilinx | - | -");
	}

	if (!regexec(&compile, raw_name, 5, group, 0)) {
		strncpy(name, raw_name + group[1].rm_so, group[1].rm_eo - group[1].rm_so);

		strncpy(version, raw_name + group[2].rm_so, group[2].rm_eo - group[2].rm_so);
		strcat(version, "_");
		strncat(version, raw_name + group[3].rm_so, group[3].rm_eo - group[3].rm_so);
		strcat(version, ".");
		strncat(version, raw_name + group[4].rm_so, group[4].rm_eo - group[4].rm_so);
	} else {
		fprintf(stderr, "Error: regexec\n");
		return strdup("xilinx | - | -");
	}

	regfree(&compile);

	free(raw_name);

	char *identifier = (char *) malloc(strlen("xilinx") + 3 + strlen(name) + 3 + strlen(version) + 1);
	if (!identifier) {
		free(name);
		free(version);
		fprintf(stderr, "Error: malloc\n");
		return strdup("xilinx | - | -");
	}

	memset(identifier, 0, strlen("xilinx") + 3 + strlen(name) + 3 + strlen(version) + 1);

	strcpy(identifier, "xilinx");
	strcat(identifier, " | ");
	strcat(identifier, name);
	strcat(identifier, " | ");
	strcat(identifier, version);

	free(name);
	free(version);

	return identifier;
#endif
}

/**
 * Loads the specified binary executable bits into the resource object.
 * @param resource Refers to a valid resource object.
 * @param length The size in bytes of the binary to be loaded.
 * @param binary Pointer to binary to be loaded. The binary specified contains the bits that describe the executable that will be run on the associated resource.
 * @return 0 on success; 1 on failure.
 */
int program_resource_with_binary(cl_resource resource, size_t length, const unsigned char *binary) {
	if (resource->program) if (inclReleaseProgram(resource->program)) goto CATCH;

	if (!(resource->program = inclCreateProgramWithBinary(resource->context, resource->device_id, length, binary))) goto CATCH;

	if (inclBuildProgram(resource->program)) goto CATCH;

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
 * Deletes a resource object.
 * @param resource Refers to a valid resource object.
 */
void release_resource(cl_resource resource) {
	if (resource->program) inclReleaseProgram(resource->program);

	inclReleaseContext(resource->context);

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
int set_compute_unit_arg(cl_compute_unit compute_unit, unsigned int index, size_t size, void *value) {
	if (size) {
		return inclSetKernelArg(compute_unit->kernel, index, size, value);
	} else {
		cl_buffer buffer = (cl_buffer) value;

		return inclSetKernelArg(compute_unit->kernel, index, sizeof(cl_mem), &buffer->mem);
	}
}
