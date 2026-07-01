#ifndef DB_H
#define DB_H

// ============================================================================
// db.h -- Mock RealDataBase API Header
//
// This module simulates an industrial database (e.g. a SCADA or PLC system)
// where values can be read/written by either:
//   1. A single "real number" index (#N)
//   2. A (link, device, register) tuple (#(L, D, R))
//
// These functions are called at runtime by the VM when it encounters
// OP_GET_DB_ID, OP_SET_DB_ID, OP_GET_DB_LINK, or OP_SET_DB_LINK opcodes.
// ============================================================================

// Read the value stored at a single-integer database index.
double GetValueByRealNo(int realNo);

// Write a value to a single-integer database index.
void SetValueByRealNo(int realNo, double val);

// Read the value stored at a (link, device, register) coordinate.
double GetValueByLinkDevReg(int linkNo, int devNo, int regNo);

// Write a value to a (link, device, register) coordinate.
void SetValueByLinkDevReg(int linkNo, int devNo, int regNo, double val);

#endif // DB_H
