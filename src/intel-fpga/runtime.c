#include <glob.h>
#include <inaccel/runtime.h>
#include <libgen.h>
#include <limits.h>
#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "inaccel/runtime/intercept.h"
#include "INCL/opencl.h"

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
};

struct _cl_memory {
	cl_resource resource;
	unsigned int index;

	size_t size;
	char *type;
};

struct _cl_resource {
	unsigned int index;

	char *name;
	char *pci_id;
	float power;
	char *serial_no;
	float temperature;
	char *vendor;
	char *version;

	cl_context context;
	cl_device_id device_id;
	cl_platform_id platform_id;
	cl_program program;

	pthread_t thread;
	unsigned char release;
	char *root_path;
};

static float get_power_1(char *spi_path) {
	char sensor_pattern[PATH_MAX];
	if (sprintf(sensor_pattern, "%s/sensor*", spi_path) < 0) {
		perror("Error: sprintf");

		return 0.0f;
	}

	glob_t sensor;
	if (glob(sensor_pattern, GLOB_NOSORT, NULL, &sensor)) {
		perror("Error: glob");

		return 0.0f;
	}

	float current_auxiliary = 0.0f;
	float current_pcie = 0.0f;
	float voltage_auxiliary = 0.0f;
	float voltage_pcie = 0.0f;

	size_t i;
	for (i = 0; i < sensor.gl_pathc; i++) {
		char name_path[PATH_MAX];
		if (sprintf(name_path, "%s/name", sensor.gl_pathv[i]) < 0) {
			perror("Error: sprintf");

			globfree(&sensor);

			return 0.0f;
		}

		FILE *name_stream = fopen(name_path, "r");
		if (!name_stream) {
			perror("Error: fopen");

			globfree(&sensor);

			return 0.0f;
		}

		char name[PATH_MAX];
		if (!fgets(name, sizeof(name), name_stream)) {
			perror("Error: fgets");

			fclose(name_stream);

			globfree(&sensor);

			return 0.0f;
		}

		fclose(name_stream);

		if (!strncmp("12v AUX Current", name, strlen("12v AUX Current"))) {
			char value_path[PATH_MAX];
			if (sprintf(value_path, "%s/value", sensor.gl_pathv[i]) < 0) {
				perror("Error: sprintf");

				globfree(&sensor);

				return 0.0f;
			}

			FILE *value_stream = fopen(value_path, "r");
			if (!value_stream) {
				perror("Error: fopen");

				globfree(&sensor);

				return 0.0f;
			}

			unsigned int value;
			if (fscanf(value_stream, "%u", &value) == EOF) {
				perror("Error: fscanf");

				fclose(value_stream);

				globfree(&sensor);

				return 0.0f;
			}

			fclose(value_stream);

			current_auxiliary = value / 1000.0f;
		}

		if (!strncmp("12v Backplane Current", name, strlen("12v Backplane Current"))) {
			char value_path[PATH_MAX];
			if (sprintf(value_path, "%s/value", sensor.gl_pathv[i]) < 0) {
				perror("Error: sprintf");

				globfree(&sensor);

				return 0.0f;
			}

			FILE *value_stream = fopen(value_path, "r");
			if (!value_stream) {
				perror("Error: fopen");

				globfree(&sensor);

				return 0.0f;
			}

			unsigned int value;
			if (fscanf(value_stream, "%u", &value) == EOF) {
				perror("Error: fscanf");

				fclose(value_stream);

				globfree(&sensor);

				return 0.0f;
			}

			fclose(value_stream);

			current_pcie = value / 1000.0f;
		}

		if (!strncmp("12v AUX Voltage", name, strlen("12v AUX Voltage"))) {
			char value_path[PATH_MAX];
			if (sprintf(value_path, "%s/value", sensor.gl_pathv[i]) < 0) {
				perror("Error: sprintf");

				globfree(&sensor);

				return 0.0f;
			}

			FILE *value_stream = fopen(value_path, "r");
			if (!value_stream) {
				perror("Error: fopen");

				globfree(&sensor);

				return 0.0f;
			}

			unsigned int value;
			if (fscanf(value_stream, "%u", &value) == EOF) {
				perror("Error: fscanf");

				fclose(value_stream);

				globfree(&sensor);

				return 0.0f;
			}

			fclose(value_stream);

			voltage_auxiliary = value / 1000.0f;
		}

		if (!strncmp("12v Backplane Voltage", name, strlen("12v Backplane Voltage"))) {
			char value_path[PATH_MAX];
			if (sprintf(value_path, "%s/value", sensor.gl_pathv[i]) < 0) {
				perror("Error: sprintf");

				globfree(&sensor);

				return 0.0f;
			}

			FILE *value_stream = fopen(value_path, "r");
			if (!value_stream) {
				perror("Error: fopen");

				globfree(&sensor);

				return 0.0f;
			}

			unsigned int value;
			if (fscanf(value_stream, "%u", &value) == EOF) {
				perror("Error: fscanf");

				fclose(value_stream);

				globfree(&sensor);

				return 0.0f;
			}

			fclose(value_stream);

			voltage_pcie = value / 1000.0f;
		}
	}

	globfree(&sensor);

	return (current_auxiliary * voltage_auxiliary) + (current_pcie * voltage_pcie);
}

static float get_power_2(char *sdr_path, char *sensors_path) {
	#define SIGN_EXT(val, bitpos) (((val) ^ (1 << (bitpos))) - (1 << (bitpos)))

	unsigned char calc_params[6];
	unsigned char reading;

	FILE *sdr_stream = fopen(sdr_path, "r");
	if (!sdr_stream) {
		perror("Error: fopen");

		return 0.0f;
	}

	FILE *sensors_stream = fopen(sensors_path, "r");
	if (!sensors_stream) {
		perror("Error: fopen");

		fclose(sdr_stream);

		return 0.0f;
	}

	if (fseek(sdr_stream, 24, SEEK_SET)) {
		perror("Error: fseek");

		fclose(sdr_stream);
		fclose(sensors_stream);

		return 0.0f;
	}

	if (fread(calc_params, sizeof(unsigned char), 6, sdr_stream) != 6) {
		perror("Error: fread");

		fclose(sdr_stream);
		fclose(sensors_stream);

		return 0.0f;
	}

	fclose(sdr_stream);

	if (fseek(sensors_stream, 4, SEEK_SET)) {
		perror("Error: fseek");

		fclose(sensors_stream);

		return 0.0f;
	}

	if (fread(&reading, sizeof(unsigned char), 1, sensors_stream) != 1) {
		perror("Error: fread");

		fclose(sensors_stream);

		return 0.0f;
	}

	fclose(sensors_stream);

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

	if (B_exp >= 0) {
		int i;
		for (i = 0; i < B_exp; i++) {
			B *= 10.0;
		}
	} else {
		int i;
		for (i = B_exp; i; i++) {
			B /= 10.0;
		}
	}

	double sensor_value = M * reading + B;

	if (R_exp >= 0) {
		int i;
		for (i = 0; i < R_exp; i++) {
			sensor_value *= 10.0;
		}
	} else {
		int i;
		for (i = R_exp; i; i++) {
			sensor_value /= 10.0;
		}
	}

	return sensor_value;
}

static float get_temperature_1(char *spi_path) {
	char sensor_pattern[PATH_MAX];
	if (sprintf(sensor_pattern, "%s/sensor*", spi_path) < 0) {
		perror("Error: sprintf");

		return 0.0f;
	}

	glob_t sensor;
	if (glob(sensor_pattern, GLOB_NOSORT, NULL, &sensor)) {
		perror("Error: glob");

		return 0.0f;
	}

	float temperature = 0.0f;

	size_t i;
	for (i = 0; i < sensor.gl_pathc; i++) {
		char name_path[PATH_MAX];
		if (sprintf(name_path, "%s/name", sensor.gl_pathv[i]) < 0) {
			perror("Error: sprintf");

			globfree(&sensor);

			return 0.0f;
		}

		FILE *name_stream = fopen(name_path, "r");
		if (!name_stream) {
			perror("Error: fopen");

			globfree(&sensor);

			return 0.0f;
		}

		char name[PATH_MAX];
		if (!fgets(name, PATH_MAX, name_stream)) {
			perror("Error: fgets");

			fclose(name_stream);

			globfree(&sensor);

			return 0.0f;
		}

		fclose(name_stream);

		if (!strncmp("FPGA Core Temperature", name, strlen("FPGA Core Temperature"))) {
			char value_path[PATH_MAX];
			if (sprintf(value_path, "%s/value", sensor.gl_pathv[i]) < 0) {
				perror("Error: sprintf");

				globfree(&sensor);

				return 0.0f;
			}

			FILE *value_stream = fopen(value_path, "r");
			if (!value_stream) {
				perror("Error: fopen");

				globfree(&sensor);

				return 0.0f;
			}

			unsigned int value;
			if (fscanf(value_stream, "%u", &value) == EOF) {
				perror("Error: fscanf");

				fclose(value_stream);

				globfree(&sensor);

				return 0.0f;
			}

			fclose(value_stream);

			temperature = value / 1000.0f;
		}
	}

	globfree(&sensor);

	return temperature;
}

static float get_temperature_2(char *temperature_path) {
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

static void *sensor_routine(void *arg) {
	cl_resource resource = (cl_resource) arg;

	char sdr_path[PATH_MAX] = {0};
	char sensors_path[PATH_MAX] = {0};
	char spi_path[PATH_MAX] = {0};
	char temperature_path[PATH_MAX] = {0};

	char bmc_pattern[PATH_MAX];
	if (sprintf(bmc_pattern, "%s/avmmi-bmc.*.auto/bmc_info", resource->root_path) >= 0) {
		glob_t bmc;
		if (!glob(bmc_pattern, GLOB_NOSORT, NULL, &bmc)) {
			sprintf(sdr_path, "%s/sdr", bmc.gl_pathv[0]);
			sprintf(sensors_path, "%s/sensors", bmc.gl_pathv[0]);
			globfree(&bmc);
		}
	}
	char spi_pattern[PATH_MAX];
	if (sprintf(spi_pattern, "%s/spi-altera*.auto/spi_master/spi*/spi*.*", resource->root_path) >= 0) {
		glob_t spi;
		if (!glob(spi_pattern, GLOB_NOSORT, NULL, &spi)) {
			sprintf(spi_path, "%s", spi.gl_pathv[0]);
			globfree(&spi);
		}
	}
	sprintf(temperature_path, "%s/thermal_mgmt/temperature", resource->root_path);

	if ((!strlen(sdr_path) || !strlen(sensors_path)) && !strlen(temperature_path)) {
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

		if (strlen(spi_path)) {
			resource->power = get_power_1(spi_path);
		} else if (strlen(sdr_path) && strlen(sensors_path)) {
			resource->power = get_power_2(sdr_path, sensors_path);
		}
		if (strlen(spi_path)) {
			resource->temperature = get_temperature_1(spi_path);
		} else if (strlen(temperature_path)) {
			resource->temperature = get_temperature_2(temperature_path);
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
	return inclEnqueueReadBuffer(buffer->command_queue, buffer->mem, 0, buffer->size, buffer->host);
}

int copy_to_buffer(cl_buffer buffer) {
	return inclEnqueueWriteBuffer(buffer->command_queue, buffer->mem, 0, buffer->size, buffer->host);
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

	if (!(buffer->mem = inclCreateBuffer(memory->resource->context, CL_MEM_READ_WRITE, buffer->size, NULL))) {
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

	if (!(compute_unit->kernel = inclCreateKernel(resource->program, compute_unit->name))) {
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

	return compute_unit;
}

cl_memory create_memory(cl_resource resource, unsigned int index) {
	if (!index) {
		cl_memory memory = (cl_memory) calloc(1, sizeof(struct _cl_memory));
		if (!memory) {
			perror("Error: calloc");

			return INACCEL_FAILED;
		}

		memory->resource = resource;
		memory->index = index;

		if (inclGetDeviceInfo(resource->device_id, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(size_t), &memory->size, NULL)) {
			free(memory);

			return INACCEL_FAILED;
		}

		if (!(memory->type = strdup("DDR"))) {
			perror("Error: strdup");

			free(memory);

			return INACCEL_FAILED;
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

	if (!(resource->platform_id = inclGetPlatformID("Intel"))) {
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

	if (!(resource->vendor = strdup("intel"))) {
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

	const char *regex = "^([^ : ]+) : .*_(.+)0000(.).$";

	regex_t compile;
	regmatch_t group[4];

	if (regcomp(&compile, regex, REG_EXTENDED)) {
		perror("Error: regcomp");

		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}

	if (regexec(&compile, raw_name, 4, group, 0)) {
		perror("Error: regexec");

		regfree(&compile);

		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}

	strncpy(resource->name, raw_name + group[1].rm_so, group[1].rm_eo - group[1].rm_so);

	char *tmp;

	tmp = strndup(raw_name + group[2].rm_so, group[2].rm_eo - group[2].rm_so);
	if (!tmp) {
		perror("Error: strndup");

		regfree(&compile);

		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}
	long major = strtol(tmp, NULL, 16);
	free(tmp);

	tmp = strndup(raw_name + group[3].rm_so, group[3].rm_eo - group[3].rm_so);
	if (!tmp) {
		perror("Error: strndup");

		regfree(&compile);

		free(raw_name);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}
	long minor = strtol(tmp, NULL, 16);
	free(tmp);

	regfree(&compile);

	free(raw_name);

	const size_t UUID = 32;

	if (!(resource->version = (char *) calloc((UUID + 1), sizeof(char)))) {
		perror("Error: calloc");

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource);

		return INACCEL_FAILED;
	}

	glob_t dev;
	if (glob("/sys/bus/pci/devices/*/fpga/intel-fpga-dev.*/intel-fpga-port.*/dev", GLOB_NOSORT, NULL, &dev)) {
		perror("Error: glob");

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return INACCEL_FAILED;
	}

	size_t i;
	for (i = 0; i < dev.gl_pathc; i++) {
		long dev_major;
		long dev_minor;

		FILE *dev_stream = fopen(dev.gl_pathv[i], "r");
		if (!dev_stream) {
			perror("Error: fopen");

			inclReleaseContext(resource->context);

			free(resource->name);
			free(resource->vendor);
			free(resource->version);
			free(resource);

			return INACCEL_FAILED;
		}

		if (fscanf(dev_stream, "%ld:%ld", &dev_major, &dev_minor) == EOF) {
			perror("Error: fscanf");

			fclose(dev_stream);

			inclReleaseContext(resource->context);

			free(resource->name);
			free(resource->vendor);
			free(resource->version);
			free(resource);

			return INACCEL_FAILED;
		}

		fclose(dev_stream);

		if ((major == dev_major) && (minor == dev_minor)) {
			char interface_id_pattern[PATH_MAX];
			if (sprintf(interface_id_pattern, "%s/intel-fpga-fme.*/pr/interface_id", dirname(dirname(dev.gl_pathv[i]))) < 0) {
				perror("Error: sprintf");

				globfree(&dev);

				inclReleaseContext(resource->context);

				free(resource->name);
				free(resource->vendor);
				free(resource->version);
				free(resource);

				return INACCEL_FAILED;
			}

			globfree(&dev);

			glob_t interface_id;
			if (glob(interface_id_pattern, GLOB_NOSORT, NULL, &interface_id)) {
				perror("Error: glob");

				inclReleaseContext(resource->context);

				free(resource->name);
				free(resource->vendor);
				free(resource->version);
				free(resource);

				return INACCEL_FAILED;
			}

			FILE *interface_id_stream = fopen(interface_id.gl_pathv[0], "r");
			if (!interface_id_stream) {
				perror("Error: fopen");

				globfree(&interface_id);

				inclReleaseContext(resource->context);

				free(resource->name);
				free(resource->vendor);
				free(resource->version);
				free(resource);

				return INACCEL_FAILED;
			}

			if (!fgets(resource->version, UUID + 1, interface_id_stream)) {
				perror("Error: fgets");

				globfree(&interface_id);

				fclose(interface_id_stream);

				inclReleaseContext(resource->context);

				free(resource->name);
				free(resource->vendor);
				free(resource->version);
				free(resource);

				return INACCEL_FAILED;
			}

			fclose(interface_id_stream);

			resource->root_path = strdup(dirname(dirname(interface_id.gl_pathv[0])));

			globfree(&interface_id);

			break;
		}
	}

	if (!strlen(resource->version)) {
		globfree(&dev);

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->vendor);
		free(resource->version);
		free(resource);

		return NULL;
	}

	char *root_path;
	if ((root_path = strdup(resource->root_path))) {
		resource->pci_id = strdup(basename(dirname(dirname(dirname(root_path)))));

		free(root_path);
	}

	if (pthread_create(&resource->thread, NULL, &sensor_routine, resource)) {
		perror("Error: pthread_create");

		inclReleaseContext(resource->context);

		free(resource->name);
		free(resource->root_path);
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

char *get_resource_pci_id(cl_resource resource) {
	return resource->pci_id;
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

	if (!(resource->program = inclCreateProgramWithBinary(resource->context, resource->device_id, size, binary))) {
		return EXIT_FAILURE;
	}

	if (inclBuildProgram(resource->program)) {
		inclReleaseProgram(resource->program);
		resource->program = NULL;

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

	free(compute_unit->name);
	free(compute_unit);
}

void release_memory(cl_memory memory) {
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

	free(resource->name);
	free(resource->pci_id);
	free(resource->root_path);
	free(resource->serial_no);
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
