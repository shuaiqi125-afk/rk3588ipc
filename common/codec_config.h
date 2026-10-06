#pragma once 
#include<string>

//codec模块公共配置，数据由bsp负责管理
struct CodecConfig
{
    std::string device;
    int width;
    int height;
    int fps;
    int bitrate;
};