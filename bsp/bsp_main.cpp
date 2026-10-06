#include "bsp_config.h"
#include "bsp_control.h"

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

using namespace std;

//打印当前codec配置
static void print_codec_config(const CodecConfig& config)
{
    cout <<"======== CODEC CONFIG ========"<< endl;
    cout <<"device :"<<config.device << endl;
    cout <<"width  :"<<config.width << endl;
    cout <<"height :"<<config.height << endl;
    cout <<"fps    :"<<config.fps<< endl;
    cout <<"bitrate:"<<config.bitrate<<endl;
    cout<<"================================"<<endl;
}
//bsp
int main()
{
    cout << "bsp开始启动" <<endl;
    //1.创建codec配置结构体
    CodecConfig codec_config {};
    //2.先设置一套默认配置，即使配置文件不存在，bsp也有一套可以工作的参数
    set_default_codec_config(codec_config);
    //3.准备config目录
    error_code ec;
    filesystem::create_directories("config",ec);
    if(ec)
    {
        cerr << "创建config目录失败:" <<ec.message() << endl;
        return -1;
    }
    //4.codec配置文件路径
    const string codec_config_path = "config/codec.conf";
    //5.判断配置文件是否已经存在
    if(!filesystem::exists(codec_config_path))
    {
        //第一次运行配置文件不存在，使用默认配置codec.conf
        cout << "codec配置文件不存在" << endl;
        cout << "使用默认配置创建:" <<endl;
        if(!save_codec_config(codec_config_path,codec_config))
        {
            cerr << "创建默认CODEC配置文件失败"<<endl;
            return -1;
        }
    }
    else
    {
        //配置文件已经存在，从文件读取配置
        cout << "发现codec配置文件:" << codec_config_path<<endl;
        if(!load_codec_config(codec_config_path,codec_config))
        {
            cerr << "读取codec配置失败"<<endl;
            return -1;
        }
    }
    //6.再次确认当前配置合法
    if(!validate_codec_config(codec_config))
    {
        cerr << "当前codec配置非法"<<endl;
        return -1;
    }
    //7.打印bsp当前管理的codec配置
    print_codec_config(codec_config);
    cout << "bsp初始化完成" << endl;
    //启动BSP控制服务器，现在bsp不会自动退出而是长期等待连接
    if(run_bsp_control_server(codec_config)<0)
    {
        cerr << "bsp控制服务器退出" << endl;
        return -1;
    }

    return 0;
}
