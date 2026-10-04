#pragma once
#include <string>

extern "C"
{
#include<libavformat/avformat.h>
#include<libavcodec/avcodec.h>
#include<libswscale/swscale.h>
}
using namespace std;

//摄像头及编码参数
struct CodecCameraConfig
{
    string device;
    int width;
    int height;
    int fps;
    int bitrate;

};
//摄像头媒体处理上下文
struct CodecCamera
{
    //摄像头输入上下文
    AVFormatContext* input_fmt_ctx = nullptr;
    //摄像头视频流编号
    int video_stream_index = -1;
    //mjpeg
    AVCodecContext* decoder_ctx = nullptr;
    //图像格式转换
    SwsContext* sws_ctx = nullptr;
    //h264编码器
    AVCodecContext* encoder_ctx = nullptr;
    //摄像头读取到的 mjpeg packet
    AVPacket* input_packet = nullptr;
    //mjpeg解码后的原始FRAME
    AVFrame* decoded_frame = nullptr;
    //转换成编码器需要格式后的Frame
    AVFrame* encoder_frame = nullptr;
    //下一帧送给H264编码器的pts
    int64_t next_pts = 0;
};

int init_codec_camera(CodecCamera& camera,const CodecCameraConfig& config);
int get_h264_packet(CodecCamera& camera,AVPacket* output_packet);
void cleanup_codec_camera(CodecCamera& camera);

