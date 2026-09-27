#ifndef TTU_SCRIPT_H
#define TTU_SCRIPT_H

#include "vm.h"
#include <stdint.h>

#define TTU_NATIVE_MAX 128
#define TTU_NATIVE_NAME_MAX 96

typedef struct
{
    char name[TTU_NATIVE_NAME_MAX];
    int min_arity;
    int max_arity;
    uint32_t capability;
    NativeFunction function;
} TTUNative;

typedef int (*NativeResolver)(const char* name);
int resolve_native(const char* name);

int register_native_function(const TTUNative* function);
int register_native_module(const char* module_name, const TTUNative* functions, int count);
void reset_native_functions(void);
int find_native_function(const char* name);
const TTUNative* get_native_function(int id);
void set_native_resolver(NativeResolver resolver);
void register_default_ttu_natives(void);

#endif
