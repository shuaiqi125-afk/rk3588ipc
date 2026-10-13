#pragma once
#include "../common/frame_buffer_pool.h"
#include<cstdint>

extern"C"
{
#include<libavcodec/packet.h>
}

//连接RTSPS 成功返回socket fd ； 失败返回 -1
int connect_frame_receiver();
//CODEC连接RTSPS以后 把buffer pool里的所有dma-buf fd一次性注册给RTSPS
bool register_frame_buffer_pool(int socket_fd,FrameBufferPool& pool);
//发送一个真实H264 packet ，从pool里找一块free buffer使用
bool send_h264_packet(int socket_fd,FrameBufferPool& pool,const AVPacket* packet,uint64_t sequence,
                        uint32_t channel_id,uint32_t stream_id);
//处理RTSPS发回来的buffer释放消息 RTSPS使用完某块buffer以后，通知CODEC重新把它变成FREE
bool receive_frame_release(int socket_fd,FrameBufferPool& pool);
//关闭CODEC 和 RTSPS之间的连接
void close_frame_sender(int socket_fd);
