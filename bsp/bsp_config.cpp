#include "bsp_config.h"
#include<fstream>
#include<iostream>
#include<string>

using namespace std;

//去掉字符串左右两边的空格，tab，换行
// " width " -> "width"
static string trim(const string& str)
{
    //找到第一个不是空白字符串的位置
    size_t first = str.find_first_not_of(" \t\r\n");
    //整个字符串都是空白
    if(first == string::npos)
    {
        return "";
    }
    //找到最后一个不是空白字符串的位置
    size_t last = str.find_last_not_of(" \t\r\n");
    return str.substr(first,last-first+1);
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
//设置codec默认配置
void set_default_codec_config(CodecConfig& config)
{
    config.device = "/dev/video0";
    config.width = 1920;
    config.height = 1080;
    config.fps = 30;
    config.bitrate = 4000000;
}
//检查codec配置是否合法
bool validate_codec_config(const CodecConfig& config)
{
    //1.摄像头设备不能为空
    if(config.device.empty())
    {
        cerr << "codec配置错误:device不能为空"<<endl;
        return false;
    }
    if(config.width<=0)
    {
        cerr << "codec配置错误:width必须大于零" << endl;
        return false;
    }
    if(config.height <= 0)
    {
        cerr << "codec配置错误:height必须大于零" << endl;
        return false;
    }
    //yuv420p通常要求宽高为偶数
    if(config.width%2 !=0 || config.height%2 !=0)
    {
        cerr << "codec配置错误:" << "width和height必须为偶数"<< endl;
        return false;
    }
    if(config.fps <= 0)
    {
        cerr << "codec配置错误:fps必须大于零" << endl;
        return false;
    }
    if(config.bitrate <= 0)
    {
        cerr << "codec配置错误:bitrate必须大于零" << endl;
        return false;
    }
    return true;
}
//从配置文件读取codec配置
bool load_codec_config(const string& path,CodecConfig& config)
{
    //1.打开配置文件
    ifstream file(path);
    if(!file.is_open())
    {
        cerr << "打开codec配置文件失败:" << path << endl;
        return false;
    }
    //不直接修改原来的config，先复制一份，如果整个文件读取成功，最后赋给config
    CodecConfig temp_config = config;
    string line;
    int line_number = 0;
    //2.一行一行读取配置文件
    while(getline(file,line))
    {
        line_number++;
        //去掉左右空格
        line = trim(line);
        //空行直接跳过
        if(line.empty())
        {
            continue;
        }
        //#开头表示注释
        if(line[0] == '#')
        {
            continue;
        }
        size_t equal_pos = line.find('=');
        if(equal_pos == string::npos)
        {
            cerr << "配置文件第" << line_number <<"行格式错误" <<line<<endl;
            return false;
        }
        //3.取出key和value
        string key = trim(line.substr(0,equal_pos));
        string value = trim(line.substr(equal_pos+1));
        if(key.empty() || value.empty())
        {
            cerr << "配置文件第" << line_number << "行为空" << endl;
            return false;
        }
        //4.根据key设置对应配置
        if(key=="device")
        {
            temp_config.device = value;
        }
        else if(key == "width")
        {
            int number = 0;
            if(!string_to_int(value,number))
            {
                cerr << "配置文件第" <<line_number << "行width不是有效整数" << endl;
                return false;
            }
            temp_config.width = number;
        }
        else if(key == "height")
        {
            int number = 0;
            if(!string_to_int(value,number))
            {
                cerr << "配置文件第" <<line_number << "行height不是有效整数" << endl;
                return false;
            }
            temp_config.height = number;
        }
        else if(key == "fps")
        {
            int number = 0;
            if(!string_to_int(value,number))
            {
                cerr << "配置文件第" <<line_number << "行fps不是有效整数" << endl;
                return false;
            }
            temp_config.fps = number;
        }
        else if(key == "bitrate")
        {
            int number = 0;
            if(!string_to_int(value,number))
            {
                cerr << "配置文件第" <<line_number << "行bitrate不是有效整数" << endl;
                return false;
            }
            temp_config.bitrate = number;
        }
        else
        {
            cerr << "位置codec配置项" << key << endl;
            return false;
        }
    }
    //5.检查配置是否合法
    if(!validate_codec_config(temp_config))
    {
        return false;
    }
    //6.合法之后修改config
    config = temp_config;
    cout << "codec配置读取成功:" << path << endl;
    return true;
}
//保存codec配置
bool save_codec_config(const string& path,const CodecConfig& config)
{
    //1.保存之前检查配置
    if(!validate_codec_config(config))
    {
        return false;
    }
    //2.打开配置文件
    ofstream file(path,ios::trunc);//打开并清空原文件
    if(!file.is_open())
    {
        cerr << "创建codec配置文件失败" << path << endl;
        return false;
    }
    //3.写入配置
    file << "# CODEC configuration" << '\n';
    file << "device=" << config.device <<'\n';
    file << "width=" << config.width << '\n';
    file << "height=" << config.height << '\n';
    file << "fps=" << config.fps << '\n';
    file << "bitrate=" << config.bitrate << '\n';
    //4.确认写入没有发生故障
    if(!file.good())
    {
        cerr << "写入codec配置文件失败:" << path << endl;
        return false;
    }
    cout <<"codec配置文件成功:" << path << endl;
    return true;
}