// ============================================================================
// db.c -- Mock RealDataBase Implementation
//
// Provides an in-memory simulation of an industrial database with two
// addressing schemes:
//   - Flat index (#N):  simple array of 1000 doubles
//   - Link/Dev/Reg (#(L,D,R)):  expandable array of up to 500 struct triples
//
// Every access produces a log line so the test suite can verify reads/writes.
// ============================================================================

#include "db.h"
#include <stdio.h>
#include <stdlib.h>

// ---------------------------------------------------------------------------
// Constants: size limits for the two storage arrays
// ---------------------------------------------------------------------------
#define MAX_REAL_ITEMS 1000   // Maximum entries in the flat-index database
#define MAX_LINK_ITEMS 500    // Maximum entries in the link-dev-reg database

// ---------------------------------------------------------------------------
// Flat-index storage:   realDatabase[realNo]
// Accessed by syntax like  #32  (reads)  or  #32 = 45.67  (writes)
// ---------------------------------------------------------------------------
static double realDatabase[MAX_REAL_ITEMS] = {0};

// ---------------------------------------------------------------------------
// Link-Dev-Reg storage:  a dynamic list of (linkNo, devNo, regNo, value) tuples
// Accessed by syntax like  #(3, 45, 12)  for reads or writes
// ---------------------------------------------------------------------------
typedef struct {
    int linkNo;     // Link number
    int devNo;      // Device number
    int regNo;      // Register number
    double value;   // Stored value
} LinkDevRegItem;

// Array of link-dev-reg entries; we search linearly on every access
static LinkDevRegItem linkDevRegDatabase[MAX_LINK_ITEMS];
static int linkDevRegCount = 0;   // Number of entries currently stored

// ---------------------------------------------------------------------------
// GetValueByRealNo: read value at flat index #realNo
// ---------------------------------------------------------------------------
double GetValueByRealNo(int realNo) {
    // Bounds check: reject indices outside [0, MAX_REAL_ITEMS)
    if (realNo < 0 || realNo >= MAX_REAL_ITEMS) {
        printf("[DB Error] GetValueByRealNo: Index %d out of bounds (0-%d).\n", realNo, MAX_REAL_ITEMS - 1);
        return 0.0;
    }
    printf("[DB Access] GetValueByRealNo(#%d) -> %g\n", realNo, realDatabase[realNo]);
    return realDatabase[realNo];
}

// ---------------------------------------------------------------------------
// SetValueByRealNo: write value at flat index #realNo
// ---------------------------------------------------------------------------
void SetValueByRealNo(int realNo, double val) {
    // Bounds check
    if (realNo < 0 || realNo >= MAX_REAL_ITEMS) {
        printf("[DB Error] SetValueByRealNo: Index %d out of bounds (0-%d).\n", realNo, MAX_REAL_ITEMS - 1);
        return;
    }
    printf("[DB Access] SetValueByRealNo(#%d, %g)\n", realNo, val);
    realDatabase[realNo] = val;
}

// ---------------------------------------------------------------------------
// GetValueByLinkDevReg: look up a (link, dev, reg) triple by linear search
// Returns 0.0 (and logs a warning) when the triple is not found
// ---------------------------------------------------------------------------
double GetValueByLinkDevReg(int linkNo, int devNo, int regNo) {
    for (int i = 0; i < linkDevRegCount; i++) {
        if (linkDevRegDatabase[i].linkNo == linkNo &&
            linkDevRegDatabase[i].devNo == devNo &&
            linkDevRegDatabase[i].regNo == regNo) {
            printf("[DB Access] GetValueByLinkDevReg(#(%d, %d, %d)) -> %g\n", linkNo, devNo, regNo, linkDevRegDatabase[i].value);
            return linkDevRegDatabase[i].value;
        }
    }
    // Not found: return default 0.0
    printf("[DB Access] GetValueByLinkDevReg(#(%d, %d, %d)) -> 0 (Not Found, Defaulting)\n", linkNo, devNo, regNo);
    return 0.0;
}

// ---------------------------------------------------------------------------
// SetValueByLinkDevReg: update an existing triple or insert a new one
// ---------------------------------------------------------------------------
void SetValueByLinkDevReg(int linkNo, int devNo, int regNo, double val) {
    // Search for existing entry; update it if found
    for (int i = 0; i < linkDevRegCount; i++) {
        if (linkDevRegDatabase[i].linkNo == linkNo &&
            linkDevRegDatabase[i].devNo == devNo &&
            linkDevRegDatabase[i].regNo == regNo) {
            printf("[DB Access] SetValueByLinkDevReg(#(%d, %d, %d), %g) (Update)\n", linkNo, devNo, regNo, val);
            linkDevRegDatabase[i].value = val;
            return;
        }
    }
    // Not found: insert new entry (if space permits)
    if (linkDevRegCount >= MAX_LINK_ITEMS) {
        printf("[DB Error] SetValueByLinkDevReg: Database full.\n");
        return;
    }
    printf("[DB Access] SetValueByLinkDevReg(#(%d, %d, %d), %g) (Insert)\n", linkNo, devNo, regNo, val);
    linkDevRegDatabase[linkDevRegCount++] = (LinkDevRegItem){linkNo, devNo, regNo, val};
}
