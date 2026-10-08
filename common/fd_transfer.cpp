#include "fd_transfer.h"

#include <iostream>
#include <cstring>
#include <cerrno>

#include <sys/socket.h>


using namespace std;

//发送一个文件描述符
bool send_fd(int socket_fd,int fd_to_send)
{
    //准备1字节普通数据
    //SCM_RIGHTS属于辅助数据，为了稳定传递我们同时发送一个普通字节
    char dummy = 'F';
    iovec iov {};
    iov.iov_base = &dummy;//数据从那开始
    iov.iov_len =sizeof(dummy);//数据有多长
    //2.准备控制消息空间
    //CMSG_SPACE(sizeof(int))表示给“一个int类型的fd”准备足够的辅助消息空间
    char control[CMSG_SPACE(sizeof(int))] = {0};
    //3.准备sendmsg 需要的消息结构
    //msg 分成 普通数据区dummy = 'F' 和 控制数据区 msg_control
    msghdr msg {};
    //普通数据
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    //控制数据
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    //4.拿到控制消息头
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);//去msg的控制消息缓冲区里，把第一条控制消息头找出来
    if(!cmsg)
    {
        cerr << "创建控制消息失败" << endl;
        return false;
    }
    //5.说明:这是socket层的控制信息
    cmsg->cmsg_level = SOL_SOCKET;
    //6.说明：我要传的是“文件描述符”
    cmsg -> cmsg_type = SCM_RIGHTS;
    //7.控制消息实际长度
    cmsg -> cmsg_len = CMSG_LEN(sizeof(int));
    //8.把发送端自己的fd放进去，假设 fd_to_send =5
    memcpy(CMSG_DATA(cmsg),&fd_to_send,sizeof(int));
    //9.发送
    //因为cmsg_type = SCM_RIGHTS,
    //linux不会简单地把整数5交给另一边
    //linux会把"fd 5背后的内核对象"安装到接收进程自己的fd表里
    ssize_t sent = sendmsg(socket_fd,&msg,MSG_NOSIGNAL);
    if(sent < 0)
    {
        cerr << "发送fd失败:" << strerror(errno) << endl;
        return false;
    }
    cout << "发送fd成功,发送端fd=" << fd_to_send << endl;
    return true;
}

//接收一个文件描述符
int receive_fd(int socket_fd)
{
    //1.准备接收普通数据
    char dummy = 0;
    iovec iov {};
    iov.iov_base = &dummy;
    iov.iov_len = sizeof(dummy);
    //2.准备控制消息缓冲区
    char control[CMSG_SPACE(sizeof(int))] = {0};
    //3.准备recvmsg结构
    msghdr msg {};
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control;
    msg.msg_controllen = sizeof(control);
    //4.接收消息
    //如果发送端使用的是SCM_RIGHTS,linux会在这里给当前进程分配一个新的fd
    ssize_t received = recvmsg(socket_fd,&msg,0);
    if(received < 0)
    {
        cerr << "接收fd失败：" << strerror(errno) << endl;
        return -1;
    }
    if(received == 0)
    {
        cerr << "对端已经关闭连接" << endl;
        return -1;
    }
    //5.取控制消息
    cmsghdr* cmsg = CMSG_FIRSTHDR(&msg);
    if(!cmsg)
    {
        cerr << "没有收到控制消息" << endl;
        return -1;
    }
    //6.确认真的是SCM_RIGHTS
    if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS)
    {
        cerr << "收到的不是scm_rights消息" << endl;
        return -1;
    }
    //7.取出linux给接收进程创建新fd
    int received_fd = -1;
    memcpy(&received_fd,CMSG_DATA(cmsg),sizeof(int));
    cout << "接收fd成功,接收端fd=" << received_fd << endl;
    return received_fd; 
}