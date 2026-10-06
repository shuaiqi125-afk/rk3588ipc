#pragma once 
#include "../common/codec_config.h"

//从bsp获取当前codec配置
//链接bsp -> 发送GET_CODEC_CONFIG -> 接收配置文本 ->解析成CodecConfig
bool get_codec_config_from_bsp(CodecConfig& config);