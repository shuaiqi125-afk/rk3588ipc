#pragma once
#include<string>
#include "../common/codec_config.h"

//设置codec默认配置
void set_default_codec_config(CodecConfig& config);
//检查codec配置是否合法
bool validate_codec_config(const CodecConfig& config);
//从配置文件读取codec配置
bool load_codec_config(const std::string& path,CodecConfig& config);
//把codec配置保存到配置文件
bool save_codec_config(const std::string& path,const CodecConfig& config);