#pragma once
#include "frame_desc.h"
#include<cstdint>

//数据面消息类型
enum class FrameIpcMessageType : uint32_t
{
    UNKNOWN = 0,
    //CODEC告诉RTSPS:buffer pool一共有多少快buffer,每块多大
    POOL_INFO = 1,
    //CODEC把某一块dma-buf fd 注册给RTSPS
    BUFFER_REGISTER = 2,
    //CODEC通知RTSPS：某一帧已经准备好了
    FRAME_READY = 3,
    //RTSPS通知CODEC 这一帧已经使用完了 可以重新使用buffer
    FRAME_RELEASE = 4
};
//1.buffer pool 基本信息
struct FramePoolInfo
{
    uint32_t type = static_cast<uint32_t>(FrameIpcMessageType::POOL_INFO);
    //一共多少块dma-buf
    uint32_t buffer_count = 0;
    //每块dma-buf容量
    uint64_t buffer_capacity = 0;
};
//2.单块dma-buf注册信息 真正的fd通过SCM_RIGHTS一起发送
struct FrameBufferRegister
{
    uint32_t type = static_cast<uint32_t>(FrameIpcMessageType::BUFFER_REGISTER);
    //这是pool里的哪一块buffer 
    uint32_t buffer_id = 0;
};
//3.一帧H264准备完成 这里不再携带dma-buf fd，RTSPS根据buffer_id找到启动时已经mmap好的buffer
struct FrameReadyMessage
{
    uint32_t type = static_cast<uint32_t>(FrameIpcMessageType::FRAME_READY);
    FrameDesc frame;
};
//4.RTSPS使用完成
struct FrameReleaseMessage
{
    uint32_t type = static_cast<uint32_t>(FrameIpcMessageType::FRAME_RELEASE);
    uint32_t buffer_id = 0;
    uint64_t sequence = 0;
};
//正式buffer pool协议接口
//CODEC -> RTSPS 发送整个buffer pool的基本信息
bool send_frame_pool_info(int socket_fd,const FramePoolInfo& info);
//RTSPS接收buffer pool基本信息
bool receive_frame_pool_info(int socket_fd,FramePoolInfo& info);
//CODEC -> RTSPS 
//注册一块dma-buf,FrameBufferRegister作为普通数据dma_buf_fd通过SCM_RIGHTS发送
bool send_frame_buffer_register(int socket_fd,const FrameBufferRegister& message,int dma_buf_fd);
//RTSPS接收一块dma-buf注册
bool receive_frame_buffer_register(int socket_fd,FrameBufferRegister& message,int& received_fd);
//CODEC -> RTSPS 通知一帧已经准备完成
bool send_frame_ready(int socket_fd,const FrameReadyMessage& message);
//RTSPS接收FRAME_READY
bool receive_frame_ready(int socket_fd,FrameReadyMessage& message);
//RTSPS -> CODEC 告诉CODEC某块buffer已经使用完成
bool send_frame_release(int socket_fd,const  FrameReleaseMessage& message);
//CODEC接收RTSPS的释放消息
bool receive_frame_release_message(int socket_fd,FrameReleaseMessage& message);

