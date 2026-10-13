#include "frame_ipc.h"

#include <iostream>
#include <cstring>
#include <cerrno>

#include <sys/socket.h>


using namespace std;

//内部辅助函数
//SOCK_SEQPACKET会保留每一次send对应的消息边界 这里用于发送一个固定大小的结构体
static bool send_message(int socket_fd,const void* data,size_t size)
{
    if(socket_fd<0 || data == nullptr ||size == 0)
    {
        return false;
    }
    ssize_t sent = send(socket_fd,data,size,MSG_NOSIGNAL);
    if(sent<0 || static_cast<size_t>(sent) != size)
    {
        return false;
    }
    return true;
}
//接收一个固定大小的结构体
static bool receive_message(int socket_fd, void* data,size_t size)
{
    if(socket_fd<0 || data==nullptr || size==0)
    {
        return false;
    }
    ssize_t received = recv(socket_fd,data,size,0);
    if(received <=0 || static_cast<size_t>(received) != size)
    {
        return false;
    }
    return true;
}
//CODEC -> RTSPS :发送buffer pool基本信息
bool send_frame_pool_info(int socket_fd,const FramePoolInfo& info)
{
    return send_message(socket_fd,&info,sizeof(info));
}
//RTSPS接收buffer pool基本信息
bool receive_frame_pool_info(int socket_fd,FramePoolInfo& info)
{
    if(!receive_message(socket_fd,&info,sizeof(info)))
    {
        return false;
    }
    //检查消息类型
    if(info.type != static_cast<uint32_t>(FrameIpcMessageType::POOL_INFO))
    {
        return false;
    }
    return true;
}
//CODEC -> RTSPS 注册一块dma-buf
//普通数据: FrameBufferRegister   附加数据:dma_buf_fd 
//fd通过SCM_RIGHTS传递
bool send_frame_buffer_register(int socket_fd,const FrameBufferRegister& message,int dma_buf_fd)
{
    if(socket_fd<0 || dma_buf_fd<0)
    {
        return false;
    }
    //1.普通消息数据
    iovec iov{};
    iov.iov_base = const_cast<FrameBufferRegister*>(&message);
    iov.iov_len = sizeof(message);
    //2.准备控制消息空间
    //这里需要装一个int类型的fd
    char control[CMSG_SPACE(sizeof(int))]{};
    //3.准备msghdr
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    //4.取得第一块控制消息头
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if(cmsg == nullptr)
    {
        return false;
    }
    //5.告诉内核:这是socket级别的控制消息
    cmsg-> cmsg_level = SOL_SOCKET;
    //6.告诉内核:我么要传的是文件描述符
    cmsg-> cmsg_type = SCM_RIGHTS;
    //7.一个int类型fd需要控制消息长度
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    //8.把dma-buf fd 复制进控制消息
    memcpy(CMSG_DATA(cmsg),&dma_buf_fd,sizeof(int));
    //9.发送
    ssize_t sent = sendmsg(socket_fd,&msg,MSG_NOSIGNAL);
    if(sent<0 || static_cast<size_t>(sent) != sizeof(message))
    {
        return false;
    }
    return true;
}
//RTSPS 接收一块dma-buf注册信息和对应fd
bool receive_frame_buffer_register(int socket_fd,FrameBufferRegister& message, int& received_fd)
{
    received_fd = -1;
    if(socket_fd < 0)
    {
        return false;
    }
    //1.普通消息接收区域
    iovec iov{};
    iov.iov_base = &message;
    iov.iov_len = sizeof(message);
    //2.准备接收SCM_RIGHTS控制消息
    char control[CMSG_SPACE(sizeof(int))]{};
    //3.准备msghdr
    msghdr msg{};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    //4.接收
    ssize_t received = recvmsg(socket_fd,&msg,0);
    if(received <=0 || static_cast<size_t>(received) != sizeof(message))
    {
        return false;
    }
    //5.检查消息有没有被截断
    if(msg.msg_flags & (MSG_CTRUNC | MSG_TRUNC))
    {
        return false;
    }
    //6.检查普通消息类型
    if(message.type != static_cast<uint32_t>(FrameIpcMessageType::BUFFER_REGISTER))
    {
        return false;
    }
    //7.取得SCM_RIGHTS控制消息
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if(cmsg == nullptr)
    {
        return false;
    }
    //8.检查是不是fd
    if(cmsg->cmsg_level != SOL_SOCKET ||
       cmsg->cmsg_type != SCM_RIGHTS ||
       cmsg->cmsg_len <CMSG_LEN(sizeof(int))
    )
    {
        return false;
    }
    //9.把fd取出来
    memcpy(&received_fd,CMSG_DATA(cmsg),sizeof(int));
    if(received_fd < 0)
    {
        return false;
    }
    return true;
}
//CODEC -> RTSPS 告诉RTSPS：某一帧H264已经写进指定buffer了
bool send_frame_ready(int socket_fd,const FrameReadyMessage& message)
{
    return send_message(socket_fd,&message,sizeof(message));
}
//RTSPS 接收FRAME_READY
bool receive_frame_ready(int socket_fd, FrameReadyMessage& message)
{
    if(!receive_message(socket_fd,&message,sizeof(message)))
    {
        return false;
    }
    if(message.type != static_cast<uint32_t>(FrameIpcMessageType::FRAME_READY))
    {
        return false;
    }
    return true;
}
//RTSPS -> CODEC
//RTSPS使用完一帧以后，把buffer_id 和 sequence发回CODEC
bool send_frame_release(int socket_fd,const FrameReleaseMessage& message)
{
    return send_message(socket_fd,&message,sizeof(message));
}
//CODEC 接收RTSPS发回来的FRAME_RELEASE
bool receive_frame_release_message(int socket_fd,FrameReleaseMessage& message)
{
    if(!receive_message(socket_fd,&message,sizeof(message)))
    {
        return false;
    }
    if(message.type != static_cast<uint32_t>(FrameIpcMessageType::FRAME_RELEASE))
    {
        return false;
    }
    return true;
}

