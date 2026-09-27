/***************************************************************************
* 文件名: ttu_script_runtime.c
* 作者: 未知
* 版本号: 0.1
* 生成日期: 2026.09.27
* 概述: TTUScript 原生函数注册、默认模拟宿主与 VM 能力初始化。
* 修改日志: 新建脚本运行时原生函数注册模块。
**************************************************************************/
#include "ttu_script_runtime.h"
#include "db.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static TTUNativeFunction g_native_functions[TTU_NATIVE_MAX];
static int g_native_count;

int ttu_script_register_native(const TTUNativeFunction *function)
{
    int i;
    if (function == NULL || function->function == NULL || function->name[0] == '\0' ||
        function->min_arity < 0 || function->max_arity < function->min_arity ||
        g_native_count >= TTU_NATIVE_MAX) return -1;
    for (i = 0; i < g_native_count; ++i)
        if (strcmp(g_native_functions[i].name, function->name) == 0) return -1;
    g_native_functions[g_native_count] = *function;
    return g_native_count++;
}

const TTUNativeFunction *ttu_script_get_native(int id)
{
    return id >= 0 && id < g_native_count ? &g_native_functions[id] : NULL;
}

int resolve_native(const char *name)
{
    int i;
    if (name == NULL) return -1;
    for (i = 0; i < g_native_count; ++i)
        if (strcmp(g_native_functions[i].name, name) == 0) return i;
    return -1;
}

static Value native_now_ms(VM *vm, int argc, const Value *argv)
{
    (void)vm; (void)argc; (void)argv;
    return int_val((int64_t)time(NULL) * 1000);
}

static Value native_log(VM *vm, int argc, const Value *argv)
{
    char message[512];
    (void)argc;
    if (vm->host != NULL && vm->host->log != NULL && (vm->capabilities & TTU_CAP_LOG) != 0) {
        if (argv[0].type == VAL_STRING) vm->host->log(vm->host_context, 1, (const char*)argv[0].as.obj);
        else vm->host->log(vm->host_context, 1, "script value");
        return nil_val();
    }
    if (argv[0].type == VAL_STRING) fprintf(stderr, "[script] %s\n", (char*)argv[0].as.obj);
    else { (void)snprintf(message, sizeof(message), "script value type=%d", argv[0].type); fprintf(stderr, "%s\n", message); }
    return nil_val();
}

static Value native_rtdb_read(VM *vm, int argc, const Value *argv)
{
    int point = (int)as_int(argv[0]);
    double value = 0.0;
    (void)argc;
    if (point < 0 || point > 65535) return result_val(false, 1, "invalid point number");
    if ((vm->capabilities & TTU_CAP_RTD_READ) == 0) return result_val(false, 2, "permission denied");
    if (vm->host != NULL && vm->host->rtdb_read != NULL) {
        if (vm->host->rtdb_read(vm->host_context, point, -1, &value) != 0) return result_val(false, 3, "RTDB read failed");
    } else value = GetValueByRealNo(point);
    return double_val(value);
}

static Value native_rtdb_write(VM *vm, int argc, const Value *argv)
{
    int point = (int)as_int(argv[0]);
    (void)argc;
    if (point < 0 || point > 65535) return result_val(false, 1, "invalid point number");
    if ((vm->capabilities & TTU_CAP_RTD_WRITE) == 0) return result_val(false, 2, "permission denied");
    if (vm->host != NULL && vm->host->rtdb_write != NULL) {
        if (vm->host->rtdb_write(vm->host_context, point, -1, as_double(argv[1])) != 0) return result_val(false, 3, "RTDB write failed");
    } else SetValueByRealNo(point, as_double(argv[1]));
    return result_val(true, 0, "ok");
}

static Value native_yk(VM *vm, int argc, const Value *argv)
{
    (void)argc;
    if (vm->host == NULL || vm->host->yk_execute == NULL ||
        (vm->capabilities & TTU_CAP_YK_EXECUTE) == 0) return result_val(false, 2, "permission denied");
    if (vm->host->yk_execute(vm->host_context, 0, (int)as_int(argv[0]), (int)as_int(argv[1])) != 0)
        return result_val(false, 3, "YK execute failed");
    return result_val(true, 0, "ok");
}

static Value native_yk_prepare(VM *vm, int argc, const Value *argv)
{
    int request_id = -1;
    (void)argc;
    if (vm->host == NULL || vm->host->yk_prepare == NULL ||
        (vm->capabilities & TTU_CAP_YK_PREPARE) == 0) return result_val(false, 2, "permission denied");
    if (vm->host->yk_prepare(vm->host_context, (int)as_int(argv[0]), &request_id) != 0)
        return result_val(false, 3, "YK prepare failed");
    return int_val(request_id);
}

static Value native_modbus_crc16(VM *vm, int argc, const Value *argv)
{
    ObjBytes *bytes;
    uint16_t crc = 0xffff;
    size_t i;
    int bit;
    (void)vm; (void)argc;
    if (argv[0].type != VAL_BYTES) return result_val(false, 1, "expected bytes");
    bytes = argv[0].as.obj;
    for (i = 0; i < bytes->length; ++i) {
        crc ^= bytes->data[i];
        for (bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1;
    }
    return int_val(crc);
}

static Value native_modbus_build_write_register(VM *vm, int argc, const Value *argv)
{
    uint8_t frame[8];
    uint16_t crc = 0xffff;
    int slave = (int)as_int(argv[0]);
    int address = (int)as_int(argv[1]);
    int value = (int)as_int(argv[2]);
    int i, bit;
    (void)vm; (void)argc;
    if (slave < 0 || slave > 247 || address < 0 || address > 65535 || value < 0 || value > 65535)
        return result_val(false, 1, "invalid register arguments");
    frame[0] = (uint8_t)slave; frame[1] = 6;
    frame[2] = (uint8_t)(address >> 8); frame[3] = (uint8_t)address;
    frame[4] = (uint8_t)(value >> 8); frame[5] = (uint8_t)value;
    for (i = 0; i < 6; ++i) { crc ^= frame[i]; for (bit = 0; bit < 8; ++bit) crc = (crc & 1) ? (crc >> 1) ^ 0xa001 : crc >> 1; }
    frame[6] = (uint8_t)crc; frame[7] = (uint8_t)(crc >> 8);
    return bytes_val(frame, sizeof(frame));
}

void ttu_script_reset_native_functions(void)
{
    memset(g_native_functions, 0, sizeof(g_native_functions));
    g_native_count = 0;
}

int ttu_script_register_core_natives(void)
{
    TTUNativeFunction fn;
    g_native_count = 0;
    memset(&fn, 0, sizeof(fn));
    (void)snprintf(fn.name, sizeof(fn.name), "%s", "now_ms"); fn.function = native_now_ms;
    if (ttu_script_register_native(&fn) < 0) return -1;
    (void)snprintf(fn.name, sizeof(fn.name), "%s", "log.info"); fn.min_arity = 1; fn.max_arity = 1; fn.capability = TTU_CAP_LOG; fn.function = native_log;
    if (ttu_script_register_native(&fn) < 0) return -1;
    (void)snprintf(fn.name, sizeof(fn.name), "%s", "rtdb.read"); fn.capability = TTU_CAP_RTD_READ; fn.function = native_rtdb_read;
    if (ttu_script_register_native(&fn) < 0) return -1;
    (void)snprintf(fn.name, sizeof(fn.name), "%s", "rtdb.write"); fn.min_arity = 2; fn.max_arity = 2; fn.capability = TTU_CAP_RTD_WRITE; fn.function = native_rtdb_write;
    if (ttu_script_register_native(&fn) < 0) return -1;
    (void)snprintf(fn.name, sizeof(fn.name), "%s", "yk.prepare"); fn.min_arity = 1; fn.max_arity = 1; fn.capability = TTU_CAP_YK_PREPARE; fn.function = native_yk_prepare;
    if (ttu_script_register_native(&fn) < 0) return -1;
    (void)snprintf(fn.name, sizeof(fn.name), "%s", "yk.execute"); fn.min_arity = 2; fn.max_arity = 2; fn.capability = TTU_CAP_YK_EXECUTE; fn.function = native_yk;
    if (ttu_script_register_native(&fn) < 0) return -1;
    (void)snprintf(fn.name, sizeof(fn.name), "%s", "modbus.crc16"); fn.min_arity = 1; fn.max_arity = 1; fn.capability = 0; fn.function = native_modbus_crc16;
    if (ttu_script_register_native(&fn) < 0) return -1;
    (void)snprintf(fn.name, sizeof(fn.name), "%s", "modbus.build_write_register"); fn.min_arity = 3; fn.max_arity = 3; fn.function = native_modbus_build_write_register;
    return ttu_script_register_native(&fn) < 0 ? -1 : 0;
}

int ttu_script_register_host_natives(void)
{
    return ttu_script_register_core_natives();
}
