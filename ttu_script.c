#include "ttu_script.h"
#include "db.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static TTUNative native_functions[TTU_NATIVE_MAX];
static int native_function_count;
static NativeResolver native_resolver;

int register_native_function(const TTUNative* function)
{
    int index;
    if (function == NULL || function->function == NULL || native_function_count >= TTU_NATIVE_MAX ||
        function->min_arity < 0 || function->max_arity < function->min_arity ||
        function->name[0] == '\0') return -1;
    if (find_native_function(function->name) >= 0) return -1;
    index = native_function_count++;
    native_functions[index] = *function;
    return index;
}

int register_native_module(const char* module_name, const TTUNative* functions, int count)
{
    char name[TTU_NATIVE_NAME_MAX];
    int i;
    int index;
    if (module_name == NULL || functions == NULL || count < 0) return -1;
    for (i = 0; i < count; ++i)
    {
        int written = snprintf(name, sizeof(name), "%s.%s", module_name, functions[i].name);
        if (written < 0 || (size_t)written >= sizeof(name)) return -1;
        {
            TTUNative item = functions[i];
            memcpy(item.name, name, (size_t)written + 1);
            index = register_native_function(&item);
        }
        if (index < 0) return -1;
    }
    return 0;
}

void reset_native_functions(void)
{
    memset(native_functions, 0, sizeof(native_functions));
    native_function_count = 0;
}

int find_native_function(const char* name)
{
    int i;
    if (name == NULL) return -1;
    for (i = 0; i < native_function_count; ++i)
        if (strcmp(native_functions[i].name, name) == 0) return i;
    return -1;
}

const TTUNative* get_native_function(int id)
{
    if (id < 0 || id >= native_function_count) return NULL;
    return &native_functions[id];
}

void set_native_resolver(NativeResolver resolver)
{
    native_resolver = resolver;
}

static Value native_now_ms(void* context, int argc, const Value* argv)
{
    (void)context; (void)argc; (void)argv;
    return int_val((int64_t)time(NULL) * 1000);
}
static Value native_rtdb_read(void* context, int argc, const Value* argv)
{
    int point = (int)as_int(argv[0]);
    (void)context; (void)argc;
    return double_val(GetValueByRealNo(point));
}
static Value native_rtdb_write(void* context, int argc, const Value* argv)
{
    int point = (int)as_int(argv[0]);
    (void)context; (void)argc;
    SetValueByRealNo(point, as_double(argv[1]));
    return result_val(true, 0, "ok");
}
static Value native_identity(void* context, int argc, const Value* argv)
{
    (void)context; (void)argc;
    return argv[0];
}
static Value native_build_register(void* context, int argc, const Value* argv)
{
    uint8_t frame[8];
    uint16_t crc = 0xffff;
    int slave = (int)as_int(argv[0]);
    int address = (int)as_int(argv[1]);
    int value = (int)as_int(argv[2]);
    int i, bit;
    (void)context; (void)argc;
    if (slave < 0 || slave > 247 || address < 0 || address > 65535 || value < 0 || value > 65535)
        return result_val(false, 1, "invalid Modbus arguments");
    frame[0] = (uint8_t)slave; frame[1] = 6;
    frame[2] = (uint8_t)(address >> 8); frame[3] = (uint8_t)address;
    frame[4] = (uint8_t)(value >> 8); frame[5] = (uint8_t)value;
    for (i = 0; i < 6; ++i) { crc ^= frame[i]; for (bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1; }
    frame[6] = (uint8_t)crc; frame[7] = (uint8_t)(crc >> 8);
    return bytes_val(frame, sizeof(frame));
}

void register_default_ttu_natives(void)
{
    TTUNative item;
    reset_native_functions();
    memset(&item, 0, sizeof(item));
    item.min_arity = 0; item.max_arity = 0; item.function = native_now_ms;
    snprintf(item.name, sizeof(item.name), "%s", "now_ms"); register_native_function(&item);
    item.min_arity = 1; item.max_arity = 1; item.capability = CAP_RTD_READ; item.function = native_rtdb_read;
    snprintf(item.name, sizeof(item.name), "%s", "rtdb.read"); register_native_function(&item);
    item.min_arity = 2; item.max_arity = 2; item.capability = CAP_RTD_WRITE; item.function = native_rtdb_write;
    snprintf(item.name, sizeof(item.name), "%s", "rtdb.write"); register_native_function(&item);
    item.min_arity = 1; item.max_arity = 1; item.capability = 0; item.function = native_identity;
    snprintf(item.name, sizeof(item.name), "%s", "log.info"); register_native_function(&item);
    item.min_arity = 3; item.max_arity = 3; item.capability = 0; item.function = native_build_register;
    snprintf(item.name, sizeof(item.name), "%s", "modbus.build_write_register"); register_native_function(&item);
    (void)native_resolver;
}

int resolve_native(const char* name)
{
    int id = find_native_function(name);
    if (id < 0 && native_resolver != NULL) return native_resolver(name);
    return id;
}
