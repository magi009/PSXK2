/* Implementación vacía de dlfcn para el núcleo (ver compat/dlfcn.h) */
#include <stddef.h>
void* dlopen(const char* f, int m) { (void)f; (void)m; return NULL; }
void* dlsym(void* h, const char* n) { (void)h; (void)n; return NULL; }
int   dlclose(void* h) { (void)h; return 0; }
char* dlerror(void) { return (char*)"dlopen no disponible en Switch 2"; }
