#ifndef TTU_SCRIPT_ADAPTER_H
#define TTU_SCRIPT_ADAPTER_H

#include <stdint.h>
#include "ramdatabase.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TTU_SCRIPT_CAP_RTD_READ  (1u << 0)
#define TTU_SCRIPT_CAP_RTD_WRITE (1u << 1)
#define TTU_SCRIPT_CAP_YK        (1u << 5)
#define TTU_SCRIPT_CAP_LOG       (1u << 16)

#ifndef TTU_SCRIPT_CAP_YK_EXECUTE
#define TTU_SCRIPT_CAP_YK_EXECUTE TTU_SCRIPT_CAP_YK
#endif

typedef struct
{
    int (*rtdb_read)(void *context, int real_no, int type, double *value);
    int (*rtdb_write)(void *context, int real_no, int type, double value);
    int (*rtdb_resolve)(void *context, int link, int dev, int reg, int *real_no);
    int (*yk_execute)(void *context, int yk_no, int value);
    void (*log)(void *context, int level, const char *message);
} t_TtuScriptHostApi;

int TtuScriptHost_Init(t_TtuScriptHostApi *api);
int TtuScriptHost_Read(void *context, int real_no, int type, double *value);
int TtuScriptHost_Write(void *context, int real_no, int type, double value);
int TtuScriptHost_Resolve(void *context, int link, int dev, int reg, int *real_no);
int TtuScriptHost_ExecuteYk(void *context, int yk_no, int value);

#ifdef __cplusplus
}
#endif

#endif
