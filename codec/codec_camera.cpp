#include "codec_camera.h"

#include<iostream>
#include<string>

extern "C"
{
#include <libavdevice/avdevice.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}

static string ffmpeg_error_string(int errnum)
{
    char errbuf[AV_ERROR_MAX_STRING_SIZE] {};
    av_strerror(errnum,errbuf,sizeof(errbuf));
    return string(errbuf);
}

static int open_camera(CodecCamera& camera,const CodecCameraConfig& config)
{
    //1.注册ffmpeg设备
    avdevice_register_all();
    //2.找到v4l2输入格式
    const AVInputFormat* input_format = av_find_input_format("v4l2");
    if(!input_format)
    {
        cerr << "找不到v4l2输入格式" << endl;
        return -1;
    }
    //3.准备摄像头参数
    AVDictionary* options = nullptr;
    //4.设置参数
    string video_size = to_string(config.width)+"x"+to_string(config.height);
    av_dict_set(&options,"video_size",video_size.c_str(),0);
    string framerate = to_string(config.fps);
    av_dict_set(&options,"framerate",framerate.c_str(),0);
    av_dict_set(&options,"input_format","mjpeg",0);
    //5.打开摄像头
    int ret = avformat_open_input(&camera.input_fmt_ctx,config.device.c_str(),input_format,&options);
    av_dict_free(&options);
    if(ret < 0)
    {
        cerr << "打开摄像头失败:" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    //6.获取媒体流信息
    ret = avformat_find_stream_info(camera.input_fmt_ctx,nullptr);
    if(ret < 0)
    {
        cerr << "获取摄像头流信息失败:" <<ffmpeg_error_string(ret) << endl;
        return -1;
    }
    //7.寻找视频流
    camera.video_stream_index = -1;
    for(unsigned int i =0;i<camera.input_fmt_ctx->nb_streams;i++)
    {
        AVStream* stream = camera.input_fmt_ctx-> streams[i];
        if(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            camera.video_stream_index = static_cast<int>(i);
            break;
        }
    }
    //10.确认成功找到视频流
    if(camera.video_stream_index < 0)
    {
        cerr << "摄像头中没有找到视频流" << endl;
        return -1;
    }
    cout << "摄像头打开成功:" << config.device << endl;
    cout << "分辨率:" << video_size << endl;
    cout << "帧率" << config.fps << endl;
    return 0;
}

static int init_decoder(CodecCamera& camera)
{
    //1.取得刚才找到的视频流
    AVStream* video_stream = camera.input_fmt_ctx->streams[camera.video_stream_index];
    //2.根据摄像头实际输出的编码格式寻找对应解码器
    const AVCodec* decoder = avcodec_find_decoder(video_stream-> codecpar->codec_id);
    if(!decoder)
    {
        cerr << "找不到视频解码器" << endl;
        return -1;
    }
    cout << "摄像头输入编码格式:" << avcodec_get_name(video_stream -> codecpar -> codec_id) << endl;
    //3.为解码器创建AVCodecContext
    camera.decoder_ctx = avcodec_alloc_context3(decoder);
    if(!camera.decoder_ctx)
    {
        cerr << "创建视频解码器上下文失败" << endl;
        return -1;
    }
    //4.把AVStream中的摄像头编码参数复制到decoder_ctx
    int ret = avcodec_parameters_to_context(camera.decoder_ctx,video_stream -> codecpar);
    if(ret < 0)
    {
        cerr << "复制视频解码器参数失败" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    //5.真正打开解码器
    ret = avcodec_open2(camera.decoder_ctx,decoder,nullptr);
    if(ret < 0)
    {
        cerr << "打开视频解码器失败" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    cout << "视频解码器初始化成功" << decoder->name <<endl;
    return 0;
}

static int init_encoder(CodecCamera& camera,const CodecCameraConfig& config)
{
    //1.寻找h264编码器
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H264);
    if(!encoder)
    {
        cerr << "找不到h264编码器" << endl;
        return -1;
    }
    //2.创建H264编码器上下文
    camera.encoder_ctx = avcodec_alloc_context3(encoder);
    if(!camera.encoder_ctx)
    {
        cerr << "创建H264编码器上下文失败" << endl;
        return -1;
    }
    //3.设置编码器分辨率
    camera.encoder_ctx -> width = config.width;
    camera.encoder_ctx -> height = config.height;
    //4.设置编码器输入像素格式（先转成yuyv后再编码）
    camera.encoder_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    //5.设置编码时间基
    camera.encoder_ctx -> time_base = AVRational{1,config.fps};
    //6.设置帧率
    camera.encoder_ctx -> framerate = AVRational{config.fps,1};
    //7.设置目标码率
    camera.encoder_ctx -> bit_rate = config.bitrate;
    //8.设置gop长度 每2s一个关键帧
    camera.encoder_ctx -> gop_size = config.fps * 2;
    //9.关闭b帧
    camera.encoder_ctx -> max_b_frames = 0;
    //10.打开H264编码器
    int ret = avcodec_open2(camera.encoder_ctx,encoder,nullptr);
    if(ret < 0)
    {
        cerr << "打开H264编码器失败" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    cout <<"H264编码器初始化成功" << encoder-> name <<endl;
    return 0;
}

//分配视频处理过程中重复使用的packet和frame
static int allocate_video_buffers(CodecCamera& camera)
{
    //1.申请摄像头输入packet用于保存av_read_frame(),从摄像头读取出来的MJPEG压缩数据
    camera.input_packet = av_packet_alloc();
    if(!camera.input_packet)
    {
        cerr << "创建摄像头输入Packet失败" << endl;
        return -1;
    }
    //2.申请mjpeg解码后的frame
    camera.decoded_frame = av_frame_alloc();
    if(!camera.decoded_frame)
    {
        cerr << "创建decoded_frame失败" << endl;
        return -1;
    }
    //3.申请准备送给H264编码器的frame
    camera.encoder_frame = av_frame_alloc();
    if(!camera.encoder_frame)
    {
        cerr << "创建encoder_frame失败" <<endl;
        return -1;
    }
    //4.设置encoder_frame格式必须和H264 encoder要求保持一致
    camera.encoder_frame -> format = camera.encoder_ctx -> pix_fmt;
    camera.encoder_frame -> width = camera.encoder_ctx -> width;
    camera.encoder_frame -> height = camera.encoder_ctx -> height;
    //5.给encoder_frame申请数据空间
    int ret = av_frame_get_buffer(camera.encoder_frame,32);
    if(ret < 0)
    {
        cerr << "为encoder_frame申请图像buffer失败:" << ffmpeg_error_string(ret) << endl;
        return -1;
    }
    cout << "视频packet和frame分配成功" << endl;
    return 0;
}

//根据mjpeg真正解码出来的frame 创建或更新图像格式转换 输入：decoded_frame 输出：encoder_ctx要求的格式
static int ensure_converter(CodecCamera& camera)
{
    AVPixelFormat src_format = static_cast<AVPixelFormat>(camera.decoded_frame->format);
    AVPixelFormat dst_format = camera.encoder_ctx -> pix_fmt;
    camera.sws_ctx = sws_getCachedContext(camera.sws_ctx,camera.decoded_frame->width,
                                                        camera.decoded_frame->height,
                                                        src_format,//输入像素格式
                                                        camera.encoder_ctx->width,
                                                        camera.encoder_ctx->height,
                                                        dst_format,//输出像素格式
                                                        SWS_BILINEAR,//缩放算法
                                                        nullptr,nullptr,nullptr);
    if(!camera.sws_ctx)
    {
        cerr << "创建swscontext失败" << endl;
        return -1;
    }
    return 0;
}

int get_h264_packet(CodecCamera& camera,AVPacket* output_packet)
{
    if(!output_packet)
    {
        cerr << "output_packet为空" << endl;
        return -1;
    }
    av_packet_unref(output_packet);
    while(true)
    {
        int ret = av_read_frame(camera.input_fmt_ctx,camera.input_packet);
        if(ret < 0)
        {
            cerr << "读取摄像头数据失败:" << ffmpeg_error_string(ret) << endl;
            return -1;
        }
        if(camera.input_packet->stream_index != camera.video_stream_index)
        {
            av_packet_unref(camera.input_packet);
            continue;
        }
        ret = avcodec_send_packet(camera.decoder_ctx,camera.input_packet);
        av_packet_unref(camera.input_packet);
        if(ret < 0)
        {
            cerr << "发送mjpeg packet到解码器失败:" << ffmpeg_error_string(ret) << endl;
            return -1;
        }
        ret = avcodec_receive_frame(camera.decoder_ctx,camera.decoded_frame);
        if(ret == AVERROR(EAGAIN))
        {
            continue;
        }
        if(ret == AVERROR_EOF)
        {
            cerr << "MJPEG解码器已经结束" << endl;
            return -1;
        }
        if(ret < 0)
        {
            cerr << "解码器编码失败:"  << ffmpeg_error_string(ret) << endl;
            return -1;
        }
        if(ensure_converter(camera)<0)
        {
            return -1;
        }
        ret = av_frame_make_writable(camera.encoder_frame);
        if(ret < 0)
        {
            cerr << "encoder_frame不可写:" << ffmpeg_error_string(ret) <<endl;
            return -1;
        }
        int scaled_height = sws_scale(camera.sws_ctx,camera.decoded_frame->data,
                                                    camera.decoded_frame->linesize,
                                                    0,
                                                    camera.decoded_frame->height,
                                                    camera.encoder_frame->data,
                                                    camera.encoder_frame->linesize);
        if(scaled_height <= 0)
        {
            cerr << "视频格式转换失败" << endl;
            return -1;
        }
        camera.encoder_frame-> pts = camera.next_pts;
        camera.next_pts++;
        ret = avcodec_send_frame(camera.encoder_ctx,camera.encoder_frame);
        if(ret < 0)
        {
            cerr << "发送frame到h264编码器失败" <<ffmpeg_error_string(ret) << endl;
            return -1;
        }
        ret = avcodec_receive_packet(camera.encoder_ctx,output_packet);
        if(ret == AVERROR(EAGAIN))
        {
            continue;
        }
        if(ret == AVERROR_EOF)
        {
            cerr << "h264编码器已经结束" << endl;
            return -1;
        }
        if(ret < 0)
        {
            cerr << "获取h264 packet 失败:" << ffmpeg_error_string(ret) << endl;
            return -1;
        }
        //这里成功得到一份h264 packet
        return 1;
    }
}

//初始化整个摄像头codec处理链
int init_codec_camera(CodecCamera& camera,const CodecCameraConfig& config)
{
    //1.打开摄像头
    if(open_camera(camera,config) < 0)
    {
        cleanup_codec_camera(camera);
        return -1;
    }
    //2.初始化mjpeg解码器
    if(init_decoder(camera)<0)
    {
        cleanup_codec_camera(camera);
        return -1;
    }
    //3.初始化h264编码器
    if(init_encoder(camera,config) < 0)
    {
        cleanup_codec_camera(camera);
        return -1;
    }
    //4.申请运行时packet和frame
    if(allocate_video_buffers(camera) <0)
    {
        cleanup_codec_camera(camera);
        return -1;
    }
    //5.初始化pts
    camera.next_pts = 0;
    cout << "CodecCamera初始化完成" << endl;
    return 0;
}

//释放全部资源
void cleanup_codec_camera(CodecCamera& camera)
{
    //1.释放摄像头输入packet
    if(camera.input_packet)
    {
        av_packet_free(&camera.input_packet);
    }
    //2.释放mjpeg解码后的frame
    if(camera.decoded_frame)
    {
        av_frame_free(&camera.decoded_frame);
    }
    //3.释放送给h264编码器的frame
    if(camera.encoder_frame)
    {
        av_frame_free(&camera.encoder_frame);
    }
    //4.释放图像格式转换器
    if(camera.sws_ctx)
    {
        sws_freeContext(camera.sws_ctx);
        camera.sws_ctx = nullptr;
    }
    //5.释放mjpeg decoder
    if(camera.decoder_ctx)
    {
        avcodec_free_context(&camera.decoder_ctx);
    }
    //6.释放h264 Encoder
    if(camera.encoder_ctx)
    {
        avcodec_free_context(&camera.encoder_ctx);
    }
    //7.关闭摄像头输入
    if(camera.input_fmt_ctx)
    {
        avformat_close_input(&camera.input_fmt_ctx);
    }
    //8.恢复状态
    camera.video_stream_index = -1;
    camera.next_pts = 0;
    cout << "codeccamera资源释放完毕" << endl;

}