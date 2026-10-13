#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct CodecConfig
{
    std::string device;

    int width = 0;
    int height = 0;
    int fps = 0;
    int bitrate = 0;

    //dma-buf pool一共有多少块buffer
    uint32_t frame_buffer_count = 0;

    //每块dma-buf的容量
    size_t frame_buffer_capacity = 0;
};