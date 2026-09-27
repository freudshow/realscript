#ifndef TTU_SCRIPT_RUNTIME_H
#define TTU_SCRIPT_RUNTIME_H

#include "ttu_script_host.h"
#include "vm.h"

#define TTU_NATIVE_MAX 256
#define TTU_NATIVE_NAME_MAX 96

typedef struct
{
    char name[TTU_NATIVE_NAME_MAX];
    int min_arity;
    int max_arity;
    uint32_t capability;
    Value (*function)(VM *vm, int argc, const Value *argv);
} TTUNativeFunction;

const TTUNativeFunction *ttu_script_get_native(int id);
int resolve_native(const char *name);
int ttu_script_register_native(const TTUNativeFunction *function);
void ttu_script_reset_native_functions(void);
int ttu_script_register_core_natives(void);
int ttu_script_register_host_natives(void);
void init_vm_with_host(VM *vm, const TTUScriptHostAPI *host, void *context,
                       uint32_t capabilities, const char *script_name);

#endif
