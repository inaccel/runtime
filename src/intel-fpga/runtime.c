#include <glob.h>
#include <libgen.h>
#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "INCL/opencl.h"
#include <inaccel/runtime.h>

struct _cl_buffer {
	void *ptr;
	size_t size;
	cl_command_queue command_queue;
	cl_mem mem;
};

struct _cl_compute_unit {
	cl_command_queue command_queue;
	cl_kernel kernel;
};

struct _cl_memory {
	unsigned int id;
	cl_resource resource;
	size_t size;
	char *type;
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

	char *temperature;
	char *sdr;
	char *sensors;
};

#define SIGN_EXT(val, bitpos) (((val) ^ (1 << (bitpos))) - (1 << (bitpos)))

float intel_get_power(cl_resource resource) {
	unsigned char calc_params[6];
	unsigned char reading;

	if(!resource->sdr || !resource->sensors) return -1.0f;

	FILE *sdr_file = fopen(resource->sdr, "r");
	FILE *sensors_file = fopen(resource->sensors, "r");

	if (!sdr_file || !sensors_file) return -1.0f;

	if (fseek(sdr_file, 24, SEEK_SET)) return -1.0f;

	int ret;
	if(!(ret = fread(calc_params, sizeof(char), 6, sdr_file))) return -1.0f;

	fclose(sdr_file);

	if (fseek(sensors_file, 4, SEEK_SET)) return -1.0f;

	if(!(ret = fread(&reading, sizeof(char), 1, sensors_file))) return -1.0f;

	fclose(sensors_file);

	int32_t B_val = ((calc_params[3] >> 6 & 0x3) << 8) | calc_params[2];
	B_val = SIGN_EXT(B_val, 9);

	int32_t M_val = ((calc_params[1] >> 6 & 0x3) << 8) | calc_params[0];
	M_val = SIGN_EXT(M_val, 9);

	int32_t R_exp = calc_params[5] >> 4 & 0x0F;
	R_exp = SIGN_EXT(R_exp, 3);

	int32_t B_exp = calc_params[5] & 0x0F;
	B_exp = SIGN_EXT(B_exp, 3);

	double M = M_val;
	double B = B_val;

	int i;
	if (B_exp >= 0) {
		for (i = 0; i < B_exp; i++) {
			B *= 10.0;
		}
	} else {
		for (i = B_exp; i; i++) {
			B /= 10.0;
		}
	}

	double sensor_value = M * reading + B;

	if (R_exp >= 0) {
		for (i = 0; i < R_exp; i++) {
			sensor_value *= 10.0;
		}
	} else {
		for (i = R_exp; i; i++) {
			sensor_value /= 10.0;
		}
	}

	return sensor_value;
}

int await_buffer_copy(cl_buffer buffer) {
	return inclFinish(buffer->command_queue);
}

int await_compute_unit_run(cl_compute_unit compute_unit) {
	return inclFinish(compute_unit->command_queue);
}

int copy_from_buffer(cl_buffer buffer) {
	return inclEnqueueReadBuffer(buffer->command_queue, buffer->mem, 0, buffer->size, buffer->ptr);
}

int copy_to_buffer(cl_buffer buffer) {
	return inclEnqueueWriteBuffer(buffer->command_queue, buffer->mem, 0, buffer->size, buffer->ptr);
}

cl_buffer create_buffer(cl_memory memory, size_t size, void *host) {
	cl_uint CL_MEMORY = memory->id << 16;

	cl_buffer buffer = (cl_buffer) calloc(1, sizeof(struct _cl_buffer));
	if (!buffer) return INACCEL_FAILED;

	buffer->size = size;
	buffer->ptr = host;

	if (!(buffer->mem = inclCreateBuffer(memory->resource->context, CL_MEM_READ_WRITE | CL_MEMORY, size, NULL))) {
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
	if (!compute_unit) return INACCEL_FAILED;

	if (!(compute_unit->kernel = inclCreateKernel(resource->program, name))) {
		free(compute_unit);
		return NULL;
	}

	if (!(compute_unit->command_queue = inclCreateCommandQueue(resource->context, resource->device_id))) {
		inclReleaseKernel(compute_unit->kernel);
		free(compute_unit);
		return INACCEL_FAILED;
	}

	return compute_unit;
}

cl_memory create_memory(cl_resource resource, unsigned int index) {
	// Intel has only one memory (for now)
	if (!index) {
		cl_memory memory = (cl_memory) calloc(1, sizeof(struct _cl_memory));
		if (!memory) return INACCEL_FAILED;

		memory->id = index;
		memory->resource = resource;
		inclGetDeviceInfo(resource->device_id, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(memory->size), &memory->size, NULL);
		memory->type = strdup("DDR");

		return memory;
	} else return NULL;
}

cl_resource create_resource(unsigned int index) {
	cl_resource resource = (cl_resource) calloc(1, sizeof(struct _cl_resource));
	if (!resource) return INACCEL_FAILED;

	if (!(resource->platform_id = inclGetPlatformID("Intel"))) {
		free(resource);
		return NULL;
	}

	if (!(resource->device_id = inclGetDeviceID(resource->platform_id, index))) {
		free(resource);
		return NULL;
	}

	if (!(resource->context = inclCreateContext(resource->device_id))) {
		free(resource);
		return INACCEL_FAILED;
	}

	if (!(resource->vendor = strdup("intel"))) {
		perror("Error: strdup");
		inclReleaseContext(resource->context);
		free(resource);
		return INACCEL_FAILED;
	}

	size_t raw_name_size;
	inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, 0, NULL, &raw_name_size);

	char *raw_name = (char *) calloc(raw_name_size, sizeof(char));
	if (!raw_name) {
		perror("Error: calloc");
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return INACCEL_FAILED;
	}

	inclGetDeviceInfo(resource->device_id, CL_DEVICE_NAME, raw_name_size, raw_name, NULL);

	resource->name = (char *) calloc(raw_name_size, sizeof(char));
	if (!resource->name) {
		perror("Error: calloc");
		free(raw_name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return INACCEL_FAILED;
	}

	const char *regex = "^([^ : ]+) : .*_(.+)0000(.).$";

	regex_t compile;
	regmatch_t group[4];

	if (regcomp(&compile, regex, REG_EXTENDED)) {
		perror("Error: regcomp");
		free(resource->name);
		free(raw_name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return INACCEL_FAILED;
	}

	long int major, minor;

	if (!regexec(&compile, raw_name, 4, group, 0)) {
		strncpy(resource->name, raw_name + group[1].rm_so, group[1].rm_eo - group[1].rm_so);

		char *tmp;

		tmp = strndup(raw_name + group[2].rm_so, group[2].rm_eo - group[2].rm_so);
		if (!tmp) {
			perror("Error: strndup");
			free(resource->name);
			free(raw_name);
			free(resource->vendor);
			inclReleaseContext(resource->context);
			free(resource);
			return INACCEL_FAILED;
		}
		major = strtol(tmp, NULL, 16);
		free(tmp);

		tmp = strndup(raw_name + group[3].rm_so, group[3].rm_eo - group[3].rm_so);
		if (!tmp) {
			perror("Error: strndup");
			free(resource->name);
			free(raw_name);
			free(resource->vendor);
			inclReleaseContext(resource->context);
			free(resource);
			return INACCEL_FAILED;
		}
		minor = strtol(tmp, NULL, 16);
		free(tmp);
	} else {
		perror("Error: regexec");
		free(resource->name);
		free(raw_name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return INACCEL_FAILED;
	}

	regfree(&compile);

	free(raw_name);

	size_t UUID = 32;

	resource->version = (char *) calloc((UUID + 1), sizeof(char));
	if (!resource->version) {
		perror("Error: calloc");
		free(resource->name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		return INACCEL_FAILED;
	}

	glob_t dev;
	int glob_ret;
	if (!(glob_ret = glob("/sys/devices/pci*/*/{,/*}/fpga/intel-fpga-dev.*/intel-fpga-port.*/dev", GLOB_NOSORT | GLOB_BRACE, NULL, &dev))) {
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
				if (!(glob_ret = glob(path, GLOB_NOSORT, NULL, &interface_id))) {
					FILE *interface_id_file = fopen(interface_id.gl_pathv[0], "r");
					if (!interface_id_file) {
						break;
					}
					if (!fgets(resource->version, UUID + 1, interface_id_file)) {
						break;
					}
					fclose(interface_id_file);

					resource->root_path = strdup(dirname(dirname(interface_id.gl_pathv[0])));

					globfree(&interface_id);
				} else perror("Error: glob");

				break;
			}
		}
		globfree(&dev);

		if (i == dev.gl_pathc) glob_ret = GLOB_NOMATCH;
	} else perror("Error: glob");

	if (!strlen(resource->version)) {
		fprintf(stderr, "Error: strlen\n");
		free(resource->version);
		free(resource->name);
		free(resource->vendor);
		inclReleaseContext(resource->context);
		free(resource);
		if (glob_ret == GLOB_NOMATCH) return NULL;
		else return INACCEL_FAILED;
	}

	// temperature and power are optional (at least for now)
	char path[PATH_MAX];
	if (sprintf(path, "%s/%s", resource->root_path, "thermal_mgmt/temperature") < 0) return resource;
	resource->temperature = strdup(path);

	if (sprintf(path, "%s/%s", resource->root_path, "avmmi-bmc.*.auto/bmc_info") < 0) return resource;

	glob_t bmc;
	if (!glob(path, GLOB_NOSORT, NULL, &bmc)) {
		if (sprintf(path, "%s/%s", bmc.gl_pathv[0], "sdr") < 0) return resource;
		resource->sdr = strdup(path);

		if (sprintf(path, "%s/%s", bmc.gl_pathv[0], "sensors") < 0) return resource;
		resource->sensors = strdup(path);

		globfree(&bmc);
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
	return intel_get_power(resource);
}

char *get_resource_serial_no(cl_resource resource) {
	return NULL;
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

	return -1.0f;
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

	free(compute_unit);
}

void release_memory(cl_memory memory) {
	free(memory->type);

	free(memory);
}

void release_resource(cl_resource resource) {
	if (resource->program) inclReleaseProgram(resource->program);

	if (resource->temperature) free(resource->temperature);
	if (resource->sdr) free(resource->sdr);
	if (resource->sensors) free(resource->sensors);

	inclReleaseContext(resource->context);

	free(resource->name);
	free(resource->root_path);
	free(resource->vendor);
	free(resource->version);

	free(resource);
}

int run_compute_unit(cl_compute_unit compute_unit) {
	return inclEnqueueTask(compute_unit->command_queue, compute_unit->kernel);
}

int set_compute_unit_arg(cl_compute_unit compute_unit, unsigned int index, size_t size, const void *value) {
	if (size) {
		return inclSetKernelArg(compute_unit->kernel, index, size, value);
	} else {
		cl_buffer buffer = (cl_buffer) value;

		return inclSetKernelArg(compute_unit->kernel, index, sizeof(cl_mem), &buffer->mem);
	}
}
