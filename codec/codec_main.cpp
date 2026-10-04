#include "codec_camera.h"

#include<cstdio>
#include<csignal>
#include<cstdint>
#include<iostream>

//程序运行标志 ctrl+c时会变成0 让while循环正常退出
static volatile sig_atomic_t running = 1;

//ctrl+c信号函数处理
static void signal_handler(int signal)
{
    if(signal == SIGINT)
    {
        running = 0;
    }
}

//codec主程序
int main()
{
    //1.注册ctrl+c处理函数
    signal(SIGINT,signal_handler);
    //2.设置第一版codec参数（先在main中设置，以后写到bsp中）
    CodecCameraConfig config{};
    config.device = "/dev/video0";
    config.width = 1920;
    config.height = 1080;
    config.fps = 30;
    config.bitrate = 4000000;
    //3.创建CodecCamera运行山下文
    CodecCamera camera{};
    //4.初始化摄像头链
    if(init_codec_camera(camera,config) < 0)
    {
        cerr << "CodecCamera初始化失败"  << endl;
        return -1;
    }
    //5.创建H264输出packet
    AVPacket* output_packet = av_packet_alloc();
    if(!output_packet)
    {
        cerr << "创建h264输出packet失败" << endl;
        cleanup_codec_camera(camera);
        return -1;
    }
    //6.创建h264输出文件
    FILE* output_file = fopen("output.h264","wb");
    if(!output_file)
    {
        cerr << "创建output.h264文件失败" << endl;
        av_packet_free(&output_packet);
        cleanup_codec_camera(camera);
        return -1;
    }
    cout << "codec开始运行" <<endl;
    cout << "按ctrl+c停止" << endl;
    //7.统计已经输出多少个h264 packet
    uint64_t packet_count = 0;
    //8.codec主循环
    while(running)
    {
        int ret = get_h264_packet(camera,output_packet);
        if(ret< 0)
        {
            cerr << "获取h264packet 失败" << endl;
            break;
        }
        if(ret == 0)
        {
            continue;
        }
        //9.得到h264 packet
        size_t written = fwrite(output_packet->data,1,output_packet->size,output_file);
        if(written != static_cast<size_t>(output_packet->size))
        {
            cerr << "写入output.h264失败" << endl;
            break;
        }
        packet_count++;
        //10.每30个packet打印一下状态
        if(packet_count % 30 == 0)
        {
            cout << "已经编码" << packet_count<<"个h264 packet" << endl;
        }
        //11.释放
        av_packet_unref(output_packet);
    }
    //12.刷新并关闭h264文件
    fflush(output_file);
    fclose(output_file);
    output_file = nullptr;
    //13.释放h264输出packet
    av_packet_free(&output_packet);
    //14.释放摄像头codec全部资源
    cleanup_codec_camera(camera);
    cout << "codec已经退出" << endl;
    return 0;
}
