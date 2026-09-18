#include "dart_api_dl.h"
#include <stdio.h>
#include <string.h>

#define DART_API_DL_MAJOR_VERSION 2

typedef struct {
  const char* name;
  void (*function)(void);
} DartApiEntry;

typedef struct {
  int major;
  int minor;
  const DartApiEntry* functions;
} DartApi;

#define DART_API_DL_DEFINITIONS(name, R, A) name##_Type name##_DL = NULL;

DART_API_ALL_DL_SYMBOLS(DART_API_DL_DEFINITIONS)
DART_API_DEPRECATED_DL_SYMBOLS(DART_API_DL_DEFINITIONS)

#undef DART_API_DL_DEFINITIONS

static void* FindFunctionPointer(const DartApiEntry* entries, const char* name) {
  if (!entries) return NULL;
  while (entries->name != NULL) {
    if (strcmp(entries->name, name) == 0) return (void*)entries->function;
    entries++;
  }
  return NULL;
}

intptr_t Dart_InitializeApiDL(void* data) {
  if (!data) return -1;
  const DartApi* dart_api_data = (const DartApi*)data;

  const DartApiEntry* dart_api_function_pointers = dart_api_data->functions;

#define DART_API_DL_INIT(name, R, A) \
  name##_DL = (name##_Type)(FindFunctionPointer(dart_api_function_pointers, #name));
  DART_API_ALL_DL_SYMBOLS(DART_API_DL_INIT)
#undef DART_API_DL_INIT

  return 0;
}
