#include "codec_bsp_client.h"

#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

using namespace std;

//bsp控制socket路径,必须跟bsp中的路径完全一样
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

//从bsp读取一行文本；比如 width=1920\n
static bool receive_line(int fd,string& line)
{
    line.clear();
    while(true)
    {
        char ch;
        ssize_t received = recv(fd,&ch,1,0);
        if(received < 0)
        {
            if(errno == EINTR)
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
        //防止异常数据无限发送
        if(line.size() > 1024)
        {
            return false;
        }
    }
}
//将字符串安全转换成int
static bool string_to_int(const string& str,int& value)
{
    try
    {
        size_t pos = 0;
        int result = stoi(str,&pos);//将str能转换成整数部分给result，并pos记录几个字符能转换成整数
        if(pos != str.size())
        {
            return false;
        }
        value = result;
        return true;
    }
    catch(...)
    {
        return false;
    }
}
//解析bsp返回一行的配置
//width=1920 解析成 key=width value=1920
static bool parse_config_line(const string& line,CodecConfig& config)
{
    size_t equal_pos = line.find('=');
    if(equal_pos == string::npos)
    {
        return false;
    }
    string key = line.substr(0,equal_pos);
    string value=line.substr(equal_pos+1);
    if(key.empty() || value.empty())
    {
        return false;
    }
    if(key == "device")
    {
        config.device = value;
    }
    else if(key == "width")
    {
        if(!string_to_int(value,config.width))
        {
            return false;
        }
    }
    else if(key == "height")
    {
        if(!string_to_int(value,config.height))
        {
            return false;
        }
    }
    else if(key == "fps")
    {
        if(!string_to_int(value,config.fps))
        {
            return false;
        }
    }
    else if(key == "bitrate")
    {
        if(!string_to_int(value,config.bitrate))
        {
            return false;
        }
    }
    else
    {
        return false;
    }
    return true;
}
//对codec收到的配置做检查
static bool validate_received_config(const CodecConfig& config)
{
    if(config.device.empty())
    {
        cerr << "bsp返回的device为空" << endl;
        return false;
    }
    if(config.width<=0 || config.height<=0)
    {
        cerr << "bsp返回的分辨率非法" << endl;
        return false;
    }
    if(config.bitrate <= 0)
    {
        cerr << "bsp返回的bitrate非法" << endl;
        return false;
    }
    return true;
}
//从bsp获取codec配置
bool get_codec_config_from_bsp(CodecConfig& config)
{
    //1.创建unix domain socket
    int socket_fd = socket(AF_UNIX,SOCK_STREAM,0);
    if(socket_fd < 0)
    {
        cerr << "创建bsp客户端socket失败:" << strerror(errno) << endl;
        return false;
    }
    //2.准备服务器地址
    sockaddr_un address {};
    address.sun_family = AF_UNIX;//AF_UNIX表示本机进程间通信，不走普通网络ip
    strncpy(address.sun_path,BSP_SOCKET_PATH,sizeof(address.sun_path)-1);//复制socket地址
    //3.连接bsp
    if(connect(socket_fd,reinterpret_cast<sockaddr*>(&address),sizeof(address))<0)
    {
        cerr << "链接bsp失败:" << strerror(errno) << endl;
        close(socket_fd);
        return false;
    }
    cout << "已经连接BSP" << endl;
    //4.向bsp请求codec配置
    const string command = "GET_CODEC_CONFIG\n";
    if(!send_all(socket_fd,command))
    {
        cerr <<"向bsp发送配置请求失败" << endl;
        close(socket_fd);
        return false;
    }
    //5.先使用临时配置接收，防止只接受到一半修改正式config
    CodecConfig temp_config {};
    bool received_end = false;
    //6.不断读取bsp返回的配置
    while(true)
    {
        string line;
        if(!receive_line(socket_fd,line))
        {
            cerr << "读取bsp返回数据失败" << endl;
            close(socket_fd);
            return false;
        }
        //bsp发送end告诉我们配置已经全部发送完毕
        if(line == "END")
        {
            received_end = true;
            break;
        }
        //解析当前行配置
        if(!parse_config_line(line,temp_config))
        {
            cerr << "解析bsp配置失败:" << line << endl;
            close(socket_fd);
            return false;
        }
    }
    //7.请求已经完成，可以关闭连接
    close(socket_fd);
    //8.确认确实收到end
    if(!received_end)
    {
        cerr << "BSP配置数据不完整" << endl;
        return false;
    }
    //9.检查收到的配置
    if(!validate_received_config(temp_config))
    {
        return false;
    }
    //10.更新正式config
    config = temp_config;
    cout << "从BSP获取codec配置成功" << endl;
    return true;
}
