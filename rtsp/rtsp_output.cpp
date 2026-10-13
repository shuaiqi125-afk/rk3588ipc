#include "rtsp_output.h"
#include <iostream>
extern"C"
{
    #include <libavutil/error.h>
}

using namespace std;

//ffmpeg错误码转字符串
static string ffmpeg_error_string(int error_code)
{
    char error_buffer[256] = {0};
    av_strerror(error_code,error_buffer,sizeof(error_buffer));
    return string(error_buffer);
}
//初始化rtsp输出
int init_rtsp_output(RtspOutput& rtsp,const string& rtsp_url,const AVCodecParameters* video_codecpar,
                        AVRational input_time_base)
{
    //1.创建rtsp输出上下文
    int ret = avformat_alloc_output_context2(&rtsp.output_fmt_ctx,nullptr,"rtsp",rtsp_url.c_str());
    if(ret < 0 || !rtsp.output_fmt_ctx)
    {
        cerr << "创建rtsp输出上下文失败:" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    //减少缓存延迟
    rtsp.output_fmt_ctx -> flags |= AVFMT_FLAG_FLUSH_PACKETS;
    rtsp.output_fmt_ctx -> max_delay = 100000;
    //2.创建视频输出流
    rtsp.video_stream = avformat_new_stream(rtsp.output_fmt_ctx,nullptr);//表示在rtsp.output_fmt_ctx中创建一条新的媒体流，并将这条媒体流保存到rtsp.video_stream中
    if(!rtsp.video_stream)
    {
        cerr << "创建rtsp视频输出流失败" << endl;
        return -1;
    }
    //3.复制h264编码参数,输入output.h264里边的h264参数，输出rtsp视频流
    ret = avcodec_parameters_copy(rtsp.video_stream->codecpar,video_codecpar);//参数二里的内容复制到参数一
    if(ret < 0)
    {
        cerr << "复制h264编码参数失败：" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    //避免容器使用不合适的codec tag
    rtsp.video_stream -> codecpar -> codec_tag =0;//复制读取的文件视频时候要写
    //4.设置时间基
    rtsp.video_stream -> time_base = input_time_base;
    //5.设置rtsp发布使用tcp
    AVDictionary* options = nullptr;
    av_dict_set(&options,"rtsp_transport","tcp",0);
    //6.连接mediactx,并写rtsp头
    ret = avformat_write_header(rtsp.output_fmt_ctx,&options);
    av_dict_free(&options);
    if(ret < 0)
    {
        cerr << "连接rtsp服务器失败：" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    rtsp.header_written = true;
    cout << "rtsp输出初始化成功" << endl;
    return 0;
}
//把h264 packet发送给mediamtx
int write_rtsp_packet(RtspOutput& rtsp,AVPacket* packet,AVRational input_time_base)
{
    if(!rtsp.output_fmt_ctx || !rtsp.video_stream || !packet)
    {
        return -1;
    }
    //1.把packet时间戳从输入时间基转换成rtsp输出流时间基
    av_packet_rescale_ts(packet,input_time_base,rtsp.video_stream->time_base);
    //2.指定这个packet属于rtsp视频流
    packet->stream_index = rtsp.video_stream -> index;
    //3.发送
    int ret = av_interleaved_write_frame(rtsp.output_fmt_ctx,packet);
    if(ret < 0)
    {
        cerr << "rtsp发送h264 packet失败:" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    return 0;
}

//清理rtsp输出
void cleanup_rtsp_output(RtspOutput& rtsp)
{
    if(rtsp.output_fmt_ctx)
    {
        if(rtsp.header_written)
        {
            av_write_trailer(rtsp.output_fmt_ctx);
        }
        avformat_free_context(rtsp.output_fmt_ctx);
        rtsp.output_fmt_ctx = nullptr;
    }
    rtsp.video_stream = nullptr;
    rtsp.header_written = false;
}