#pragma once
#include<cstdint>

//媒体帧类型，既包含编码后的码流类型，也包含可能提供给svp等模块使用的原始图像类型
enum class CodecType : uint32_t 
{
    UNKNOWN = 0,
    H264,
    H265,
    MJPEG,
    NV12,
    YUYV422
};

//frame标志位，每一个bit表示一种帧属性，后续需要新增属性时，可以继续增加新的bit。
enum FrameFlags : uint32_t
{
    FRAME_FLAG_NONE = 0,
    //当前帧是关键帧
    FRAME_FLAG_KEY = 1U<<0,
    //当前流已经结束
    FRAME_FLAG_EOS = 1U << 1,
};

//跨模块统一媒体帧描述结构
struct FrameDesc
{
    //FrameDesc协议版本
    uint32_t version;
    //媒体通道编号
    uint32_t channel_id;
    //当前通道码流编号，0=主码流,1=子码流
    uint32_t stream_id;
    //当前Frame的数据类型:H264 H265 MJPEG NV12 YUYV422
    CodecType codec;
    //图像宽高
    uint32_t width;
    uint32_t height;
    //帧序号
    uint64_t sequence;
    //显示时间戳，整个3588ipc统一使用us
    int64_t pts_us;
    //dma-buf中当前frame的有效长度
    uint64_t size;
    //frame状态标志: FRAME_FLAG_KEY FRAME_FLAG_EOS
    uint32_t flags;
};

//FrameDesc当前协议版本
constexpr uint32_t FRAME_DESC_VERSION = 1;
//判断当前frame是不是关键帧
inline bool is_key_frame(const FrameDesc& frame)
{
    return(frame.flags & FRAME_FLAG_KEY) !=0;
}
//判断当前frame是不是流结束标志
inline bool is_eos_frame(const FrameDesc& frame)
{
    return(frame.flags & FRAME_FLAG_EOS) != 0;
}
