#ifndef NDEBUG

#ifndef INACCEL_RUNTIME_INTERCEPT_H
#define INACCEL_RUNTIME_INTERCEPT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef INACCEL_RUNTIME_H
#define INACCEL_FAILED ((void *) -1)
#endif

#ifndef INACCEL_RUNTIME_H
typedef struct _cl_resource *cl_resource;
#endif

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("create_resource"), visibility ("hidden")))
#endif
cl_resource __inaccel_create_resource(unsigned int index);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_resource_vendor"), visibility ("hidden")))
#endif
char *__inaccel_get_resource_vendor(cl_resource resource);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_resource_name"), visibility ("hidden")))
#endif
char *__inaccel_get_resource_name(cl_resource resource);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_resource_version"), visibility ("hidden")))
#endif
char *__inaccel_get_resource_version(cl_resource resource);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_resource_pci_id"), visibility ("hidden")))
#endif
char *__inaccel_get_resource_pci_id(cl_resource resource);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_resource_serial_no"), visibility ("hidden")))
#endif
char *__inaccel_get_resource_serial_no(cl_resource resource);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_resource_power"), visibility ("hidden")))
#endif
float __inaccel_get_resource_power(cl_resource resource);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_resource_temperature"), visibility ("hidden")))
#endif
float __inaccel_get_resource_temperature(cl_resource resource);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("program_resource_with_binary"), visibility ("hidden")))
#endif
int __inaccel_program_resource_with_binary(cl_resource resource, size_t size, const void *binary);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("release_resource"), visibility ("hidden")))
#endif
void __inaccel_release_resource(cl_resource resource);

#ifndef INACCEL_RUNTIME_H
typedef struct _cl_memory *cl_memory;
#endif

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("create_memory"), visibility ("hidden")))
#endif
cl_memory __inaccel_create_memory(cl_resource resource, unsigned int index);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_memory_type"), visibility ("hidden")))
#endif
char *__inaccel_get_memory_type(cl_memory memory);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("get_memory_size"), visibility ("hidden")))
#endif
size_t __inaccel_get_memory_size(cl_memory memory);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("release_memory"), visibility ("hidden")))
#endif
void __inaccel_release_memory(cl_memory memory);

#ifndef INACCEL_RUNTIME_H
typedef struct _cl_buffer *cl_buffer;
#endif

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("create_buffer"), visibility ("hidden")))
#endif
cl_buffer __inaccel_create_buffer(cl_memory memory, size_t size, void *host);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("copy_to_buffer"), visibility ("hidden")))
#endif
int __inaccel_copy_to_buffer(cl_buffer buffer);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("copy_from_buffer"), visibility ("hidden")))
#endif
int __inaccel_copy_from_buffer(cl_buffer buffer);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("await_buffer_copy"), visibility ("hidden")))
#endif
int __inaccel_await_buffer_copy(cl_buffer buffer);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("release_buffer"), visibility ("hidden")))
#endif
void __inaccel_release_buffer(cl_buffer buffer);

#ifndef INACCEL_RUNTIME_H
typedef struct _cl_compute_unit *cl_compute_unit;
#endif

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("create_compute_unit"), visibility ("hidden")))
#endif
cl_compute_unit __inaccel_create_compute_unit(cl_resource resource, const char *name);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("set_compute_unit_arg"), visibility ("hidden")))
#endif
int __inaccel_set_compute_unit_arg(cl_compute_unit compute_unit, unsigned int index, size_t size, const void *value);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("run_compute_unit"), visibility ("hidden")))
#endif
int __inaccel_run_compute_unit(cl_compute_unit compute_unit);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("await_compute_unit_run"), visibility ("hidden")))
#endif
int __inaccel_await_compute_unit_run(cl_compute_unit compute_unit);

#ifdef INACCEL_RUNTIME_H
__attribute__ ((weak, alias("release_compute_unit"), visibility ("hidden")))
#endif
void __inaccel_release_compute_unit(cl_compute_unit compute_unit);

#ifdef __cplusplus
}
#endif

#endif // INACCEL_RUNTIME_INTERCEPT_H

#endif
