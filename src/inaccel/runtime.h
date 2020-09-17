#ifndef RUNTIME_H
#define RUNTIME_H

/* CL resource struct (API Type). */
typedef struct _cl_resource *cl_resource;

enum resource_options {
	NAME = 0,
	POWER,
	SERIAL_NO,
	TEMPERATURE,
	VENDOR,
	VERSION
};

/**
 * Creates a resource object.
 * @param index The index associated with this resource.
 * @return The resource.
 */
cl_resource create_resource(unsigned int index);

/**
 * Get specific information about a resource.
 * @param resource Refers to a valid resource object.
 * @return A "vendor | name | version" string.
 */
char *get_resource_info(cl_resource resource, enum resource_options option);

/**
 * Loads the specified binary executable bits into the resource object.
 * @param resource Refers to a valid resource object.
 * @param length The size in bytes of the binary to be loaded.
 * @param binary Pointer to binary to be loaded. The binary specified contains the bits that describe the executable that will be run on the associated resource.
 * @return 0 on success; 1 on failure.
 */
int program_resource_with_binary(cl_resource resource, size_t length, const unsigned char *binary);

/**
 * Deletes a resource object.
 * @param resource Refers to a valid resource object.
 */
void release_resource(cl_resource resource);

/* CL memory struct (API Type). */
typedef struct _cl_memory *cl_memory;

/**
 * Creates a memory object.
 * @param resource A valid resource used to create the memory object.
 * @param index The index associated with this memory.
 * @return The memory.
 */
cl_memory create_memory(cl_resource resource, unsigned int index);

/**
 * Get the size of a memory.
 * @param memory Refers to a valid memory object.
 * @return The size of the memory in bytes
 */
size_t get_memory_size(cl_memory memory);

/**
 * Deletes a memory object.
 * @param memory Refers to a valid memory object.
 */
void release_memory(cl_memory memory);

/* CL buffer struct (API Type). */
typedef struct _cl_buffer *cl_buffer;

/**
 * Creates a buffer object.
 * @param memory A valid memory used to create the buffer object.
 * @param size The size in bytes of the buffer memory object to be allocated.
 * @param host_ptr A pointer to the buffer data that should already be allocated by the application. The size of the buffer that address points to must be greater than or equal to the size bytes.
 * @return The buffer.
 */
cl_buffer create_buffer(cl_memory memory, size_t size, void *host_ptr);

/**
 * Used to get information that is common to all buffer objects.
 * @param buffer Specifies the buffer object being queried.
 * @return Return actual size of buffer in bytes.
 */
size_t get_buffer_size(cl_buffer buffer);

/**
 * Used to get information that is common to all buffer objects.
 * @param buffer Specifies the buffer object being queried.
 * @return Return the host_ptr argument value specified when buffer is created.
 */
void *get_buffer_host_ptr(cl_buffer buffer);

/**
 * Commands to write to a buffer object from host memory.
 * @param buffer Refers to a valid buffer object.
 * @return 0 on success; 1 on failure.
 */
int copy_to_buffer(cl_buffer buffer);

/**
 * Commands to read from a buffer object to host memory.
 * @param buffer Refers to a valid buffer object.
 * @return 0 on success; 1 on failure.
 */
int copy_from_buffer(cl_buffer buffer);

/**
 * Waits on the host thread until all previous copy commands are issued to the associated resource and have completed.
 * @param buffer Refers to a valid buffer object.
 * @return 0 on success; 1 on failure.
 */
int await_buffer_copy(cl_buffer buffer);

/**
 * Deletes a buffer object.
 * @param buffer Refers to a valid buffer object.
 */
void release_buffer(cl_buffer buffer);

/* CL compute unit struct (API Type). */
typedef struct _cl_compute_unit *cl_compute_unit;

/**
 * Creates a compute unit object.
 * @param resource A valid resource used to create the compute unit object.
 * @param name A function name in the binary executable.
 * @return The compute unit.
 */
cl_compute_unit create_compute_unit(cl_resource resource, const char *name);

/**
 * Used to set the argument value for a specific argument of a compute unit.
 * @param buffer Refers to a valid compute unit object.
 * @param index The argument index.
 * @param size Specifies the size of the argument value. If the argument is a buffer object, the size is NULL.
 * @param value A pointer to data that should be used for argument specified by index. If the argument is a buffer the value entry will be the appropriate object. The buffer object must be created with the resource associated with the compute unit object.
 * @return 0 on success; 1 on failure.
 */
int set_compute_unit_arg(cl_compute_unit compute_unit, unsigned int index, size_t size, void *value);

/**
 * Command to execute a compute unit on a resource.
 * @param buffer Refers to a valid compute unit object.
 * @return 0 on success; 1 on failure.
 */
int run_compute_unit(cl_compute_unit compute_unit);

/**
 * Waits on the host thread until all previous run commands are issued to the associated resource and have completed.
 * @param buffer Refers to a valid compute unit object.
 * @return 0 on success; 1 on failure.
 */
int await_compute_unit_run(cl_compute_unit compute_unit);

/**
 * Deletes a compute unit object.
 * @param compute_unit Refers to a valid compute unit object.
 */
void release_compute_unit(cl_compute_unit compute_unit);

#endif
