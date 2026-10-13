#include "rtsp_frame_receiver.h"

#include <iostream>
#include <cstring>
#include <cerrno>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

using namespace std;

//unix Domain Socket在文件系统中的地址
static const char* FRAME_SOCKET_PATH = "/tmp/rk3588ipc_codec_rtsp.sock";
//创建RTSPS的Unix Domain Socket服务器
int create_frame_receiver_server()
{
    //1.创建socket
    int server_fd = socket(AF_UNIX,SOCK_SEQPACKET,0);
    if(server_fd < 0)
    {
        cerr << "创建RTSPS socket失败:" << strerror(errno) << endl;
        return -1;
    }
    //2.删除可能残留的旧socket文件
    unlink(FRAME_SOCKET_PATH);
    //3.准备Unix Domain Socket地址
    sockaddr_un address{};
    address.sun_family = AF_UNIX;//family表示这是什么类型的地址；AF_UNIX表示这个地址是 Unix Domain Socket 地址
    strncpy(address.sun_path,FRAME_SOCKET_PATH,sizeof(address.sun_path)-1);//参数二复制到参数一
    //4.给server_fd绑定地址
    if(bind(server_fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))< 0)//fd绑定到地址address
    {
        cerr << "bind失败:" << strerror(errno) << endl;
        close(server_fd);
        return -1;
    }
    //5.开始监听连接
    if(listen(server_fd,1)<0)
    {
        cerr << "listen失败:" << strerror(errno) << endl;
        close(server_fd);
        unlink(FRAME_SOCKET_PATH);
        return -1;
    }
    cout << "RTSPS等待CODEC连接:" << FRAME_SOCKET_PATH << endl;
    return server_fd;
}
//等待codec连接
int accept_codec_connection(int server_fd)
{
    int client_fd = accept(server_fd,nullptr,nullptr);//程序停在这里等待，直到codec.connect真正链接过来,accept返回一个新的client_fd
    if(client_fd < 0)
    {
        cerr << "accept失败:" << strerror(errno) << endl;
        return -1;
    }
    cout << "CODEC已连接RTSPS" << endl;
    return client_fd;
}
//关闭服务器
void close_frame_receiver_server(int server_fd)
{
    if(server_fd >= 0)
    {
        close(server_fd);
    }
    unlink(FRAME_SOCKET_PATH);

}