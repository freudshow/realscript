#ifndef DB_H
#define DB_H

// RealDataBase API signatures as requested by the user.
// These mock the user's actual database implementation.

double GetValueByRealNo(int realNo);
void SetValueByRealNo(int realNo, double val);

double GetValueByLinkDevReg(int linkNo, int devNo, int regNo);
void SetValueByLinkDevReg(int linkNo, int devNo, int regNo, double val);

#endif // DB_H
