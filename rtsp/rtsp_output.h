#pragma once 

#include <string>

extern"C"
{
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
}

//rtsp输出运行上下文
struct RtspOutput
{
    AVFormatContext* output_fmt_ctx = nullptr;
    AVStream* video_stream = nullptr;
    bool header_written = false;
};
//初始化RTSP输出
int init_rtsp_output(RtspOutput& rtsp,const std::string& rtsp_url,
                     const AVCodecParameters* video_codecpar,AVRational input_time_base);
//把一个h264 packet 发送到rtsp
int write_rtsp_packet(RtspOutput& rtsp,AVPacket* packet,AVRational input_time_base);
//清理rtsp输出
void cleanup_rtsp_output(RtspOutput& rtsp);
