#pragma once
#include "../common/codec_config.h"

//启动bsp控制服务器
//当前第一版负责等待codec链接
//接收GET_CODEC_CONFIG
//把当前codecconfig发送给codec
//正常情况一直运行，返回-1表示服务器发生错误
int run_bsp_control_server(const CodecConfig& codec_config);