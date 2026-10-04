#include "fd_transfer.h"

#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

int send_frame_fd(int socket_fd, int dma_buf_fd,const FrameDesc& frame)
{
    //sendmsg使用的总消息结构
    struct msghdr msg{};
    //普通数据区域 用于发送FrameDesc
    struct iovec iov{};
    iov.iov_base = const_cast<FrameDesc*>(&frame);
    iov.iov_len = sizeof(FrameDesc);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    //控制消息区域  用于通过SCM_RIGHTS发送dma-buf fd
    char control[CMSG_SPACE(sizeof(int))]{};
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    //去的第一条控制消息头
    struct cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if(cmsg == nullptr)
    {
        std::cerr << "CMSG_FIRSTHDR failed" << std::endl;
    }
}