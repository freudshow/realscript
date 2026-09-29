/***************************************************************************
* 文件名: ttu_script_adapter.c
* 作者: 未知
* 版本号: 1.0
* 生成日期: 2026.09.27
* 概述: 为脚本运行时提供经过边界校验的 TTU 实时库和遥控宿主回调。
* 修改日志: 新建 TTUScript 宿主适配接口。
**************************************************************************/
#include "prjcfg.h"
#include "ttu_script_adapter.h"
#include "common_yk.h"
#include <math.h>
#include <stdio.h>

/***************************************************************************
* 函数名称: TtuScriptHost_Read
* 功能描述: 读取实时库数值，屏蔽不可直接映射的遥控点。
* 输入参数: context保留；real_no为0至32767；type为有效RTDB类型或-1。
* 输出参数: value成功时返回实时库数值。
* 返回值: 0成功，负数表示参数、点号或底层读取失败。
* 补充信息(使用注意事项): 通过现有实时库函数访问，不暴露内部全局指针。
* 修改日志: 2026.09.27 新增。
**************************************************************************/
int TtuScriptHost_Read(void *context, int real_no, int type, double *value)
{
    float real_value;
    unsigned char point_type;

    (void)context;
    if (value == NULL || real_no < 0 || real_no > 32767)
    {
        return -1;
    }
    *value = 0.0;
    point_type = GetRtdbTypeByRtdbNo((short)real_no);
    if (point_type > Type_PARA || point_type == Type_YK || (type >= 0 && type != point_type))
    {
        return -2;
    }
    real_value = GetValueByRealNo(real_no);
    if (!isfinite(real_value))
    {
        return -3;
    }
    *value = (double)real_value;
    return 0;
}

/***************************************************************************
* 函数名称: TtuScriptHost_Write
* 功能描述: 通过现有 RTDB 写接口写入有数值表示的点。
* 输入参数: context保留；real_no为0至32767；type为-1或对应点类型；value为有限值。
* 输出参数: 无。
* 返回值: 0成功，负数表示参数、类型或底层写入失败。
* 补充信息(使用注意事项): 不允许经此接口执行遥控。
* 修改日志: 2026.09.27 新增。
**************************************************************************/
int TtuScriptHost_Write(void *context, int real_no, int type, double value)
{
    unsigned char point_type;
    int write_result;

    (void)context;
    if (real_no < 0 || real_no > 32767 || !isfinite(value) || value > 3.4e38 || value < -3.4e38)
    {
        return -1;
    }
    point_type = GetRtdbTypeByRtdbNo((short)real_no);
    if (point_type > Type_PARA || point_type == Type_YK || (type >= 0 && type != point_type))
    {
        return -2;
    }
    write_result = SetValueByRealNo(real_no, (float)value);
    return write_result;
}

/***************************************************************************
* 函数名称: TtuScriptHost_Resolve
* 功能描述: 将链路、设备和寄存器地址转换为实时库号。
* 输入参数: context保留；link、dev、reg必须在short可表示范围内。
* 输出参数: real_no成功时输出实时库号。
* 返回值: 0成功，负数表示参数或实时库查找失败。
* 补充信息(使用注意事项): 使用 TTU 实时库的 FastGetRtdbNo 映射。
* 修改日志: 2026.09.27 新增。
**************************************************************************/
int TtuScriptHost_Resolve(void *context, int link, int dev, int reg, int *real_no)
{
    int32_t resolved;

    (void)context;
    if (real_no == NULL || link < 0 || link > 32767 || dev < 0 || dev > 32767 || reg < 0 || reg > 32767)
    {
        return -1;
    }
    *real_no = -1;
    resolved = gt_RtdbFunc.FastGetRtdbNo((short)link, (short)dev, (short)reg);
    if (resolved < 0 || resolved > 32767)
    {
        return -2;
    }
    *real_no = (int)resolved;
    return 0;
}

/***************************************************************************
* 函数名称: TtuScriptHost_ExecuteYk
* 功能描述: 将已授权的遥控动作转发给现有公共遥控实现。
* 输入参数: context保留；yk_no须在公开遥控号范围内；value仅允许0或1。
* 输出参数: 无。
* 返回值: 底层操作成功返回0，否则返回负值。
* 补充信息(使用注意事项): 生产调用还必须在脚本服务层执行 capability、闭锁、审批及审计检查；此函数不模拟安全闭环。
* 修改日志: 2026.09.27 新增。
**************************************************************************/
int TtuScriptHost_ExecuteYk(void *context, int yk_no, int value)
{
    int result;

    (void)context;
    if (yk_no < 0 || yk_no > 65535 || (value != 0 && value != 1))
    {
        return -1;
    }
    result = CommonYkOperation(YkAttribute_CO,
                               value ? CommonYkCmd_ScoOn : CommonYkCmd_ScoOff,
                               (unsigned short int)yk_no,
                               CommonYkCmd_Init);
    return result;
}

/***************************************************************************
* 函数名称: TtuScriptHost_Init
* 功能描述: 初始化 TTU Host ABI 回调表。
* 输入参数: api为调用方提供的有效回调表地址。
* 输出参数: api获得 RTDB 查询/写入、遥控和日志回调。
* 返回值: 0成功，负数表示参数无效。
* 补充信息(使用注意事项): 当前只提供本仓库已确认可映射的原语，不伪造参数、SOE及协议服务。
* 修改日志: 2026.09.27 新增。
**************************************************************************/
int TtuScriptHost_Init(t_TtuScriptHostApi *api)
{
    if (api == NULL)
    {
        return -1;
    }
    api->rtdb_read = TtuScriptHost_Read;
    api->rtdb_write = TtuScriptHost_Write;
    api->rtdb_resolve = TtuScriptHost_Resolve;
    api->yk_execute = TtuScriptHost_ExecuteYk;
    api->log = NULL;
    return 0;
}
