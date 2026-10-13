#include "codec_camera.h"
#include "codec_bsp_client.h"
#include "codec_frame_sender.h"

#include "../common/frame_buffer_pool.h"

#include <cstdio>
#include <csignal>
#include <cstdint>
#include <iostream>

using namespace std;

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
    //从bsp获取codec配置
    CodecConfig config {};
    if(!get_codec_config_from_bsp(config ))
    {
        cerr << "从BSP获取CODEC配置失败" << endl;
        return -1;
    }
    //3.根据bsp配置初始化codec
    CodecCamera camera{};
    if(init_codec_camera(camera,config) < 0)
    {
        cerr << "CodecCamera初始化失败"  << endl;
        return -1;
    }
    //4.准备dma-buf pool配置
    FrameBufferPoolConfig pool_config{};
    pool_config.buffer_count = config.frame_buffer_count;
    pool_config.buffer_capacity = config.frame_buffer_capacity;
    //5.创建并初始化dma-buf pool
    FrameBufferPool pool{};
    if(!init_frame_buffer_pool(pool,pool_config))
    {
        cerr << "初始化FrameBufferPool失败" << endl;
        cleanup_codec_camera(camera);
        return -1;
    }
    //6.连接rtsps
    int rtsp_socket_fd = connect_frame_receiver();
    if(rtsp_socket_fd<0)
    {
        cerr << "CODEC连接RTSPS失败" << endl;
        release_frame_buffer_pool(pool);
        cleanup_codec_camera(camera);
        return -1;
    }
    //7.把整个dma-buf pool 注册给rtsps
    if(!register_frame_buffer_pool(rtsp_socket_fd,pool))
    {
        cerr << "注册FrameBufferPool到RTSPS失败" << endl;
        close_frame_sender(rtsp_socket_fd);
        release_frame_buffer_pool(pool);
        cleanup_codec_camera(camera);
        return -1;
    }
    //8.创建H264输出packet
    AVPacket* output_packet = av_packet_alloc();
    if(!output_packet)
    {
        cerr << "创建H264输出packet失败" << endl;
        close_frame_sender(rtsp_socket_fd);
        release_frame_buffer_pool(pool);
        cleanup_codec_camera(camera);
        return -1;
    }
    cout << "CODEC开始运行" << endl;
    cout << "按ctrl+c停止" << endl;
    //9.H264 packet序列号
    uint64_t packet_count = 0;
    //10.codec主循环
    while(running)
    {
        //从摄像头读取MJPEG解码格式转换H264编码最终h264放进output_packet
        int ret = get_h264_packet(camera,output_packet);
        if(ret < 0)
        {
            cerr << "获取H264 packet失败" << endl;
            break;
        }
        //当前还没有产生H264 packet
        if(ret < 0)
        {
            continue;
        }
        //11.把真实H264packet 放进dma-buf pool 然后发送FRAME_READY给RTSPS
        if(!send_h264_packet(rtsp_socket_fd,pool,output_packet,packet_count,0,0))
        {
            cerr << "发送H264 packet 到RTSPS失败" << endl;
            av_packet_unref(output_packet);
            break;
        }
        //12.每300个packet打印一次
        if(packet_count %300 == 0)
        {
            cout << "已经处理" << packet_count<< "个H264 packet" << endl;
        }
        //13.当前packet处理完成AVPacket结构本身继续复用
        av_packet_unref(output_packet);
        //14.进入下一帧
        packet_count++;
    }
    //15.释放H264输出packet
    av_packet_free(&output_packet);
    //16.关闭CODEC -> RTSPS连接
    close_frame_sender(rtsp_socket_fd);
    //17.释放整个dma-buf pool
    release_frame_buffer_pool(pool);
    //18.释放摄像头和编解码资源
    cleanup_codec_camera(camera);
    cout << "CODEC已经退出" <<endl;
    return 0;
}