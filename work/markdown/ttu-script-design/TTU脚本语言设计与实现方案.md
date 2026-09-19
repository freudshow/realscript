# linux_TTU 脚本语言设计与实现方案

## 文档状态

| 项目 | 内容 |
|---|---|
| 文档名称 | linux_TTU 脚本语言设计与实现方案 |
| 目标语言 | RealScript-TTU，暂名 TTUScript |
| 关联项目 | 当前 RealScript、`~/repo/linux_TTU/` |
| 文档用途 | 后续架构设计、接口实现、代码评审和测试对照基线 |
| 当前阶段 | 设计基线，尚未开始实现 TTU 适配代码 |
| 文档原则 | 先定义稳定的脚本层接口，再通过 C 适配层连接现有协议和实时库 |

> 本文档是后续开发的单一设计参考文件。实现过程中如发现现有代码行为与本文档不一致，应在本文档中记录差异、原因和兼容策略；不应只在代码中隐式改变设计。

---

## 1. 目标与非目标

### 1.1 总体目标

在当前 RealScript 的基础上，为 `linux_TTU` 提供一种面向电力终端、通信终端和数据采集终端的嵌入式脚本语言，使脚本能够组合完成：

- 实时库数据读取和写入；
- 遥信、遥测、遥控、遥调和电度操作；
- 遥控预置、执行、撤销、反馈闭环和重试；
- Modbus 读写、数据解析、CRC 校验和组帧；
- Modbus 块采集和点表映射；
- IEC101/IEC104、DLT645 等协议的业务级操作；
- 转发表查询、主站数据映射和控制点解析；
- SOE、告警、实时数据上报、历史数据查询；
- 透传、原始帧处理和受控的串口/网络访问；
- 逻辑引擎任务、定时器、比较器、PID、AGC、AVC 等控制逻辑；
- 备自投、同期合闸、有压合闸、双电源切换等专用业务；
- 终端配置和协议诊断。

### 1.2 设计目标

1. **业务优先**：脚本首先面向点、设备、遥控和协议业务，不直接面向 C 结构体。
2. **安全可控**：高风险操作必须有权限、校验、超时、撤销和结果状态。
3. **兼容现有代码**：优先复用 `linux_TTU` 已有的 RTDB、遥控、数据中心和协议接口。
4. **逐步实施**：先实现 RTDB 和遥控，再扩展 Modbus、遥调、事件、专用业务。
5. **可测试**：脚本 VM 可以在 PC 上使用模拟宿主接口运行，不依赖真实设备。
6. **协议解耦**：协议线程、环形缓冲区和状态机留在 C/C++ 模块中，由适配器提供稳定 ABI。
7. **可审计**：脚本执行的遥控、遥调和配置修改可记录脚本名、行号、点号、结果和操作者上下文。

### 1.3 非目标

以下内容不作为第一阶段目标：

- 让脚本直接创建和管理所有协议线程；
- 让脚本直接修改 `linux_TTU` 的全局结构体；
- 让脚本替代所有现有协议状态机；
- 让脚本执行任意 Linux Shell 命令；
- 让脚本绕过远方/本地、软压板、硬压板和闭锁逻辑；
- 在第一阶段实现完整的通用 JSON 语言或动态对象系统。

---

## 2. 现有代码基线

### 2.1 当前 RealScript

当前 RealScript 的处理链路为：

```text
source
  → lexer
  → parser
  → AST
  → compiler
  → bytecode
  → stack VM
```

主要文件：

| 文件 | 现有职责 |
|---|---|
| `lexer.c/h` | 词法分析和 Token 定义 |
| `parser.c/h` | 递归下降语法分析 |
| `ast.c/h` | AST 节点和释放 |
| `compiler.c/h` | AST 到字节码的编译 |
| `vm.c/h` | 栈式虚拟机 |
| `value.c/h` | Tagged Union 值系统和运算 |
| `db.c/h` | 当前模拟数据库接口 |
| `main.c` | 文件执行和内置验证测试 |

现有语言支持：

- 整数、双精度浮点、布尔值和 nil；
- 全局变量和局部变量；
- `fn` 函数声明和递归调用；
- `if / else`、`while`、`for`；
- 算术、比较、逻辑短路和位运算；
- `#32` 形式的按实时库号访问；
- `#(link, dev, reg)` 形式的按链路、设备、寄存器访问。

现有数据库接口：

```c
double GetValueByRealNo(int realNo);
void SetValueByRealNo(int realNo, double val);

double GetValueByLinkDevReg(int linkNo, int devNo, int regNo);
void SetValueByLinkDevReg(int linkNo, int devNo, int regNo, double val);
```

依据：`db.h:17-24`。

现有数据库字节码：

```text
OP_GET_DB_ID
OP_SET_DB_ID
OP_GET_DB_LINK
OP_SET_DB_LINK
```

依据：`compiler.h:43-47`、`vm.c:306-337`。

当前函数调用流程：

```text
函数名
  → 编译为全局变量访问
  → OP_GET_GLOBAL
  → OP_CALL
```

依据：`compiler.c:448-469`、`vm.c:402-432`。

当前调用只支持 `VAL_FUNC`，还没有稳定的 C 原生函数注册表。

### 2.2 linux_TTU 的实时库和点模型

`linux_TTU` 的实时库数据类型包括：

```c
Type_YX   = 0;  // 遥信
Type_YC   = 1;  // 遥测
Type_YK   = 2;  // 遥控
Type_DD   = 3;  // 电度
Type_PARA = 4;  // 参数
```

依据：`~/repo/linux_TTU/src/realdatabase/ramdatabase.h:101-109`。

核心实时库函数表：

```c
int (*WriteDevSelfData)(short linkNo, short devNo, short RegNo,
                        unsigned char type, char *value);
int (*RtdbWriteVal)(short linkNo, short devNo, short RegNo,
                    char *val, char type);
int (*GetRtdbVal)(short linkNo, short devNo, short RegNo,
                  unsigned char type, char *value);
int (*RtdbWriteValByRtdbNo)(short realDevNo, char *val,
                            unsigned char type);
int (*GetRtdbValByRtdbNo)(short realDevNo, char *val,
                          unsigned char type);
int32_t (*FastGetRtdbNo)(short linkNo, short devNo, short regNo);
int (*GetRegNoFromRtdbNo)(int no);
rt_value_type_e (*GetTypeByRtdbNo)(u32 rtdbNo);
```

依据：`~/repo/linux_TTU/src/realdatabase/ramdatabase.h:494-507`。

兼容性结论：

当前 RealScript 的 `double` 数据模型可以暂时承载实时库数值，但不能完整表达：

- 遥控操作的预置、执行和撤销状态；
- SOE 事件及其时间戳；
- Modbus 原始帧；
- 参数名、模型名、设备名和 AppName；
- 查询结果的错误码、状态和响应正文。

因此必须扩展字符串、字节数组、数组、映射、句柄和结果对象。

### 2.3 linux_TTU 的遥控能力

公共遥控接口：

```c
int ExeYkOff(unsigned short int ykNo);
int ExeYzOn(unsigned short int ykNo);
int ExeYzOff(unsigned short int ykNo);
int ExeYkOn(unsigned short int ykNo);

int CommonYkOperation(unsigned char ykAttribute,
                      unsigned char cmd,
                      unsigned short int ykNo,
                      unsigned char yzCmd);
```

依据：`~/repo/linux_TTU/src/common/common_yk.h:335-344`。

遥控属性：

```c
YkAttribute_CO = 0;  // 常合
YkAttribute_DO = 1;  // 脉冲
```

遥控命令：

```c
CommonYkCmd_Init   = 0;
CommonYkCmd_ScoOn  = 1;
CommonYkCmd_ScoOff = 2;
CommonYkCmd_DcoOn  = 3;
CommonYkCmd_DcoOff = 4;
CommonYkCmd_YzOn   = 5;
CommonYkCmd_YzOff  = 6;
```

依据：`~/repo/linux_TTU/src/common/common_yk.h:32-38`、`:293-297`。

遥控号已经按协议和业务分段，包括 Modbus 下挂设备、DLT645、组合遥控、软压板、IEC103、IEC104、IEC101、板件和其他链路等。

关键范围依据：`~/repo/linux_TTU/src/common/common_yk.h:59-103`。

### 2.4 IEC104 遥控和遥调

IEC104 遥控队列接口：

```c
int Iec104YKYZ(unsigned short YKNo);                 // 预置
int Iec104YKZX(unsigned short YKNo, unsigned char cmd); // 执行
int Iec104YKCX(unsigned short YKNo);                 // 撤销
int Iec104YKClear(void);                             // 清理
```

依据：`~/repo/linux_TTU/src/protocol/Iec101_104/iecMaster/iec104master.cpp:63-149`。

IEC104 遥调队列接口：

```c
int iec104ParaSet(int RTDBNo,
                  int SlaveAddr,
                  int ParaAddr,
                  float WriteCoreVal,
                  char *Ip,
                  int IpLen);
```

依据：`~/repo/linux_TTU/src/protocol/Iec101_104/iecMaster/iec104master.cpp:41-59`。

### 2.5 linux_TTU 的参数和数据中心接口

参数设置和读取：

```c
int AppGetParameter(const char *AppName,
                    const char *Dev,
                    const char TotalCall,
                    const int CheckParaDataArrayCount,
                    char (*CheckParaDataArray)[512],
                    t_SetParameterBody *ParaData,
                    void *ptCfg);

int AppSetParameter(const char *AppName,
                    const char *dev,
                    t_SetParameterBody *paradata,
                    int num,
                    int Count);

int AppDelParameter(const char *AppName,
                    const char *dev,
                    t_SetParameterBody *paradata,
                    int num,
                    int Count);
```

依据：`~/repo/linux_TTU/src/database/databaseinterface.h:184-185`、`:235`、`:284`。

实时数据：

```c
int CheckRealtimeData(...);
int CheckRealtimeDataMany(...);
int AppActiveReportRealData(...);
```

依据：`~/repo/linux_TTU/src/database/databaseinterface.h:431`、`:500-513`。

历史数据：

```c
int CheckHistoryDataAccordingDate(...);
int FrozenHistory_Init(...);
void FrozenHistory_Free(...);
```

依据：`~/repo/linux_TTU/src/database/databaseinterface.h:563-582`。

SOE：

```c
int Report104SoeData(...);
int Request104SoeDataupperN(...);
int Request104SoeData(...);
```

依据：`~/repo/linux_TTU/src/database/databaseinterface.h:770-812`。

MQTT 自定义遥控：

```c
int MQTTYKOperation(char *AppName,
                    char *DevType,
                    char *DevGuid,
                    char *prop,
                    char *name,
                    char *val);
```

依据：`~/repo/linux_TTU/src/database/databaseinterface.c:6567-6646`。

### 2.6 Modbus 和协议组帧

当前已有 Modbus 接口：

```c
int ReadMeasurementSamplingRegisters(unsigned char pcAddr,
                                     unsigned char readNum,
                                     unsigned char unitNum,
                                     char *regBuff);

int ReadRegistersOriginalVal(unsigned char pcAddr,
                             unsigned char readNum,
                             char *regBuff);

int GetSetModbusData(unsigned char cmd,
                     unsigned char regCount,
                     unsigned short int regAddr,
                     unsigned char *value,
                     unsigned char *readWriteCount);
```

依据：`~/repo/linux_TTU/src/system/database.h:72-81`。

参数设置类 Modbus 接口：

```c
void ModbusCommonSet_0x05_0x06(int MAddr, int MCmd, int MReg,
                               int MRegCount, int DevCom,
                               int WriteRegval);

void ModbusCommonSet_0x10(int MAddr, int MCmd, int MReg,
                          int MRegCount, int DevCom,
                          int *WriteRegval);

void ModbusCommonSet(int MAddr, int MCmd, int MReg,
                     int MRegCount, int DevCom,
                     int WriteRegval);
```

依据：`~/repo/linux_TTU/src/realdatabase/CallBackParaFuncRealize/CallBackFuncArray.h:119-121`。

底层已经实现：

- 功能码 `0x05` 写单个线圈；
- 功能码 `0x06` 写单个保持寄存器；
- 功能码 `0x10` 写多个保持寄存器；
- 自动计算 Modbus CRC；
- `MRegCount == 1` 时按 16 位写入；
- `MRegCount == 2` 时按 32 位写入。

对应源码：`~/repo/linux_TTU/src/realdatabase/CallBackParaFuncRealize/callbackindex_8to20func.c:502-625`。

Modbus 数据类型：

```c
ModbusDataType_u8 = 0;
ModbusDataType_u16 = 1;
ModbusDataType_u32 = 2;
ModbusDataType_s8 = 3;
ModbusDataType_s16 = 4;
ModbusDataType_s32 = 5;
ModbusDataType_bitField = 6;
ModbusDataType_MultiType = 7;
ModbusDataType_Float = 8;
```

依据：`~/repo/linux_TTU/src/protocol/ModbusBlocks.h:17-28`。

### 2.7 透传和端口

透传配置：

```c
    int UpType;
    union {
        struct NET_CAN_Port upNetOrCanInfo;
        struct FORE_COMM_PARAM_SerialPort upSerialPortInfo;
    };

    int downType;
    union {
        struct NET_CAN_Port downNetOrCanInfo;
        struct FORE_COMM_PARAM_SerialPort downSerialPortInfo;
    };

    int dataflow;
} t_ProtoPad;
```

端口类型：

```text
PORT_NET_CAN = 0
PORT_SERIAL  = 1
```

数据流向：

```text
dataflow_Both     = 0
dataflow_UpToDown = 1
dataflow_DownToUp = 2
```

依据：`~/repo/linux_TTU/src/protocol/PassThrough/passthrough.h:11-46`。

透传实现使用 `CommRead()`、`CommWrite()`、`tmEvEvery()` 和 `evReceive()`，见 `PassThrough.cpp:63-177`。

### 2.8 逻辑引擎

逻辑引擎 V2 已有任务接口：

```c
LogicEngineV2_Init();
LogicEngineV2_DeInit();
LogicEngineV2_LoadConfig();
LogicEngineV2_Start();
LogicEngineV2_Stop();
LogicEngineV2_StartTask();
LogicEngineV2_StopTask();
LogicEngineV2_CreateTask();
LogicEngineV2_DeleteTask();
LogicEngineV2_AddElement();
LogicEngineV2_RemoveElement();
LogicEngineV2_ConnectElements();
LogicEngineV2_DisconnectElements();
LogicEngineV2_SetElementParam();
LogicEngineV2_GetElementParam();
```

依据：`~/repo/linux_TTU/src/logicengine/logic_engine_v2.h:109-227`。

已注册元件包括：

```text
AND OR NOT XOR
COMPARE_GT COMPARE_GE COMPARE_LT COMPARE_LE COMPARE_EQ COMPARE_RANGE
MAX MIN
TIMER_DELAY TIMER_INTERVAL TIMER_OVERTIME TIMER_PERIOD
YC_INPUT YX_INPUT YC_OUTPUT YX_OUTPUT YK_OUTPUT YT_OUTPUT
CONST_INPUT RTDB_CHANGE_DETECT
ARITHMETIC_ADD ARITHMETIC_SUB ARITHMETIC_MUL ARITHMETIC_DIV
ARITHMETIC_NEG ARITHMETIC_ABS ARITHMETIC_EXPRESSION
PI_CONTROLLER RATE_OF_CHANGE IF
```

依据：`~/repo/linux_TTU/src/logicengine/logic_engine_v2.c:70-119`。

数据接口边界：

```c
    float (*GetYcValue)(int rtdbNo);
    int   (*GetYxValue)(int rtdbNo);
    void  (*SetYcValue)(int rtdbNo, float value);
    void  (*SetYxValue)(int rtdbNo, int value);
    float (*GetParaValue)(int paraNo);
    int   (*SetParaValue)(int paraNo, float value);
    int   (*ExecuteYk)(int ykNo, int value);
} t_LogicEngineV2_DataInterface;
```

依据：`~/repo/linux_TTU/src/logicengine/logic_engine_v2.h:243-259`。

注意：当前 `VisualProg_ExecuteYk()` 只打印日志并返回成功，尚未接入真实遥控。依据：`~/repo/linux_TTU/src/logicengine/logic_visual_programming.c:614-627`。脚本适配层不能直接复用这个未完成的实现，必须接入 `common_yk` 或统一遥控服务。

---

## 3. 总体架构

推荐架构：

```text
                    TTUScript 脚本
                           │
        ┌──────────────────┴──────────────────┐
        │                                     │
   业务原语模块                            协议/诊断原语
        │                                     │
        └──────────────────┬──────────────────┘
                           │
                 TTU Host Adapter ABI
                           │
        ┌──────────────────┼──────────────────┐
        │                  │                  │
       RTDB             控制服务             协议服务
        │                  │                  │
 RealDev/ramdatabase   common_yk       Modbus/IEC/DLT645
```

核心原则：

1. RealScript 核心不直接包含大量 `linux_TTU` 私有头文件。
2. `linux_TTU` 通过 `TTUScriptHostAPI` 向 VM 注入能力。
3. 业务原语使用稳定的脚本类型和结果对象。
4. C/C++ 适配层负责结构体转换、锁、线程切换、内存管理和错误转换。
5. 协议状态机继续由现有协议线程管理。

推荐依赖方向：

```text
RealScript core
    ↓
TTU Script Host ABI
    ↓
linux_TTU scripting adapter
    ↓
RealDev / common_yk / databaseinterface / protocol modules
```

---

## 4. 语言扩展设计

### 4.1 值类型

当前 `Value` 类型：

```c
VAL_INT
VAL_DOUBLE
VAL_BOOL
VAL_NIL
VAL_FUNC
```

建议扩展为：

```c
typedef enum {
    VAL_INT,
    VAL_DOUBLE,
    VAL_BOOL,
    VAL_NIL,
    VAL_FUNC,
    VAL_NATIVE,
    VAL_STRING,
    VAL_BYTES,
    VAL_ARRAY,
    VAL_MAP,
    VAL_POINT,
    VAL_RESULT,
    VAL_HANDLE,
    VAL_TIME
} ValueType;
```

用途：

| 类型 | 用途 |
|---|---|
| `VAL_STRING` | AppName、设备名、模型名、IP、错误信息 |
| `VAL_BYTES` | Modbus 帧、透传数据、原始协议数据 |
| `VAL_ARRAY` | 批量点、寄存器列表、参数列表 |
| `VAL_MAP` | 结构化结果、JSON 风格对象 |
| `VAL_POINT` | RTDB 点、链路/设备/寄存器点 |
| `VAL_RESULT` | 统一操作结果 |
| `VAL_HANDLE` | 异步请求、端口、定时任务、逻辑任务 |
| `VAL_TIME` | SOE 和历史数据时间戳 |

第一阶段可以只实现：

```text
VAL_STRING
VAL_BYTES
VAL_ARRAY
VAL_RESULT
VAL_HANDLE
```

`VAL_POINT`、`VAL_MAP`、`VAL_TIME` 可以随后加入，但脚本层接口应从一开始按这些概念设计。

### 4.2 原生函数

建议定义原生函数类型：

```c
typedef struct VM VM;
typedef struct Value Value;

typedef Value (*NativeFn)(VM *vm, int argc, const Value *argv);

typedef struct {
    const char *name;
    int min_arity;
    int max_arity;
    NativeFn function;
    uint32_t capability;
} NativeFunction;
```

注册接口：

```c
int register_native_function(const NativeFunction *function);
int register_native_module(const char *module_name,
                           const NativeFunction *functions,
                           int count);
```

推荐原生函数名称使用点号分隔：

```text
rtdb.read
modbus.build_write_register
soe.report
alarm.raise
```

如果当前词法器不支持点号，应增加 `TOKEN_DOT`，并让调用节点保存完整限定名；短期也可以把点号转换为下划线，但长期不建议丢失模块语义。

### 4.3 原生调用字节码

建议新增：

```c
OP_CALL_NATIVE
```

字节码格式：

```text
OP_CALL_NATIVE
native_function_id
arg_count
```

建议 `native_function_id` 使用 16 位，以避免当前常量池和全局变量的 8 位限制。

短期兼容方案是使用 `VAL_NATIVE` 复用 `OP_CALL`，但正式方案应使用独立的 `OP_CALL_NATIVE`，原因是：

- 原生函数错误需要显示模块和参数位置；
- 原生函数需要 capability 检查；
- 原生函数可能访问 VM 宿主上下文；
- 原生函数可能返回句柄和异步结果；
- 原生函数调用不应占用普通脚本全局变量槽位。

### 4.4 结果对象

统一结果对象建议包含：

```text
{
    ok: bool,
    code: int,
    stage: string,
    message: string,
    request_id: int,
    accepted: bool,
    completed: bool,
    feedback: value,
    elapsed_ms: int
}
```

结果阶段：

```text
validate
permission
prepare
execute
feedback
retry
cancel
timeout
complete
```

脚本示例：

```real
var result = yk.execute_checked(12000, YK_ON, 101, true, 3000, 2);

if (!result.ok) {
    log.error(result.message);
    alarm.raise("YK_FAILED", ALARM_MAJOR, result.message);
}
```

### 4.5 错误处理

C 层错误码不能直接依赖脚本调用者记忆 `-1`、`-2`。建议保留原始错误码，同时提供统一类别：

```text
ERR_OK
ERR_INVALID_ARGUMENT
ERR_PERMISSION_DENIED
ERR_POINT_NOT_FOUND
ERR_TYPE_MISMATCH
ERR_BUSY
ERR_TIMEOUT
ERR_CRC
ERR_PROTOCOL
ERR_FEEDBACK_MISMATCH
ERR_NOT_SUPPORTED
ERR_INTERNAL
```

脚本可以采用两种模式：

```real
var result = modbus.send(port, frame, 1000, 3);
if (!result.ok) { ... }
```

或严格模式：

```real
must(modbus.send(port, frame, 1000, 3));
```

第一阶段只实现显式 `result.ok` 检查，避免增加异常机制。

---

## 5. 原语目录和函数参数契约

以下接口是脚本层的推荐稳定名称。C 层可以先以 `ttuscript_*` 前缀实现，再注册为脚本原语。

### 5.1 通用运行时

```text
sleep_ms(ms: int) -> nil
now_ms() -> int
now_time() -> time
log.info(message: string) -> nil
log.warn(message: string) -> nil
log.error(message: string) -> nil
last_error() -> string
assert(condition: bool, message: string) -> result
```

约束：

- `sleep_ms()` 不得阻塞协议线程；
- 在脚本任务线程中允许睡眠；
- 在同步回调上下文中应拒绝或转换为异步等待；
- 日志函数应自动附带脚本名、行号和任务名。

### 5.2 RTDB

```text
rtdb.resolve(link: int, dev: int, reg: int) -> int
rtdb.type(real_no: int) -> int
rtdb.reg(real_no: int) -> int
rtdb.valid(real_no: int) -> bool
rtdb.read(real_no: int) -> value
rtdb.write(real_no: int, value: value) -> result
rtdb.read_at(link: int, dev: int, reg: int) -> value
rtdb.write_at(link: int, dev: int, reg: int, value: value) -> result
rtdb.read_many(points: array) -> result
rtdb.write_many(items: array) -> result
```

强类型接口：

```text
yx.read(real_no: int) -> bool
yx.write(real_no: int, state: bool) -> result
yx.read_at(link: int, dev: int, reg: int) -> bool
yx.write_at(link: int, dev: int, reg: int, state: bool) -> result

yc.read(real_no: int) -> double
yc.write(real_no: int, value: double) -> result
yc.read_at(link: int, dev: int, reg: int) -> double
yc.write_at(link: int, dev: int, reg: int, value: double) -> result

dd.read(real_no: int) -> double
dd.write(real_no: int, value: double) -> result

para.read(real_no: int) -> double
para.write(real_no: int, value: double) -> result
```

映射：

```text
rtdb.resolve       → gt_RtdbFunc.FastGetRtdbNo()
rtdb.type          → gt_RtdbFunc.GetTypeByRtdbNo()
rtdb.read          → GetRtdbValByRtdbNo()/GetValueByRealNo()
rtdb.write         → RtdbWriteValByRtdbNo()/SetValueByRealNo()
```

注意：`SetValueByRealNo()` 当前实现中返回值初始化为 `-1`，但成功路径没有明确更新为 0，见 `RealDev.c:1597-1639`。适配层必须修正或包裹这个返回值语义，不能把它原样暴露为脚本结果。

### 5.3 遥信

```text
yx.changed(real_no: int) -> bool
yx.rising(real_no: int) -> bool
yx.falling(real_no: int) -> bool
yx.wait(real_no: int, expected: bool, timeout_ms: int) -> result
yx.set_debounce(real_no: int, milliseconds: int) -> result
yx.set_reverse(real_no: int, enabled: bool) -> result
yx.enable_soe(real_no: int, enabled: bool) -> result
yx.enable_cos(real_no: int, enabled: bool) -> result
```

这些设置接口不能直接修改 `gt_RealDevConfigure`，应通过专门配置服务完成，并在运行期限制可修改范围。

### 5.4 遥测

```text
yc.over_limit(real_no: int) -> bool
yc.upper_limit(real_no: int) -> double
yc.lower_limit(real_no: int) -> double
yc.deadzone(real_no: int) -> double
yc.scale(real_no: int) -> double
yc.average(real_no: int, window_ms: int) -> double
yc.rate(real_no: int, window_ms: int) -> double
```

### 5.5 遥控

简化接口：

```text
yk.on(yk_no: int) -> result
yk.off(yk_no: int) -> result
yk.prepare(yk_no: int) -> result
yk.execute(yk_no: int, value: int) -> result
yk.cancel(yk_no: int) -> result
yk.pulse(yk_no: int, value: int, keep_ms: int) -> result
```

查询接口：

```text
yk.kind(yk_no: int) -> int
yk.describe(yk_no: int) -> string
yk.is_allowed(yk_no: int) -> result
yk.feedback(yk_no: int) -> value
```

闭环接口：

```text
yk.execute_checked(
    yk_no: int,
    value: int,
    feedback_real_no: int,
    expected_state: bool,
    timeout_ms: int,
    retry_count: int
) -> result
```

映射建议：

```text
yk.on       → ExeYkOn()
yk.off      → ExeYkOff()
yk.prepare  → ExeYzOn() 或协议专用预置接口
yk.cancel   → ExeYzOff() 或协议专用撤销接口
yk.execute  → CommonYkOperation() 或协议专用执行接口
```

IEC104 专用接口：

```text
iec104.yk.prepare(yk_no: int) -> result
iec104.yk.execute(yk_no: int, command: int) -> result
iec104.yk.cancel(yk_no: int) -> result
```

对应：

```text
Iec104YKYZ()
Iec104YKZX()
Iec104YKCX()
```

### 5.6 遥调和参数

通用遥调：

```text
yt.get(real_no: int) -> value
yt.set(real_no: int, value: value) -> result
yt.set_at(link: int, dev: int, reg: int, value: value) -> result
```

数据中心参数：

```text
parameter.get(app: string, dev: string, name: string) -> result
parameter.get_many(app: string, dev: string, names: array) -> result
parameter.set(app: string, dev: string, name: string,
              value: value, datatype: string) -> result
parameter.set_many(app: string, dev: string, items: array) -> result
parameter.delete(app: string, dev: string, name: string) -> result
```

对应：

```text
AppGetParameter()
AppSetParameter()
AppDelParameter()
```

IEC104 遥调：

```text
iec104.parameter_set(
    rtdb_no: int,
    slave_addr: int,
    parameter_addr: int,
    value: double,
    ip: string
) -> result
```

对应：`iec104ParaSet()`。

PT/CT：

```text
pt.get(channel: int) -> result
pt.set(channel: int, primary: int, secondary: int) -> result
pt.validate(kind: int, value: int) -> result

ct.get(channel: int) -> result
ct.set(channel: int, primary: int, secondary: int) -> result
ct.validate(kind: int, value: int) -> result
```

对应：

```text
GetPtPrimarySecondaryVal()
SetPtPrimarySecondary()
CheckPtSetIsValiad()
GetCtPrimarySecondary()
SetCtPrimarySecondary()
CheckCtSetIsValiad()
```

### 5.7 Modbus 读写

```text
modbus.read_coils(port: int, slave: int, address: int, count: int) -> result
modbus.read_discrete_inputs(port: int, slave: int, address: int, count: int) -> result
modbus.read_holding(port: int, slave: int, address: int, count: int) -> result
modbus.read_input(port: int, slave: int, address: int, count: int) -> result

modbus.write_coil(port: int, slave: int, address: int, value: bool) -> result
modbus.write_register(port: int, slave: int, address: int, value: int) -> result
modbus.write_registers(port: int, slave: int, address: int, values: array) -> result
modbus.send(port: int, frame: bytes, timeout_ms: int, retry: int) -> result
```

第一阶段可以把 `port` 解释为已配置链路号，避免脚本自行打开串口。

### 5.8 Modbus 组帧、解析和 CRC

组帧：

```text
modbus.build_write_coil(slave: int, address: int, value: bool) -> bytes
modbus.build_write_register(slave: int, address: int, value: int) -> bytes
modbus.build_write_registers(slave: int, address: int, values: array) -> bytes
```

解析：

```text
modbus.decode_u8(frame: bytes, offset: int) -> int
modbus.decode_u16(frame: bytes, offset: int, endian: int) -> int
modbus.decode_u32(frame: bytes, offset: int, endian: int) -> int
modbus.decode_s8(frame: bytes, offset: int) -> int
modbus.decode_s16(frame: bytes, offset: int, endian: int) -> int
modbus.decode_s32(frame: bytes, offset: int, endian: int) -> int
modbus.decode_float(frame: bytes, offset: int, endian: int) -> double
modbus.decode_bit(frame: bytes, offset: int, bit: int) -> bool
modbus.decode_scaled(frame: bytes, offset: int, type: int,
                     format: int, scale: double) -> value
```

CRC：

```text
modbus.crc16(frame: bytes) -> int
modbus.crc16_append(frame: bytes) -> bytes
modbus.crc16_check(frame: bytes) -> bool
```

协议层必须明确 CRC 字节顺序，脚本层不能依赖底层注释中的高低字节约定。

### 5.9 Modbus 块采集

```text
modbus.block.create(
    link: int,
    device: int,
    slave: int,
    function: int,
    start: int,
    count: int
) -> handle

modbus.block.add_item(
    block: handle,
    name: string,
    offset: int,
    data_type: int,
    data_format: int,
    scale: double,
    property: int
) -> result

modbus.block.start(block: handle) -> result
modbus.block.stop(block: handle) -> result
modbus.block.read(block: handle, name: string) -> value
```

底层结构参考 `t_RegvalLtu`、`t_ModbusArea`，但不能直接把结构体指针传入 VM。

### 5.10 转发表

```text
forward.table_count() -> int
forward.table_info(index: int) -> result
forward.resolve_yx(link: int, address: int) -> result
forward.resolve_yc(link: int, address: int) -> result
forward.resolve_yk(link: int, address: int) -> result
forward.resolve_dd(link: int, address: int) -> result
forward.read(link: int, address: int) -> value
forward.can_write(link: int, address: int) -> bool
forward.write(link: int, address: int, value: value) -> result
```

转发表项包含地址、类型、映射实时库号和值等信息，参考 `ramdatabase.h` 中的 `ZfTableParaItem`。

不能直接暴露：

```text
p_YXDataItem
p_YCDataItem
p_YKDataItem
```

### 5.11 SOE 和事件

```text
soe.count() -> int
soe.get(index: int) -> result
soe.latest() -> result
soe.read_range(start: int, count: int) -> result

soe.report(
    app: string,
    model: string,
    device: string,
    event: string,
    ext: array
) -> result

soe.query_latest(
    app: string,
    model: string,
    device: string,
    yx_model: string,
    count: int
) -> result
```

对应：

```text
GetSoeInfo()
ReadSoeFromFile()
Report104SoeData()
Request104SoeDataupperN()
```

### 5.12 告警和日志

```text
alarm.raise(code: string, level: int, message: string) -> result
alarm.clear(code: string) -> result
alarm.active(code: string) -> bool
alarm.list() -> result
alarm.wait(code: string, timeout_ms: int) -> result
```

底层的 `myprintkWarnInf()`、`myprintkYKInf()` 等日志能力应由统一适配器接管，自动附带脚本上下文。

### 5.13 实时数据中心

```text
cloud.realtime.get(app: string, device: string, name: string) -> result
cloud.realtime.get_many(app: string, device: string, names: array) -> result
cloud.realtime.report(app: string, device_type: string, device: string,
                      quality: string, values: array) -> result
cloud.change.report(app: string, device: string, model: string,
                    values: array) -> result

cloud.history.query(app: string, device: string,
                    start_time: time, end_time: time,
                    span: int, frozen_type: string,
                    names: array) -> result
```

### 5.14 模型和设备管理

```text
model.create(app: string, name: string, items: array) -> result
model.delete(app: string, name: string) -> result
model.get(app: string, name: string) -> result

device.register(app: string, model: string, port: string,
                address: string, description: string) -> result
device.unregister(app: string, model: string, port: string,
                  address: string, description: string) -> result
device.guid(app: string, device_no: string) -> result
```

### 5.15 透传和端口

配置优先：

```text
port.bind_link(link: int) -> handle
port.read(port: handle, max_bytes: int, timeout_ms: int) -> result
port.write(port: handle, data: bytes) -> result

passthrough.create(up_link: int, down_link: int,
                   direction: int, idle_ms: int) -> handle
passthrough.start(tunnel: handle) -> result
passthrough.stop(tunnel: handle) -> result
passthrough.send(link: int, data: bytes) -> result
passthrough.receive(link: int, timeout_ms: int) -> result
```

第一阶段禁止任意路径打开串口和网络设备。

### 5.16 逻辑引擎

```text
logic.init() -> result
logic.start() -> result
logic.stop() -> result
logic.task.create(name: string, scan_interval_ms: int) -> handle
logic.task.delete(task: handle) -> result
logic.task.start(task: handle) -> result
logic.task.stop(task: handle) -> result
logic.element.add(task: handle, type_name: string, element_id: int) -> result
logic.element.remove(task: handle, element_id: int) -> result
logic.connect(task: handle, src_elem: int, src_port: int,
              dst_elem: int, dst_port: int) -> result
logic.disconnect(task: handle, src_elem: int, src_port: int) -> result
logic.param.set(task: handle, elem: int, param: int, value: double) -> result
logic.param.get(task: handle, elem: int, param: int) -> result
```

逻辑元素名称保持与现有注册表一致，避免产生第二套名称。

### 5.17 专用电力业务

备自投：

```text
bzt.enable(enabled: bool) -> result
bzt.quit_enable(enabled: bool) -> result
bzt.reset() -> result
bzt.set_delay(milliseconds: int) -> result
bzt.set_quit_delay(milliseconds: int) -> result
bzt.set_after_check_yx_time(seconds: int) -> result
bzt.execute_yk(qf_index: int, value: int) -> result
bzt.hard_plate_status() -> result
bzt.status() -> result
bzt.locked() -> bool
```

对应 `BZTQuitYkEnable()`、`BZTYkEnable()`、`BZTResetSet()`、`BZTDelayTimeSet()`、`BZTQuitDelayTimeSet()`、`BZTAfterCheckYxTimeSet()` 和 `BZTDoYKAndCheck()`。

同期合闸和有压合闸：

```text
sync.check(voltage_diff: double, phase_diff: double,
           frequency_diff: double) -> result
sync.close(yk_no: int, max_voltage_diff: double,
           max_phase_diff: double, max_frequency_diff: double,
           timeout_ms: int) -> result

voltage_close.configure(voltage_point: int, yk_no: int,
                        max_attempts: int, interval_ms: int) -> result
voltage_close.enable(enabled: bool) -> result
voltage_close.status() -> result
```

AGC/AVC：

```text
agc.start() -> result
agc.stop() -> result
agc.enable(enabled: bool) -> result
agc.remote_enable(enabled: bool) -> result
agc.set(value: double) -> result
agc.status() -> result

avc.enable(enabled: bool) -> result
avc.remote_enable(enabled: bool) -> result
avc.set_voltage(value: double) -> result
avc.status() -> result
```

---

## 6. TTU Host ABI 详细设计

### 6.1 宿主上下文

建议新增 `ttu_script_host.h`：

```c
#ifndef TTU_SCRIPT_HOST_H
#define TTU_SCRIPT_HOST_H

#include <stdint.h>

typedef struct {
    int  (*rtdb_read)(void *ctx, int real_no, int type, double *value);
    int  (*rtdb_write)(void *ctx, int real_no, int type, double value);
    int  (*rtdb_resolve)(void *ctx, int link, int dev, int reg,
                         int *real_no);
    int  (*rtdb_type)(void *ctx, int real_no, int *type);

    int  (*yk_prepare)(void *ctx, int yk_no, int *request_id);
    int  (*yk_execute)(void *ctx, int request_id, int yk_no, int value);
    int  (*yk_cancel)(void *ctx, int request_id, int yk_no);
    int  (*yk_feedback)(void *ctx, int yk_no, double *value);

    int  (*yt_get)(void *ctx, const char *app, const char *dev,
                   const char *name, double *value);
    int  (*yt_set)(void *ctx, const char *app, const char *dev,
                   const char *name, double value,
                   const char *datatype);

    int  (*modbus_send)(void *ctx, int port, const uint8_t *frame,
                        int frame_len, int timeout_ms, int retry,
                        uint8_t *response, int response_capacity,
                        int *response_len);

    int  (*soe_report)(void *ctx, const char *app, const char *model,
                       const char *dev, const char *event,
                       const char *ext_json);
    int  (*alarm_raise)(void *ctx, const char *code, int level,
                        const char *message);
    int  (*alarm_clear)(void *ctx, const char *code);

    int  (*sleep_ms)(void *ctx, int milliseconds);
    void (*log)(void *ctx, int level, const char *message);
} TTUScriptHostAPI;

#endif
```

实际实现可以根据现有 C/C++ 编译边界拆成多个接口，但脚本 VM 只依赖稳定的宿主定义。

### 6.2 VM 中的宿主状态

`VM` 建议增加：

```c
    TTUScriptHostAPI host;
    void *host_context;
    uint32_t capabilities;
    const char *script_name;
    int current_line;
    int task_id;
} TTUScriptRuntime;
```

VM：

```c
    CallFrame frames[FRAMES_MAX];
    int frameCount;
    Value stack[STACK_MAX];
    Value *stackTop;
    Value globals[512];

    TTUScriptRuntime ttu;
} VM;
```

初始化接口：

```c
void init_vm(VM *vm);
void init_vm_with_host(VM *vm,
                       const TTUScriptHostAPI *host,
                       void *host_context,
                       uint32_t capabilities,
                       const char *script_name);
```

### 6.3 capability 权限

建议定义：

```c
#define CAP_RTD_READ          (1u << 0)
#define CAP_RTD_WRITE         (1u << 1)
#define CAP_YX_WRITE          (1u << 2)
#define CAP_YC_WRITE          (1u << 3)
#define CAP_YK_PREPARE        (1u << 4)
#define CAP_YK_EXECUTE        (1u << 5)
#define CAP_YK_CANCEL         (1u << 6)
#define CAP_YT_WRITE          (1u << 7)
#define CAP_MODBUS_READ       (1u << 8)
#define CAP_MODBUS_WRITE      (1u << 9)
#define CAP_RAW_FRAME         (1u << 10)
#define CAP_SERIAL_IO         (1u << 11)
#define CAP_NETWORK_IO        (1u << 12)
#define CAP_CONFIG_READ       (1u << 13)
#define CAP_CONFIG_WRITE      (1u << 14)
#define CAP_CLOUD_REPORT      (1u << 15)
#define CAP_LOG               (1u << 16)
#define CAP_PROTOCOL_RESTART  (1u << 17)
#define CAP_SYSTEM_COMMAND    (1u << 18)
```

脚本执行前检查：

```c
int ttu_script_require_capability(VM *vm, uint32_t capability);
```

权限不足返回：

```text
ERR_PERMISSION_DENIED
```

推荐默认权限：

| 脚本类型 | 默认权限 |
|---|---|
| 监视脚本 | `CAP_RTD_READ`、`CAP_LOG`、`CAP_CLOUD_REPORT` |
| 保护脚本 | 监视权限加 `CAP_YK_PREPARE`、`CAP_YK_EXECUTE`、`CAP_YK_CANCEL` |
| 维护脚本 | RTDB、Modbus、配置读取，写权限单独审批 |
| 诊断脚本 | RTDB 读取、协议状态、日志、受控原始帧 |

危险权限默认关闭：

```text
CAP_SYSTEM_COMMAND
CAP_PROTOCOL_RESTART
CAP_CONFIG_WRITE
CAP_RAW_FRAME
CAP_SERIAL_IO
```

---

## 7. C 适配层设计

建议在 `linux_TTU/src/scripting/` 下增加：

```text
ttu_script_runtime.h/.c
ttu_script_host.h/.c
ttu_script_rtdb.c
```

其中：

| 文件 | 职责 |
|---|---|
| `ttu_script_runtime.c` | VM 创建、脚本加载、执行、销毁 |
| `ttu_script_register.c` | 注册所有原生模块和原语 |
| `ttu_script_rtdb.c` | 对接 `RealDev` 和 `ramdatabase` |
| `ttu_script_yk.c` | 对接 `common_yk`、IEC104 遥控队列和闭环反馈 |
| `ttu_script_yt.c` | 对接参数设置、PT/CT、IEC104 遥调 |
| `ttu_script_modbus.c` | 对接 Modbus 读写和链路服务 |
| `ttu_script_frame.c` | 纯函数式组帧、解帧和 CRC |
| `ttu_script_forward.c` | 转发表查询和映射 |
| `ttu_script_soe.c` | 本地 SOE 和数据中心 SOE |
| `ttu_script_cloud.c` | 实时数据、历史数据和 MQTT 业务接口 |
| `ttu_script_logic.c` | 逻辑引擎 V2 任务和元件包装 |
| `ttu_script_bzt.c` | 备自投、同期、有压合闸等业务包装 |
| `ttu_script_security.c` | capability、限流、审计和调用上下文 |

适配层职责：

1. 将脚本值转换为 C 类型；
2. 检查参数范围和点类型；
3. 检查 capability；
4. 获取 RTDB 锁或协议服务锁；
5. 调用现有接口；
6. 转换错误码；
7. 创建结果对象；
8. 记录审计日志；
9. 管理 C 缓冲区和脚本对象的生命周期。

禁止适配层把全局结构体指针直接放入脚本 `Value`。

---

## 8. 脚本语法建议

### 8.1 第一阶段兼容语法

第一阶段尽量不引入完整对象语法，可以使用普通函数调用：

```real
var voltage = yc_at(3, 45, 12);
var state = yx_at(3, 45, 13);

if (voltage < 180 && state) {
    var result = yk_execute_checked(12000, 1, 13, false, 3000, 2);
    if (!result.ok) {
        log_error(result.message);
    }
}
```

兼容优点：

- 当前 parser 已支持函数调用；
- 不必第一阶段实现属性访问和方法调用；
- 可以快速验证 VM 原生调用和 Host ABI。

缺点：

- 模块边界不够清晰；
- 函数数量较多；
- 参数名无法直接表达语义。

### 8.2 正式语法

正式版本推荐支持模块限定名：

```real
var voltage = yc.read_at(3, 45, 12);
var state = yx.read_at(3, 45, 13);

if (voltage < 180 && state) {
    var result = yk.execute_checked(
        12000,
        YK_ON,
        13,
        true,
        3000,
        2
    );
}
```

需要的词法和语法扩展：

- 点号 Token；
- 字符串字面量；
- 字节数组字面量；
- 数组字面量；
- 映射字面量；
- 具名参数或配置对象；
- 结果对象字段访问。

### 8.3 长期任务语法

建议后续支持：

```real
on start {
    log.info("script started");
}

every 100 ms {
    var voltage = yc.read_at(3, 1, 10);
}

on rising(yx.read(3, 1, 20)) {
    soe.report("app", "model", "device", "switch_on", []);
}

on alarm("comm_failure") {
    yk.off(12000);
}
```

如果语法扩展延期，可以先使用宿主注册函数：

```c
ttuscript_register_periodic_script("periodic_task", 100);
ttuscript_register_yx_event(real_no, TTU_EDGE_RISING,
                            "on_switch_changed");
```

---

## 9. 安全、并发和资源管理

### 9.1 线程模型

脚本不应直接运行在协议接收回调中。推荐：

```text
协议线程 / RTDB 线程
        │
        ├── 产生事件
        └── 投递到脚本任务队列

脚本任务线程
        │
        ├── 执行脚本
        ├── 调用 Host ABI
        └── 将请求投递给协议服务
```

同步原语如果底层需要等待，应使用受控超时，并禁止无限等待。

### 9.2 遥控安全流程

`yk.execute_checked()` 的推荐流程：

```text
1. 检查遥控号范围和映射
2. 检查脚本 capability
3. 检查本地/远方状态
4. 检查软压板和硬压板
5. 检查协议链路在线状态
6. 检查闭锁条件
7. 执行预置
8. 等待预置成功或超时
9. 执行命令
10. 等待遥信反馈
11. 反馈不匹配时按 retry_count 重试
12. 超时或失败时撤销和告警
13. 写入审计记录
```

### 9.3 限流

应对以下操作限流：

```text
遥控执行
遥调写入
Modbus 写帧
原始串口发送
配置写入
协议重启
```

限流参数：

```text
script_id
operation_type
point_or_link
minimum_interval_ms
burst_count
```

### 9.4 资源生命周期

脚本对象的所有权规则：

- C 层返回的字符串由 VM 对象系统管理；
- C 层返回的字节数组复制到 VM 管理的缓冲区；
- 协议响应缓冲区在转换成 `VAL_BYTES` 后立即释放；
- 历史数据和 SOE 查询结果转换完成后释放底层列表；
- `VAL_HANDLE` 必须带类型和关闭函数；
- 脚本结束时自动取消未完成的可取消句柄；
- 句柄不能跨 VM 使用。

---

## 10. 不应直接暴露的接口

以下接口应保留在 C/C++ 协议模块内部：

### 10.1 协议线程和状态机

```text
ModbusBlock::SndFrameData()
ModbusBlock::ModbusParaseData()
ModbusSlaveZfRtdb::UartModbusParaseData()
ModbusSlaveZfRtdb::FindOneFrameData()
```

原因：依赖线程、全局缓冲区、时序和内部状态。

### 10.2 全局实时库对象

内部对象：

```text
gt_RealDevConfigure
gt_RtdbFunc
gt_ZftableFunc
g_tRtdbGroup
```

脚本只能使用 `rtdb.*` 和 `forward.*` 包装。

### 10.3 MQTT 和 System V 消息底层

```text
Message_Send()
Message_Rcv()
Message_SendByType()
MqttArrvdmsgDeal()
```

脚本使用 `cloud.*` 和 `parameter.*` 业务接口。

### 10.4 全局环形缓冲区

内部对象：

```text
g_Iec104YKBuf
g_Iec104ParaBuf
```

脚本使用 `iec104.yk.*` 和 `iec104.parameter_set()`。

### 10.5 任意系统命令

不提供：

```real
system.exec("...");
```

如需要 GRE、网络状态等能力，提供固定白名单原语：

```text
network.gre.status() -> result
network.gre.enable() -> result
```

---

## 11. 实施分阶段计划

### 阶段 0：接口冻结和模拟宿主

交付：

- `TTUScriptHostAPI` 定义；
- 原生函数注册表；
- `VAL_STRING`、`VAL_BYTES`、`VAL_ARRAY`、`VAL_RESULT`；
- 模拟 RTDB、模拟遥控和模拟 Modbus；
- 原生函数调用测试。

不接入真实 linux_TTU 协议线程。

### 阶段 1：RTDB 和基础遥控

交付原语：

```text
rtdb.resolve
rtdb.read
rtdb.write
rtdb.read_at
rtdb.write_at
sleep_ms
log.info
log.warn
log.error
```

交付安全能力：

- capability；
- 点类型检查；
- 遥控预置/执行/撤销；
- 审计日志；
- 超时和限流。

### 阶段 2：Modbus 和字节数组

交付：

```text
VAL_BYTES
modbus.read_*
modbus.write_*
modbus.send
modbus.build_write_coil
modbus.build_write_register
modbus.build_write_registers
modbus.decode_*
modbus.crc16
```

首先实现纯函数式组帧，再接入实际链路发送。

### 阶段 3：遥调、IEC104 和数据中心

交付：

```text
parameter.*
cloud.realtime.*
cloud.history.*
cloud.change.*
```

### 阶段 4：SOE、告警和事件任务

交付：

```text
soe.*
on rising
on falling
on change
on alarm
```

### 阶段 5：逻辑引擎和专用业务

交付：

```text
logic.*
bzt.*
voltage_close.*
agc.*
avc.*
```

### 阶段 6：转发表、透传和运维

交付：

```text
forward.*
passthrough.*
port.*
terminal.*
protocol.status()
```

危险能力单独审批，不与普通脚本默认权限合并。

---

## 12. 测试设计

### 12.1 RealScript 核心测试

新增脚本：

```text
test_native_functions.real
test_string.real
test_bytes.real
test_result.real
test_capability.real
test_native_errors.real
```

覆盖：

- 原生函数调用；
- 参数数量错误；
- 参数类型错误；
- 原生函数返回错误；
- capability 拒绝；
- bytes 越界；
- 字符串长度限制；
- 结果字段访问；
- VM 结束时句柄清理。

### 12.2 RTDB 模拟测试

模拟接口：

```c
mock_rtdb_set(real_no, value);
mock_rtdb_set_type(real_no, type);
mock_rtdb_resolve(link, dev, reg, real_no);
mock_yk_expect_prepare(yk_no);
mock_yk_expect_execute(yk_no, value);
mock_yk_set_feedback(real_no, value);
```

示例：

```real
var voltage = yc.read_at(3, 1, 10);
if (voltage > 200) {
    var result = yk.execute_checked(12000, YK_ON, 101, true, 1000, 1);
}
```

### 12.3 Modbus 组帧测试

必须验证：

```text
0x05 帧长度、字段和 CRC
0x06 帧长度、字段和 CRC
0x10 单寄存器
0x10 双寄存器
非法寄存器数量
大小端解释
有 CRC 帧校验
错误 CRC 拒绝
```

### 12.4 遥控安全测试

必须验证：

```text
本地状态下遥控被拒绝
软压板未投入时遥控被拒绝
硬压板闭锁时遥控被拒绝
无权限脚本被拒绝
预置后超时自动撤销
执行后无反馈进入失败
反馈状态不匹配进入失败
失败重试次数正确
重复执行受到限流
脚本终止后未完成句柄被取消
```

### 12.5 并发和稳定性测试

必须验证：

- 多脚本同时读取 RTDB；
- 多脚本同时请求 Modbus；
- 脚本请求与协议线程同时访问同一链路；
- MQTT 数据中心请求超时；
- 脚本死循环或栈溢出；
- 脚本错误不影响协议线程；
- 脚本重载不泄漏 Value、字符串、字节数组和句柄；
- VM 达到最大调用帧、最大栈和最大原生调用数时行为明确。

---

## 13. 典型脚本示例

### 13.1 低电压保护示例

```real
var voltage = yc.read_at(3, 1, 10);
var breaker = yx.read_at(3, 1, 11);

if (voltage < 180 && breaker) {
    var result = yk.execute_checked(
        12000,
        YK_OFF,
        11,
        false,
        3000,
        2
    );

    if (!result.ok) {
        alarm.raise("LOW_VOLTAGE_TRIP_FAILED", ALARM_MAJOR,
                    result.message);
    }
}
```

### 13.2 Modbus 组帧和发送示例

```real
var frame = modbus.build_write_register(1, 0x0100, 220);

if (!modbus.crc16_check(frame)) {
    log.error("generated frame CRC is invalid");
} else {
    var result = modbus.send(1, frame, 1000, 3);
    if (!result.ok) {
        alarm.raise("MODBUS_WRITE_FAILED", ALARM_MAJOR,
                    result.message);
    }
}
```

### 13.3 参数设置示例

```real
var result = parameter.set(
    "1376.2App",
    "MCCB_guid",
    "ratio",
    50,
    "Int"
);

if (!result.ok) {
    log.error(result.message);
}
```

### 13.4 SOE 上报示例

```real
var ext = [
    {"name": "voltage", "value": "220"},
    {"name": "source", "value": "script"}
];

soe.report(
    "1376.2App",
    "MultiMeter",
    "MultiMeter_guid",
    "switch_open",
    ext
);
```

### 13.5 定时逻辑示例

```real
every 100 ms {
    var voltage = yc.read_at(3, 1, 10);

    if (voltage < 180) {
        timer.start("low_voltage", 5000);
    } else {
        timer.cancel("low_voltage");
    }

    if (timer.expired("low_voltage")) {
        yk.off(12000);
    }
}
```

---

## 14. 代码实现顺序

建议按以下顺序修改当前 RealScript：

1. 扩展 `ValueType`，增加字符串、字节数组和结果对象；
2. 增加对象析构和 VM 生命周期管理；
3. 增加原生函数注册表；
4. 增加原生函数调用字节码；
5. 增加字符串字面量；
6. 增加数组字面量；
7. 增加结果对象字段读取；
8. 增加 capability 检查；
9. 保留现有 `#N` 和 `#(L,D,R)` 语法；
10. 用 Host API 替换 `db.c` 中的模拟数据库；
11. 在 linux_TTU 中实现 RTDB 适配器；
12. 实现遥控适配器；
13. 实现 Modbus 纯函数组帧；
14. 实现 Modbus 发送适配器；
15. 实现 SOE、告警和数据中心适配器；
16. 最后实现事件任务、逻辑引擎和专用业务模块。

不建议一开始同时修改 lexer、parser、compiler、VM、所有协议模块和逻辑引擎。应先把 Host ABI 和模拟测试建立起来。

---

## 15. 版本和兼容策略

脚本语言版本建议使用：

```text
TTUScript 0.1：原生函数、RTDB、基础遥控
TTUScript 0.2：字符串、字节数组、Modbus 组帧
TTUScript 0.3：遥调、IEC104、数据中心、SOE
TTUScript 0.4：事件和周期任务
TTUScript 0.5：逻辑引擎和专用业务
TTUScript 1.0：接口冻结和现场部署
```

兼容规则：

1. 已发布脚本原语不能无版本地改变参数含义。
2. 新增参数优先通过新函数或可选参数完成。
3. C 层接口变更要保留旧适配入口，至少跨一个主版本。
4. 脚本文件应声明语言版本：

```real
language "TTUScript 0.2";
```

5. 脚本权限和宿主能力应独立于语言版本。

---

## 16. 最终推荐的第一批原语

如果需要控制第一阶段范围，优先实现以下接口：

```text
log.info
log.warn
log.error
sleep_ms

rtdb.resolve
rtdb.type
rtdb.read
rtdb.write
rtdb.read_at
rtdb.write_at

yx.read

yc.read

yk.prepare

yt.get
parameter.get
parameter.set

modbus.read
modbus.write
modbus.build_write_coil
modbus.build_write_register
modbus.build_write_registers
modbus.crc16
modbus.decode

soe.latest
soe.report
alarm.raise
alarm.clear
```

这批接口已经覆盖：

- 实时库；
- 遥信；
- 遥测；
- 遥控；
- 遥调；
- Modbus 读写；
- Modbus 组帧；
- CRC；
- SOE；
- 告警；
- 日志；
- 定时等待。

---

## 17. 结论

最终建议不是把 `linux_TTU` 的所有 C 函数逐个暴露给脚本，而是建立一层稳定的 TTU 业务抽象：

```text
脚本业务原语
    ↓
统一结果、权限、审计和超时
    ↓
TTU Host ABI
    ↓
现有 RTDB、遥控、遥调、Modbus、IEC、DLT645、MQTT 和逻辑引擎
```

最重要的工程决策：

1. `rtdb.*` 负责数据，`yx.*`、`yc.*`、`dd.*`、`para.*` 负责类型化访问。
2. `yk.*` 负责统一遥控事务，底层协议预置/执行/撤销由适配器处理。
3. `yt.*` 和 `parameter.*` 负责遥调，不把参数回调函数数组直接暴露给脚本。
4. `modbus.*` 同时提供业务读写和纯函数式组帧、解码、CRC。
5. `forward.*` 提供转发表查询和映射，不暴露全局转发表结构。
6. `soe.*`、`alarm.*` 和 `cloud.*` 将事件、告警和数据中心业务统一起来。
7. 逻辑引擎作为脚本的执行后端和可选编排服务，而不是重新复制一套逻辑元件实现。
8. 所有高风险能力都必须经过 capability、闭锁检查、超时、限流和审计。

后续开发时，应优先完成阶段 0 和阶段 1，先证明“RealScript 原生调用 + TTU Host ABI + RTDB/遥控模拟测试”这条链路可用，再逐步接入真实协议模块。

---

## 18. 可视化编程与脚本语言一体化设计

### 18.1 现场使用目标

TTUScript 不是替代现有可视化编程，而是作为其可控扩展。最终现场交付应满足以下工作方式：

1. **常规需求优先通过可视化完成**：采集、比较、逻辑门、定时、边沿、RTDB 写入、标准遥控、标准遥调、PI、变化检测等由经过验证的元件库拖拽配置。
2. **复杂需求通过脚本节点补足**：多条件联锁、批量 Modbus 组帧、转发表规则、动态映射、数据计算、协议组合、特殊告警、业务流程和设备差异由脚本实现。
3. **画布负责结构，脚本负责算法**：流程拓扑、数据来源、执行周期、输出去向和安全边界在画布上清晰可见；脚本只实现局部、可命名、可测试的逻辑。
4. **同一套运行治理**：画布元件和脚本节点使用同一 RTDB、同一 Host ABI、同一 capability、同一遥控服务、同一日志、同一审计和同一热加载事务。
5. **现场可配置而非现场改 C/C++**：现场工程师通过画布选择点、配置参数、连接节点、引用经过审核的脚本模板；高级工程师才编辑脚本源码。

目标不是让每个现场人员编写通用程序，而是将绝大多数现场差异收敛为：

```text
点表选择 + 元件参数 + 连线拓扑 + 脚本模板参数 + 权限审批
```

### 18.2 现有可视化编程能力和可复用基础

当前 `linux_TTU` 已有可视化编程规约，属于 `ProtoType_VisualProgramming = 57`，配置文件规则为 `c[链路号]57.json`。依据：

- `~/repo/linux_TTU/src/logicengine/logic_visual_programming.h:8-10`
- `~/repo/linux_TTU/src/realdatabase/ramdatabase.h:174`

现有机制已经提供下列重要基础：

| 能力 | 当前实现和依据 | 一体化设计中的用途 |
|---|---|---|
| 按链路运行 | 每链路一个 `t_VisualProgContext` | 作为图和脚本包的共同部署边界 |
| 多逻辑任务 | 每链路最多 10 个任务 | 每个任务可包含普通元件和脚本元件 |
| 元件上限 | 每任务最多 500 个元件 | 脚本元件计为一个元件，防止画布失控 |
| 端口和参数 | 每元件最多 16 个输入、16 个输出、32 个参数 | 作为脚本节点的可视化 I/O 契约 |
| JSON 配置 | 根对象 `LogicTasks`，每任务包含 `Elements` | 扩展同一配置模型，不另建平行配置系统 |
| 配置校验 | 元件 ID、下一节点、输入引用校验 | 增加脚本语法、端口、权限和副作用校验 |
| 热加载 | 稳定窗口、MD5、快照、停止旧任务、启动新任务、失败恢复旧任务 | 图与脚本包应作为一个原子版本整体切换 |
| 运行监控 | 任务 ID、线程、扫描周期和元件数量 | 增加脚本状态、版本、耗时和最后错误 |

关键源码依据：

- 上下文、加载、启动、停止、重载接口：`logic_visual_programming.h:41-138`；
- 稳定文件校验和快照：`logic_visual_programming.c:67-175`；
- 热加载、启动失败回滚：`logic_visual_programming.c:352-464`；
- `LogicTasks`、`Elements`、ID 和 `NextID` 解析：`logic_visual_programming.c:753-973`；
- 任务扫描和节点顺序执行：`logic_visual_programming.c:638-750`；
- 基础逻辑、定时、I/O、算术和控制元件注册：`logic_engine_v2.c:70-119`。

当前 I/O 元件已经能够通过 `t_LogicEngineV2_DataInterface` 读写 YC、YX、参数和 YK。依据：`logic_engine_v2.h:243-259`、`element_io.c:60-199`。因此脚本节点应接入同一个服务边界，而不是自行绕过 RTDB 或遥控实现。

### 18.3 当前可视化实现的边界与需要修正的事实

现有实现可作为基础，但不应原样承载高风险脚本控制。需要在集成时处理以下边界：

1. `VisualProg_ExecuteYk()` 当前仅打印日志并返回成功，未调用真实遥控，见 `logic_visual_programming.c:621-627`。可视化 `YK_OUTPUT` 和未来脚本 `yk.*` 必须统一改为调用受控遥控服务。
2. `TIMER_DELAY` 当前使用 `usleep()` 阻塞任务扫描线程，见 `elements/element_timer.c:145-177`。脚本定时器和可视化定时器应改为非阻塞状态机，避免一个延时节点阻塞同任务的采集、保护或告警逻辑。
3. 当前任务按 `NextID` 顺序执行，输入端口通过源元件 ID 解析，见 `logic_visual_programming.c:650-741`。一体化设计必须在加载期构建和校验执行计划，至少检测无效跳转、环路、未初始化输入、跨任务连接和脚本端口不匹配。
4. 当前可视化参数使用通用 JSON 对象，但没有脚本源码、脚本哈希、权限、输入输出契约和副作用元数据。新配置必须补全这些字段。
5. 当前 MQTT 直接加载配置被明确拒绝，要求通过配置文件原子替换触发热加载，见 `logic_visual_programming.c:1334-1349`。脚本包也必须遵守相同原则，不能绕过稳定校验直接执行下发源码。

这些是架构集成的前提，不是要求一次性重写全部可视化模块。

### 18.4 一体化模型：一个任务、一个图、两类节点

每个现场逻辑任务统一表示为：

```text
VisualLogicTask
    ├── 元数据：名称、说明、链路、扫描周期、启停状态、版本、责任人
    ├── 图：标准元件、脚本元件、连接、顺序边和错误边
    ├── 脚本包：内嵌脚本或受控脚本文件引用
    ├── I/O 契约：每个脚本节点的输入、输出、类型和默认值
    ├── 安全策略：capability、点范围、遥控白名单、限流和审批状态
    └── 运行状态：部署版本、配置哈希、脚本哈希、最后结果和诊断信息
```

画布支持两类节点：

| 节点类别 | 用途 | 运行方式 |
|---|---|---|
| 标准元件 | 已有 YC/YX 输入输出、逻辑门、比较、定时、算术、PI、变化检测、YK/YT 输出等 | 继续由逻辑引擎执行 |
| 脚本元件 | 将有限个画布端口输入传入 TTUScript，得到有限个输出和结果 | 由 `SCRIPT_*` 元件调用嵌入式 VM |

脚本不是独立于画布的“黑盒后台程序”。每个脚本元件必须在画布上显示：

- 脚本名称和版本；
- 输入端口名称、类型、来源；
- 输出端口名称、类型、去向；
- 申请的 capability；
- 是否允许 RTDB 写入、遥控、遥调、Modbus 写和云端上报；
- 最近一次执行状态、耗时、返回码和最后错误；
- 脚本摘要和源文件哈希。

### 18.5 脚本节点类型

第一阶段建议只实现四类脚本节点，避免每个节点都拥有不可控副作用。

#### 18.5.1 `SCRIPT_EXPRESSION`

用途：纯计算、单位换算、阈值、条件、组合计算、位处理和数据整形。

约束：

```text
允许：输入端口、局部变量、纯算术、比较、数组/字节处理
禁止：RTDB 写、遥控、遥调、Modbus 发送、透传、云端上报
```

典型画布：

```text
YC_INPUT ─┐
          ├── SCRIPT_EXPRESSION ── COMPARE_RANGE ── YX_OUTPUT
CONST ────┘
```

脚本接口约定：

```real
fn run(inputs) {
    return {
        voltage_kv: inputs.voltage / 1000.0,
        valid: inputs.raw > 0
    };
}
```

#### 18.5.2 `SCRIPT_DECISION`

用途：多条件联锁、状态机分支、故障判别、复杂转发表选择、设备型号差异处理。

约束：默认纯决策，不直接产生高风险动作；输出布尔、整数、枚举、原因码和诊断信息。

典型画布：

```text
YC_INPUT ─┐
YX_INPUT ─┼── SCRIPT_DECISION ── IF ── YK_OUTPUT
YX_INPUT ─┘                       └── ALARM_OUTPUT
```

这样现场人员可以直观看到“什么条件触发控制”，高风险输出仍经可视化的受控 `YK_OUTPUT` 节点执行。

#### 18.5.3 `SCRIPT_ACTION`

用途：确实需要在脚本内部编排多个操作的场景，例如：

- Modbus 组帧并执行多步读写；
- 多设备按顺序遥控并等待反馈；
- SOE、告警和云端上报组合；
- 复杂的 IEC104/Modbus/转发表流程；
- 专用业务的受控流程编排。

约束：必须显式在节点配置中声明副作用和 capability；每个动作都要经过 Host ABI 的权限、限流、闭锁和审计。

输入：触发信号、数值输入、业务上下文。

输出：

```text
ok: bool
code: int
stage: string
message: string
completed: bool
feedback: value
```

典型画布：

```text
RISING_EDGE ── SCRIPT_ACTION ── RESULT_OK ── LOG_OUTPUT
                    │
                    └────────── RESULT_FAIL ── ALARM_OUTPUT
```

#### 18.5.4 `SCRIPT_TASK`

用途：整条任务都由脚本实现，但仍由可视化平台管理其周期、权限、版本、运行状态和输入点清单。

适用场景：

- 设备厂家差异较大；
- 复杂通信帧处理；
- 多阶段状态机；
- 算法密集但不适合拆成大量画布节点的逻辑；
- 已验证的业务模板整体复用。

`SCRIPT_TASK` 仍必须在画布中显示为一个任务卡片，并提供可视化端口、状态、权限和日志入口。它不是绕过平台治理的自由脚本。

### 18.6 端口与脚本 I/O 契约

脚本节点与可视化图之间只能通过声明式端口交换数据。

建议在 `t_ElementPort` 的现有类型基础上扩展：

```c
typedef enum {
    DATA_TYPE_BOOL = 0,
    DATA_TYPE_INT,
    DATA_TYPE_FLOAT,
    DATA_TYPE_DOUBLE,
    DATA_TYPE_STRING,
    DATA_TYPE_BYTES,
    DATA_TYPE_RESULT
} eDataType;
```

第一阶段，画布端口只允许 `BOOL`、`INT`、`FLOAT`、`DOUBLE`；字符串、字节数组和结果对象可通过脚本节点的受控附加端口或诊断输出使用。这样能保持现有 `double value` 端口模型的兼容性。

脚本节点配置必须声明：

```text
输入端口：名称、类型、必填、默认值、允许来源
输出端口：名称、类型、是否只读、默认值
结果端口：ok、code、stage、message、elapsed_ms
```

运行时转换规则：

```text
画布 BOOL   → TTUScript bool
画布 INT    → TTUScript int
画布 FLOAT  → TTUScript double
画布 DOUBLE → TTUScript double

TTUScript bool/int/double → 按端口声明转换回画布值
TTUScript nil 或类型不匹配 → 节点失败，输出默认值并记录诊断
```

禁止脚本通过全局变量名隐式访问画布中其他节点的端口；所有跨节点数据必须经过连线。这是可视化可读性、静态校验和现场审计的基础。

### 18.7 统一配置包

推荐使用一个可原子部署的“逻辑包”，包含图配置、脚本源码、元数据和哈希。

建议目录：

```text
logic-pack/
├── manifest.json
├── graph.json
├── scripts/
│   ├── voltage_judgement.real
│   ├── modbus_action.real
│   └── templates/
│       └── low_voltage_trip.real
├── tests/
│   ├── voltage_judgement.test.json
│   └── modbus_action.test.json
└── assets/
    └── optional-ui-assets/
```

现场运行时可以继续落在 `c[链路号]57.json` 兼容入口，但该 JSON 应引用或内嵌版本化逻辑包。推荐最终 JSON 根结构：

```json
{
  "SchemaVersion": "VisualTTU/1.0",
  "Package": {
    "Id": "low-voltage-protection",
    "Version": "1.2.0",
    "Description": "低电压保护与告警",
    "CreatedBy": "engineering",
    "ApprovedBy": "protection-review",
    "Hash": "sha256:..."
  },
  "LogicTasks": [
    {
      "TaskId": 1,
      "Name": "low_voltage_protection",
      "Description": "现场低压跳闸逻辑",
      "Enable": true,
      "ScanInterval_ms": 100,
      "Capabilities": ["rtdb.read", "yk.execute", "alarm.raise"],
      "Elements": [
        {
          "Id": 10,
          "Type": "YC_INPUT",
          "NextID": 20,
          "Params": [{
            "Name": "linkdev",
            "Value": {"Link": 3, "Dev": 1, "Reg": 10}
          }]
        },
        {
          "Id": 20,
          "Type": "SCRIPT_DECISION",
          "NextID": 30,
          "Script": {
            "Id": "voltage_judgement",
            "Path": "scripts/voltage_judgement.real",
            "Entry": "run",
            "Sha256": "...",
            "Timeout_ms": 10,
            "MaxInstructions": 5000,
            "Capabilities": ["rtdb.read"]
          },
          "Inputs": [{"Name": "voltage", "SourceElemId": 10, "SourcePortId": 0}],
          "Outputs": [
            {"Name": "trip", "Type": "BOOL"},
            {"Name": "reason", "Type": "INT"}
          ]
        },
        {
          "Id": 30,
          "Type": "YK_OUTPUT",
          "NextID": -1,
          "Inputs": [{"SourceElemId": 20, "SourcePortId": 0}],
          "Params": [{"Name": "YkInf", "Value": {"YkNo": 12000, "Command": 0}}]
        }
      ]
    }
  ]
}
```

说明：示例中的字段用于设计说明，实际落地时需要使 `SCRIPT_*` 与现有 `ParseConfig` 的字段风格一致。原有标准元件 JSON 字段应继续兼容。

### 18.8 配置、编译和部署流程

完整生命周期：

```text
画布编辑 / 模板选择 / 脚本编辑
        ↓
前端静态校验
        ↓
逻辑包生成（graph + scripts + manifest + hash）
        ↓
离线编译脚本、元件校验和仿真测试
        ↓
审批与签名
        ↓
原子替换现场逻辑包
        ↓
现有稳定窗口、MD5、快照校验
        ↓
加载到临时上下文并校验图、脚本、权限、点表
        ↓
停止旧任务 → 启动新任务
        ↓
失败则销毁临时上下文并恢复旧版本
```

加载阶段新增校验：

1. JSON Schema 版本匹配；
2. 每个标准元件类型存在；
3. 每个 `SCRIPT_*` 节点类型存在；
4. 脚本文件存在、大小受限、哈希匹配；
5. 脚本可以在不执行副作用的编译模式下完成 parse 和 compile；
6. 脚本入口函数存在，参数数量与端口契约一致；
7. 输入输出端口类型兼容；
8. 所需 capability 不超过任务和设备允许权限；
9. 遥控号、RTDB 点、链路、设备、寄存器和转发表引用有效；
10. 不存在禁止的跨任务连接、未初始化输入或非法环路；
11. 每个副作用节点都有错误输出、日志策略和限流策略；
12. 逻辑包的仿真用例全部通过，或有明确的受控豁免记录。

现有稳定文件、MD5、快照和失败回滚机制应继续复用，不能为脚本部署开辟不受保护的旁路。

### 18.9 运行模型和调度

每个 `t_LogicTask` 保持一个执行上下文：

```text
TaskRuntime
    ├── 图执行计划
    ├── 标准元件状态
    ├── 每个脚本节点的已编译 ObjFunction
    ├── 每个脚本节点的 VM 或隔离执行上下文
    ├── 权限集合
    ├── 指令/时间/内存预算
    ├── 节点运行状态和诊断环形缓冲区
    └── 任务取消令牌
```

调度规则：

1. 一个任务只能由其所属任务线程串行执行，避免同一脚本节点重入。
2. 不同任务可以并发，但所有 RTDB、遥控、Modbus 和 MQTT 操作通过各自服务层串行化或加锁。
3. `SCRIPT_EXPRESSION` 和 `SCRIPT_DECISION` 在本次扫描周期内同步执行。
4. `SCRIPT_ACTION` 允许发起异步请求，但不得无限阻塞扫描线程；未完成的请求通过句柄和后续周期轮询结果。
5. 脚本执行应有 `MaxInstructions` 和 `Timeout_ms`；超过预算视为节点失败，记录 `ERR_SCRIPT_BUDGET_EXCEEDED`。
6. 脚本节点失败后按配置选择：输出默认值、走错误边、重试、冻结输出、告警或停用任务。
7. 脚本不得调用 `sleep_ms()` 长时间阻塞图任务；延时必须使用任务定时状态机或可视化定时节点。
8. 运行时记录节点耗时、最近 N 次返回码、最近 N 条脚本日志和输出快照。

### 18.10 执行图语义

当前可视化任务使用 `NextID` 指定顺序流程，并通过输入端口引用源元件输出。为保持兼容，第一阶段沿用此模式；新增脚本节点也必须有 `NextID`。

后续建议将配置语义明确分成两类边：

```text
DataEdge：源元件输出 → 目标元件输入
ControlEdge：当前节点 → 下一执行节点，或成功/失败/超时分支
```

建议逐步增加：

```text
NextID        正常执行后继
OnSuccessID   成功后继
OnFailureID   失败后继
OnTimeoutID   超时后继
```

这样可以将脚本动作和错误处理明确画在画布上：

```text
SCRIPT_ACTION
    ├── OnSuccessID → 更新状态 / 继续流程
    ├── OnFailureID → 告警 / 回滚 / 限流等待
    └── OnTimeoutID → 撤销遥控 / 通信故障处理
```

### 18.11 标准元件、脚本元件和模板库的职责划分

应建立明确分层，避免现场把所有工作都塞进长脚本：

| 需求类别 | 首选实现 | 脚本是否允许 |
|---|---|---|
| 单点采集、RTDB 映射 | `YC_INPUT`、`YX_INPUT`、`YC_OUTPUT`、`YX_OUTPUT` | 仅在特殊类型映射时使用 |
| 常规逻辑 | AND、OR、NOT、XOR、IF、比较器 | 可用于复杂表达式 |
| 常规定时 | 标准非阻塞定时元件 | 仅用于复杂状态机 |
| 标准遥控 | 受控 `YK_OUTPUT` | 可通过 `SCRIPT_ACTION` 编排，但不能绕过服务 |
| 标准遥调 | 受控 `YT_OUTPUT` | 可用于批量或条件化参数设置 |
| Modbus 帧 | 模板节点 + 配置参数 | 特殊寄存器编码、厂家差异、批量流程 |
| 报表和上报 | 标准云端/SOE 元件 | 数据整形、规则路由、特殊上报 |
| 备自投/同期/AGC/AVC | 专用标准组件 | 特殊现场策略必须经过审批脚本模板 |

模板库至少提供：

```text
低压告警与跳闸
遥控预置-执行-反馈
Modbus 单寄存器写
Modbus 多寄存器写
Modbus 读-解析-写 RTDB
遥信变位生成 SOE
遥测越限告警
遥调边沿触发
IEC104 参数下发
设备通信超时处理
转发表映射
分时段策略
AGC/AVC 启停
备自投使能与状态上报
```

每个模板应包含：画布图、脚本源码、默认参数、允许的点类型、权限、仿真用例、版本和变更记录。

### 18.12 可视化编辑器要求

现场编辑器至少应提供：

1. **点表选择器**：按链路、设备、寄存器、实时库号、类型、描述搜索；不要求人员手输 RTDB 号。
2. **协议参数表单**：Modbus 从站、功能码、寄存器、数据类型、字节序、比例系数、超时和重试可视化配置。
3. **脚本节点编辑器**：只编辑该节点绑定的脚本，显示输入输出契约、语法错误、类型错误、权限和预算。
4. **模板参数表单**：默认操作模板优先填写参数，不直接暴露源码。
5. **连线类型检查**：布尔、整数、浮点、字节和结果端口不兼容时禁止发布。
6. **权限和风险提示**：拖入 YK、YT、Modbus 写、原始帧或配置写节点时显示必需权限、白名单和审批状态。
7. **流程仿真**：输入模拟 YC/YX、显示端口变化、脚本日志、计划的遥控/帧发送，但默认不执行真实副作用。
8. **在线诊断**：显示任务是否运行、当前版本、MD5、节点执行周期、最后错误、最近输出和审计记录。
9. **版本差异**：比较两个逻辑包的元件、连线、脚本哈希、权限和遥控目标变化。
10. **回滚**：选择已部署版本并通过同一原子热加载机制恢复。

### 18.13 脚本节点 C 实现设计

建议新增：

```text
linux_TTU/src/logicengine/elements/element_script.c
linux_TTU/src/logicengine/elements/element_script.h
linux_TTU/src/logicengine/script_bridge.c
linux_TTU/src/logicengine/script_bridge.h
```

建议新增元件注册：

```c
RegisterElementType("SCRIPT_EXPRESSION", ScriptExpression_Create);
RegisterElementType("SCRIPT_DECISION", ScriptDecision_Create);
RegisterElementType("SCRIPT_ACTION", ScriptAction_Create);
RegisterElementType("SCRIPT_TASK", ScriptTask_Create);
```

脚本元件私有状态建议：

```c
typedef struct {
    char script_id[64];
    char script_path[256];
    char entry_name[64];
    unsigned char script_hash[32];
    ObjFunction *function;
    VM vm;

    uint32_t capabilities;
    int timeout_ms;
    int max_instructions;
    int failure_policy;

    int input_count;
    int output_count;
    int last_error_code;
    char last_error[256];
    uint64_t last_elapsed_us;
    bool compiled;
} t_ScriptElementPrivate;
```

`element_script.c` 的执行流程：

```text
1. 读取画布输入端口
2. 依据端口契约转换成 TTUScript 参数
3. 设置 VM 当前任务、节点、能力和取消令牌
4. 执行指定入口函数
5. 校验返回值与输出端口契约
6. 更新画布输出、结果端口、诊断和审计
7. 失败时执行节点失败策略或走失败控制边
```

`script_bridge.c` 负责创建 `TTUScriptHostAPI`，并确保脚本和可视化输出都调用同一业务服务。例如：

```text
SCRIPT_ACTION yk.execute()
        └── TTUScriptHostAPI.yk_execute()
                └── TtuControlService.execute_checked()

YK_OUTPUT
        └── TtuControlService.execute_checked()
```

禁止存在两套不同的遥控实现。

### 18.14 统一控制服务

为解决当前 `VisualProg_ExecuteYk()` 尚未实际接入控制的问题，应新增统一服务层：

```text
TtuControlService
    ├── validate_target()
    ├── check_permission()
    ├── check_local_remote()
    ├── check_soft_hard_plate()
    ├── check_interlock()
    ├── prepare()
    ├── execute()
    ├── wait_feedback()
    ├── cancel()
    ├── rate_limit()
    └── audit()
```

调用来源可以是：

```text
IEC101/104 主站请求
MQTT 自定义遥控
可视化 YK_OUTPUT
可视化 SCRIPT_ACTION
TTUScript 独立任务
备自投或同期合闸逻辑
```

所有来源必须得到同一结果对象和审计记录，避免某个入口绕开闭锁、反馈或限流。

### 18.15 可视化/脚本的安全和权限继承

权限从逻辑包到任务再到脚本节点逐级收缩：

```text
设备全局允许权限
    ∩ 逻辑包审批权限
        ∩ 任务声明权限
            ∩ 节点声明权限
                ∩ 脚本原语实际所需权限
```

任何一级不允许，调用即拒绝。

示例：

```text
逻辑包：允许 rtdb.read、yk.execute、alarm.raise
任务：只允许 rtdb.read、yk.execute
脚本节点：声明 rtdb.read
脚本实际调用 alarm.raise
结果：拒绝，因为任务未授予 alarm.raise
```

高风险节点还需要目标白名单：

```text
允许的 RTDB 写入范围
允许的遥控号集合
允许的遥调回调和地址范围
允许的 Modbus 链路、从站、功能码和寄存器范围
允许的 MQTT 主题或数据中心业务类型
```

### 18.16 仿真、预演和现场验收

可视化与脚本一体化后，必须支持“先仿真，后下装”：

```text
输入回放 / 模拟点表
        ↓
可视化执行图
        ↓
脚本节点
        ↓
模拟 Host API
        ↓
输出追踪、计划动作、告警和帧记录
```

仿真模式规则：

- `yk.*` 不发真实遥控，只记录计划动作和校验结果；
- `yt.*` 不写真实参数，只记录目标和值；
- `modbus.send()` 不写串口，只返回模拟响应；
- `cloud.*` 不向真实 MQTT 发布；
- 每次扫描保留端口快照和脚本结果；
- 可以导入 SOE、YC/YX 变化和历史序列回放。

现场验收应至少包含：

```text
图结构校验通过
脚本编译通过
端口类型校验通过
权限和白名单校验通过
模板测试通过
仿真用例通过
热加载成功
失败配置保持旧任务运行
遥控预置、执行、反馈和撤销记录完整
节点异常不会停止无关任务
```

### 18.17 可视化与脚本一体化实施阶段

在第 0 至第 6 阶段计划之外，增加以下交付序列：

#### 阶段 V1：只读脚本表达式节点

交付：

```text
SCRIPT_EXPRESSION
脚本输入/输出端口绑定
语法、编译、类型和预算校验
画布显示脚本状态和最后错误
仿真 Host API
```

该阶段不允许任何副作用，适合先验证编辑器、配置包、编译、热加载和端口模型。

#### 阶段 V2：脚本决策节点和标准输出元件

交付：

```text
SCRIPT_DECISION
OnSuccess/OnFailure 控制边
脚本输出连接到标准 YK_OUTPUT、YT_OUTPUT、ALARM_OUTPUT
统一 TtuControlService 接入标准 YK_OUTPUT
```

该阶段推荐用于绝大多数保护、联锁和现场流程需求：脚本决定，标准元件执行。

#### 阶段 V3：受控脚本动作节点

交付：

```text
SCRIPT_ACTION
capability 和目标白名单
异步请求句柄
Modbus/IEC104/SOE/云端动作原语
结果端口、失败边、审计和限流
```

#### 阶段 V4：模板市场和现场交付工具

交付：

```text
逻辑包 manifest
模板库
参数表单
版本比较
审批和签名
离线仿真和回放
原子部署、回滚和在线诊断
```

### 18.18 最终现场能力边界

达到目标后的现场配置能力应覆盖：

```text
点表映射、数据换算、死区、阈值、越限
逻辑门、条件、边沿、周期、延时、状态机
遥控、遥调、闭锁、反馈、重试和告警
Modbus 采集、寄存器解析、组帧、写入和异常处理
转发表查找、协议数据转换和设备型号适配
SOE、本地事件、云端实时数据和历史数据交互
备自投、同期合闸、有压合闸、AGC、AVC 的模板化编排
串口/网络透传的受控配置
现场问题诊断、仿真回放、版本回滚和审计
```

工程上应以“90% 需求由模板和画布参数完成，10% 差异由受控脚本节点完成”为目标，而不是追求让每个现场都编写大量自由脚本。这样既获得灵活性，也保留电力现场所需的可见性、可验证性、可审计性和安全边界。
