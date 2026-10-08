#include "frame_ipc.h"

#include <iostream>
#include <cstring>
#include <cerrno>

#include <sys/socket.h>


using namespace std;

//发送 framedesc + dma-buf fd
bool send_frame(int socket_fd,const FrameDesc& desc,int dma_buf_fd)
{
    //1.普通数据区
    //这次不发送dummy = 'F',直接把FrameDesc作为普通数据发送
    iovec iov{};
    iov.iov_base = const_cast<FrameDesc*>(&desc);
    iov.iov_len = sizeof(FrameDesc);
    //2.控制消息区
    //用于传dma-buf fd
    char control[CMSG_SPACE(sizeof(int))] = {0};
    //3.准备msghdr
    msghdr msg {};
    //普通数据:dma-buf fd
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    //控制数据:dma-buf fd
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    //4.取得控制消息头
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if(!cmsg)
    {
        cerr << "创建控制消息失败" << endl;
        return false;
    }
    //5.设置SCM_RIGHTS
    cmsg -> cmsg_level = SOL_SOCKET;
    cmsg -> cmsg_type = SCM_RIGHTS;
    cmsg -> cmsg_len = CMSG_LEN(sizeof(int));
    //6.把dma-buf fd放进控制消息
    memcpy(CMSG_DATA(cmsg),&dma_buf_fd,sizeof(int));
    //7.发送：普通数据:FrameDesc 辅助数据:dma-buf fd
    ssize_t sent = sendmsg(socket_fd,&msg,MSG_NOSIGNAL);
    if (sent < 0)
    {
        cerr << "发送frame失败:" << strerror(errno) << endl;
        return false;
    }
    if(static_cast<size_t>(sent) != sizeof(FrameDesc))
    {
        cerr <<"FrameDesc发送不完整" << endl;
        return false;
    }
    return true;
}

//接收FrameDesc + dma-buf fd
bool receive_frame(int socket_fd,FrameDesc& desc,int& received_fd)
{
    received_fd = -1;
    //1.普通数据接收区
    //recvmsg以后FrameDesc会直接写入desc
    iovec iov {};
    iov.iov_base = &desc;
    iov.iov_len = sizeof(FrameDesc);
    //2.控制消息接收区
    char control[CMSG_SPACE(sizeof(int))] = {0};
    //3.准备msghdr
    msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    //4.接收
    ssize_t received = recvmsg(socket_fd,&msg,0);
    if(received < 0)
    {
        cerr << "接收frame失败" << strerror(errno) << endl;
        return false;
    }
    if(received == 0)
    {
        cerr << "对端已经关闭连接" << endl;
        return false;
    }
    if(static_cast<size_t>(received) != sizeof(FrameDesc))
    {
        cerr << "FrameDesc接收不完整" << endl;
        return false;
    }
    //5.检查控制消息有没有被截断
    if(msg.msg_flags & MSG_CTRUNC)
    {
        cerr << "SCM_RIGHTS控制消息被截断" << endl;
        return false;
    }
    //6.取得控制消息
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if(!cmsg)
    {
        cerr << "没有收到dma-buf fd" << endl;
        return false;
    }
    //7.确认是SCM_RIGHTS
    if(cmsg -> cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
    {
        cerr << "收到的控制消息不是SCM_RIGHTS" << endl;
        return false;
    }
    //8.取出接收端自己的fd
    memcpy(&received_fd,CMSG_DATA(cmsg),sizeof(int));
    return true;
}