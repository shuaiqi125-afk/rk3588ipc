#pragma once 
#include<cstdint>

//编码类型
enum class FrameCodec : uint32_t
{
    UNKNOWN = 0,
    H264 = 1,
    H265 = 2
};

//帧标志位
enum FrameFlags : uint32_t
{
    FRAME_FLAG_NONE = 0,
    //是否为关键帧
    FRAME_FLAG_KEY = 1U << 0
};

//一帧媒体数据的描述信息，里边不保存h264数据，只负责描述 这块dma-buf 里面装的是什么东西
struct FrameDesc
{
    //编码格式 比如：h264
    uint32_t codec = 0;
    //当前帧实际有效数据大小 如：packet->size = 82345
    uint32_t size = 0;
    //时间戳
    int64_t pts = 0;
    //序列号,第几帧
    uint64_t sequence = 0;
    //标志位 如：FRAME_FLAG_KEY
    uint32_t flags = FRAME_FLAG_NONE;
};
