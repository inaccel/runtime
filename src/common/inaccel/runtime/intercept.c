#ifndef NDEBUG

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#include "intercept.h"

static struct timespec epoch;

__attribute__ ((constructor))
static void init() {
	clock_gettime(CLOCK_MONOTONIC, &epoch);
}

#define LOGGER \
struct timespec time = {}, time_returned = {}; \
long thread = syscall(SYS_gettid); \
static __thread unsigned int log;

#define LOG(format, ...) \
if (!getenv( __FUNCTION__) || strcmp(getenv( __FUNCTION__), "off")) { \
	clock_gettime(CLOCK_MONOTONIC, &time); \
	long diff_sec = 0, diff_nsec = 0; \
	diff_sec = difftime(time.tv_sec, epoch.tv_sec); \
	diff_nsec = time.tv_nsec - epoch.tv_nsec; \
	if (diff_nsec < 0) { \
		diff_sec--; \
		diff_nsec += 1000000000L; \
	} \
	fprintf(stderr, ">>>> %s(%ld#%u) at %ld.%09lds" format "\n", __FUNCTION__, thread, log, diff_sec, diff_nsec, ## __VA_ARGS__); \
}

#define LOG_RETURNED(format, ...) \
if (!getenv( __FUNCTION__) || strcmp(getenv(__FUNCTION__), "off")) { \
	clock_gettime(CLOCK_MONOTONIC, &time_returned); \
	long diff_sec = 0, diff_nsec = 0; \
	diff_sec = difftime(time_returned.tv_sec, time.tv_sec); \
	diff_nsec = time_returned.tv_nsec - time.tv_nsec; \
	if (diff_nsec < 0) { \
		diff_sec--; \
		diff_nsec += 1000000000L; \
	} \
	long sec = diff_sec; \
	long msec = diff_nsec / 1000000; \
	long usec = (diff_nsec / 1000) % 1000; \
	long nsec = diff_nsec % 1000; \
	if (!sec) { \
		if (!msec) { \
			if (!usec) { \
				fprintf(stderr, "<<<< %s(%ld#%u) returned in %ldns" format "\n", __FUNCTION__, thread, log++, nsec, ## __VA_ARGS__); \
			} else { \
				fprintf(stderr, "<<<< %s(%ld#%u) returned in %ldus and %ldns" format "\n", __FUNCTION__, thread, log++, usec, nsec, ## __VA_ARGS__); \
			} \
		} else { \
			fprintf(stderr, "<<<< %s(%ld#%u) returned in %ldms, %ldus and %ldns" format "\n", __FUNCTION__, thread, log++, msec, usec, nsec, ## __VA_ARGS__); \
		} \
	} else { \
		fprintf(stderr, "<<<< %s(%ld#%u) returned in %lds, %ldms, %ldus and %ldns" format "\n", __FUNCTION__, thread, log++, sec, msec, usec, nsec, ## __VA_ARGS__); \
	} \
}

cl_resource create_resource(unsigned int index) {
	LOGGER;
	LOG(": index = %u", index);
	cl_resource resource = __inaccel_create_resource(index);
	if (resource == INACCEL_FAILED) {
		LOG_RETURNED(": resource = (failed)");
	} else {
		LOG_RETURNED(": resource = %p", resource);
	}
	return resource;
}

char *get_resource_vendor(cl_resource resource) {
	LOGGER;
	LOG(": resource = %p", resource);
	char *vendor = __inaccel_get_resource_vendor(resource);
	LOG_RETURNED(": vendor = %s", vendor);
	return vendor;
}

char *get_resource_name(cl_resource resource) {
	LOGGER;
	LOG(": resource = %p", resource);
	char *name = __inaccel_get_resource_name(resource);
	LOG_RETURNED(": name = %s", name);
	return name;
}

char *get_resource_version(cl_resource resource) {
	LOGGER;
	LOG(": resource = %p", resource);
	char *version = __inaccel_get_resource_version(resource);
	LOG_RETURNED(": version = %s", version);
	return version;
}

char *get_resource_pci_id(cl_resource resource) {
	LOGGER;
	LOG(": resource = %p", resource);
	char *pci_id = __inaccel_get_resource_pci_id(resource);
	LOG_RETURNED(": pci_id = %s", pci_id);
	return pci_id;
}

char *get_resource_serial_no(cl_resource resource) {
	LOGGER;
	LOG(": resource = %p", resource);
	char *serial_no = __inaccel_get_resource_serial_no(resource);
	LOG_RETURNED(": serial_no = %s", serial_no);
	return serial_no;
}

float get_resource_power(cl_resource resource) {
	LOGGER;
	LOG(": resource = %p", resource);
	float power = __inaccel_get_resource_power(resource);
	LOG_RETURNED(": power = %f", power);
	return power;
}

float get_resource_temperature(cl_resource resource) {
	LOGGER;
	LOG(": resource = %p", resource);
	float temperature = __inaccel_get_resource_temperature(resource);
	LOG_RETURNED(": temperature = %f", temperature);
	return temperature;
}

int program_resource_with_binary(cl_resource resource, size_t size, const void *binary) {
	LOGGER;
	LOG(": resource = %p, size = %lu, binary = %p", resource, size, binary);
	int error = __inaccel_program_resource_with_binary(resource, size, binary);
	LOG_RETURNED(": error = %d", error);
	return error;
}

void release_resource(cl_resource resource) {
	LOGGER;
	LOG(": resource = %p", resource);
	__inaccel_release_resource(resource);
	LOG_RETURNED("");
}

cl_memory create_memory(cl_resource resource, unsigned int index) {
	LOGGER;
	LOG(": resource = %p, index = %u", resource, index);
	cl_memory memory = __inaccel_create_memory(resource, index);
	if (memory == INACCEL_FAILED) {
		LOG_RETURNED(": memory = (failed)");
	} else {
		LOG_RETURNED(": memory = %p", memory);
	}
	return memory;
}

char *get_memory_type(cl_memory memory) {
	LOGGER;
	LOG(": memory = %p", memory);
	char *type = __inaccel_get_memory_type(memory);
	LOG_RETURNED(": type = %s", type);
	return type;
}

size_t get_memory_size(cl_memory memory) {
	LOGGER;
	LOG(": memory = %p", memory);
	size_t size = __inaccel_get_memory_size(memory);
	LOG_RETURNED(": size = %lu", size);
	return size;
}

void release_memory(cl_memory memory) {
	LOGGER;
	LOG(": memory = %p", memory);
	__inaccel_release_memory(memory);
	LOG_RETURNED("");
}

cl_buffer create_buffer(cl_memory memory, size_t size, void *host) {
	LOGGER;
	LOG(": memory = %p, size = %lu, host = %p", memory, size, host);
	cl_buffer buffer = __inaccel_create_buffer(memory, size, host);
	if (buffer == INACCEL_FAILED) {
		LOG_RETURNED(": buffer = (failed)");
	} else {
		LOG_RETURNED(": buffer = %p", buffer);
	}
	return buffer;
}

int copy_to_buffer(cl_buffer buffer) {
	LOGGER;
	LOG(": buffer = %p", buffer);
	int error = __inaccel_copy_to_buffer(buffer);
	LOG_RETURNED(": error = %d", error);
	return error;
}

int copy_from_buffer(cl_buffer buffer) {
	LOGGER;
	LOG(": buffer = %p", buffer);
	int error = __inaccel_copy_from_buffer(buffer);
	LOG_RETURNED(": error = %d", error);
	return error;
}

int await_buffer_copy(cl_buffer buffer) {
	LOGGER;
	LOG(": buffer = %p", buffer);
	int error = __inaccel_await_buffer_copy(buffer);
	LOG_RETURNED(": error = %d", error);
	return error;
}

void release_buffer(cl_buffer buffer) {
	LOGGER;
	LOG(": buffer = %p", buffer);
	__inaccel_release_buffer(buffer);
	LOG_RETURNED("");
}

cl_compute_unit create_compute_unit(cl_resource resource, const char *name) {
	LOGGER;
	LOG(": resource = %p, name = %s", resource, name);
	cl_compute_unit compute_unit = __inaccel_create_compute_unit(resource, name);
	if (compute_unit == INACCEL_FAILED) {
		LOG_RETURNED(": compute_unit = (failed)");
	} else {
		LOG_RETURNED(": compute_unit = %p", compute_unit);
	}
	return compute_unit;
}

int set_compute_unit_arg(cl_compute_unit compute_unit, unsigned int index, size_t size, const void *value) {
	LOGGER;
	if (size) {
		LOG(": compute_unit = %p, index = %u, size = %lu, value = %p", compute_unit, index, size, value);
	} else {
		LOG(": compute_unit = %p, index = %u, buffer = %p", compute_unit, index, value);
	}
	int error = __inaccel_set_compute_unit_arg(compute_unit, index, size, value);
	LOG_RETURNED(": error = %d", error);
	return error;
}

int run_compute_unit(cl_compute_unit compute_unit) {
	LOGGER;
	LOG(": compute_unit = %p", compute_unit);
	int error = __inaccel_run_compute_unit(compute_unit);
	LOG_RETURNED(": error = %d", error);
	return error;
}

int await_compute_unit_run(cl_compute_unit compute_unit) {
	LOGGER;
	LOG(": compute_unit = %p", compute_unit);
	int error = __inaccel_await_compute_unit_run(compute_unit);
	LOG_RETURNED(": error = %d", error);
	return error;
}

void release_compute_unit(cl_compute_unit compute_unit) {
	LOGGER;
	LOG(": compute_unit = %p", compute_unit);
	__inaccel_release_compute_unit(compute_unit);
	LOG_RETURNED("");
}

#endif
