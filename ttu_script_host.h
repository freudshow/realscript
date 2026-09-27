#ifndef TTU_SCRIPT_HOST_H
#define TTU_SCRIPT_HOST_H
#include <stdint.h>
typedef struct TTUScriptHostAPI {
    int (*rtdb_read)(void*, int, int, double*);
    int (*rtdb_write)(void*, int, int, double);
    int (*rtdb_resolve)(void*, int, int, int, int*);
    int (*yk_prepare)(void*, int, int*);
    int (*yk_execute)(void*, int, int, int);
    int (*yk_cancel)(void*, int, int);
    void (*log)(void*, int, const char*);
} TTUScriptHostAPI;
#endif
