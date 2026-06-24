#include "db.h"
#include <stdio.h>
#include <stdlib.h>

#define MAX_REAL_ITEMS 1000
#define MAX_LINK_ITEMS 500

static double realDatabase[MAX_REAL_ITEMS] = {0};

typedef struct {
    int linkNo;
    int devNo;
    int regNo;
    double value;
} LinkDevRegItem;

static LinkDevRegItem linkDevRegDatabase[MAX_LINK_ITEMS];
static int linkDevRegCount = 0;

double GetValueByRealNo(int realNo) {
    if (realNo < 0 || realNo >= MAX_REAL_ITEMS) {
        printf("[DB Error] GetValueByRealNo: Index %d out of bounds (0-%d).\n", realNo, MAX_REAL_ITEMS - 1);
        return 0.0;
    }
    printf("[DB Access] GetValueByRealNo(#%d) -> %g\n", realNo, realDatabase[realNo]);
    return realDatabase[realNo];
}

void SetValueByRealNo(int realNo, double val) {
    if (realNo < 0 || realNo >= MAX_REAL_ITEMS) {
        printf("[DB Error] SetValueByRealNo: Index %d out of bounds (0-%d).\n", realNo, MAX_REAL_ITEMS - 1);
        return;
    }
    printf("[DB Access] SetValueByRealNo(#%d, %g)\n", realNo, val);
    realDatabase[realNo] = val;
}

double GetValueByLinkDevReg(int linkNo, int devNo, int regNo) {
    for (int i = 0; i < linkDevRegCount; i++) {
        if (linkDevRegDatabase[i].linkNo == linkNo &&
            linkDevRegDatabase[i].devNo == devNo &&
            linkDevRegDatabase[i].regNo == regNo) {
            printf("[DB Access] GetValueByLinkDevReg(#(%d, %d, %d)) -> %g\n", linkNo, devNo, regNo, linkDevRegDatabase[i].value);
            return linkDevRegDatabase[i].value;
        }
    }
    printf("[DB Access] GetValueByLinkDevReg(#(%d, %d, %d)) -> 0 (Not Found, Defaulting)\n", linkNo, devNo, regNo);
    return 0.0;
}

void SetValueByLinkDevReg(int linkNo, int devNo, int regNo, double val) {
    for (int i = 0; i < linkDevRegCount; i++) {
        if (linkDevRegDatabase[i].linkNo == linkNo &&
            linkDevRegDatabase[i].devNo == devNo &&
            linkDevRegDatabase[i].regNo == regNo) {
            printf("[DB Access] SetValueByLinkDevReg(#(%d, %d, %d), %g) (Update)\n", linkNo, devNo, regNo, val);
            linkDevRegDatabase[i].value = val;
            return;
        }
    }
    if (linkDevRegCount >= MAX_LINK_ITEMS) {
        printf("[DB Error] SetValueByLinkDevReg: Database full.\n");
        return;
    }
    printf("[DB Access] SetValueByLinkDevReg(#(%d, %d, %d), %g) (Insert)\n", linkNo, devNo, regNo, val);
    linkDevRegDatabase[linkDevRegCount++] = (LinkDevRegItem){linkNo, devNo, regNo, val};
}
