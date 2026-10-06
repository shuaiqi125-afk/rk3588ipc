#include "bsp_control.h"

#include<cerrno>
#include<cstring>
#include<iostream>
#include<sstream>
#include<string>
#include<sys/socket.h>
#include<sys/un.h>
#include<unistd.h>

using namespace std;

//BSP Unix Domain Socket路径
static const char* BSP_SOCKET_PATH = "/tmp/rk3588ipc_bsp.sock";
//确保一整段数据全部发送出去，SCOK_STREAM不能保证一次send()将数据全部发送出去
static bool send_all(int fd,const string& data)
{
    size_t total_sent = 0;//已经发送字节数
    while(total_sent < data.size())
    {
        ssize_t sent = send(fd,data.data()+total_sent,data.size()-total_sent,0);
        if(sent <0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            return false;
        }
        if(sent == 0)
        {
            return false;
        }
        total_sent += static_cast<size_t>(sent);
    }
    return true;
}
//从客户端读取一行命令
static bool receive_line(int fd,string& line)
{
    line.clear();//清空内容，但不删除变量
    while(true)
    {
        char ch;
        ssize_t received = recv(fd,&ch,1,0);
        if(received < 0)
        {
            if(errno == EINTR)//被信号中断
            {
                continue;
            }
            return false;
        }
        if(received == 0)
        {
            return false;
        }
        if(ch == '\n')
        {
            return true;
        }
        line += ch;
        //防止数据过大
        if(line.size() > 1024)
        {
            return false;
        }
    }
}

//把CodecConfig转换成要发送的文本
static string serialize_codec_config(const CodecConfig& config)
{
    ostringstream stream;
    stream << "device=" << config.device << '\n';
    stream << "width=" << config.width << '\n';
    stream << "height=" << config.height << '\n';
    stream << "fps=" << config.fps << '\n';
    stream << "bitrate=" << config.bitrate << '\n';
    stream << "END" << '\n';
    return stream.str();
}

//bsp控制服务器
int run_bsp_control_server(const CodecConfig& codec_config)
{
    //1.创建Unix Domain Socket
    int server_fd = socket(AF_UNIX,SOCK_STREAM,0);
    if(server_fd < 0)
    {
        cerr  << "创建BSP控制Socket失败:" << strerror(errno)<< endl;
        return -1;
    }
    //2.删除可能残留的旧Socket文件
    unlink(BSP_SOCKET_PATH);
    //3.准备unix domain socket 地址
    sockaddr_un address {};
    address.sun_family = AF_UNIX;
    strncpy(address.sun_path,BSP_SOCKET_PATH,sizeof(address.sun_path)-1);
    //4.把socket绑定到文件路径
    if(bind(server_fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0)
    {
        cerr << "绑定bsp控制socket失败:" << strerror(errno) << endl;
        close(server_fd);
        return -1;
    }
    //5.开始监听
    if(listen(server_fd,5)<0)
    {
        cerr << "监听bsp控制socket失败" << strerror(errno)<<endl;
        close(server_fd);
        unlink(BSP_SOCKET_PATH);
        return -1;
    }
    cout << "bsp控制服务器已经启动" << endl;
    cout << "socket:" << BSP_SOCKET_PATH << endl;
    //6.bsp长期运行
    while(true)
    {
        cout << "等待模块链接..." << endl;
        //等待一个客户端连接
        int client_fd = accept(server_fd,nullptr,nullptr);
        if(client_fd < 0)
        {
            if(errno == EINTR)
            {
                continue;
            }
            cerr << "accept失败:" << strerror(errno) << endl;
            break;
        }
        cout << "收到模块连接" << endl;
        //读取客户端命令
        string command;
        if(!receive_line(client_fd,command))
        {
            cerr << "读取模块命令失败" << endl;
            close(client_fd);
            continue;
        }
        cout << "收到命令:" << command << endl;
        //codec请求当前配置
        if(command == "GET_CODEC_CONFIG")
        {
            string response = serialize_codec_config(codec_config);
            if(!send_all(client_fd,response))
            {
                cerr << "发送codec配置失败" << endl;
            }
            else
            {
                cout << "codec配置发送完成" << endl;
            }
        }
        else
        {
            //当前第一版不认识其他命令
            send_all(client_fd,"ERROR=UNKNOWN_COMMAND\nEND\n");
        }
        //当前请求处理完成
        close(client_fd);
    }
    //7.服务器异常退出时清理
    close(server_fd);
    unlink(BSP_SOCKET_PATH);
    return -1;
}
