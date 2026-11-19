/* Stub de dlfcn.h para Switch 2: no hay carga dinámica de librerías.
 * El núcleo recibe vkGetInstanceProcAddr del frontend, así que nunca llama a dlopen. */
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_LOCAL  0
#define RTLD_GLOBAL 0x100
void* dlopen(const char* file, int mode);
void* dlsym(void* handle, const char* name);
int   dlclose(void* handle);
char* dlerror(void);
#ifdef __cplusplus
}
#endif
