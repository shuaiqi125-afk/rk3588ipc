#include "rtsp_frame_receiver.h"
#include "rtsp_output.h"

#include "../common/frame_ipc.h"
#include "../common/frame_desc.h"

#include <iostream>
#include <string>
#include <cstdint>
#include <climits>
#include <limits>
#include <new>
#include <vector>

#include <unistd.h>
#include <sys/mman.h>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/buffer.h>
}

using namespace std;

//RTSPS保存的一块已经注册并mmap好的dma-buf
struct MappedFrameBuffer
{
    //RTSPS进程自己的dma-buf fd
    int fd=-1;
    //RTSPS mmap 出来的虚拟地址
    void* addr = nullptr;
    //这块mda-buf的总容量
    size_t capacity = 0;
};
//每一个正在被FFmpeg使用的packet对应一个释放信息
struct PacketLease
{
    //CODEC <-> RTSPS socket 
    int socket_fd = -1;
    //使用的是pool中的哪块buffer
    uint32_t buffer_id = 0;
    //这一块buffer当前装的哪一帧
    uint64_t sequence = 0;
};
//清理rtsps保存整个dma-buf pool
static void cleanup_mapped_frame_pool(vector<MappedFrameBuffer>& buffers)
{
    for(MappedFrameBuffer& buffer : buffers)
    {
        //解除RTSPS自己的mmap
        if(buffer.addr != nullptr && buffer.addr != MAP_FAILED)
        {
            munmap(buffer.addr,buffer.capacity);
            buffer.addr = nullptr;
        }
        //关闭RTSPS自己的dma-buf fd
        if(buffer.fd >=0)
        {
            close(buffer.fd);
            buffer.fd = -1;
        }
        buffer.capacity = 0;
    }
    buffers.clear();
}
//RTSPS启动后接收CODEC注册的整个buffer pool
static bool receive_and_map_frame_buffer_pool(int socket_fd,vector<MappedFrameBuffer>& buffers)
{
    //1.先接收pool基本信息
    FramePoolInfo info{};
    if(!receive_frame_pool_info(socket_fd,info))
    {
        cerr <<"接收FramePoolInfo失败" << endl;
        return false;
    }
    //2.检查pool配置
    if(info.buffer_count == 0|| info.buffer_capacity == 0 ||
         info.buffer_capacity > static_cast<uint64_t>(numeric_limits<size_t>::max()))
    {
        cerr << "收到非法buffer pool配置" << endl;
        return false;
    }
    size_t buffer_capacity = static_cast<size_t>(info.buffer_capacity);
    //3.按照CODEC告诉我们的数量创建buffer表
    buffers.resize(info.buffer_count);
    //4.逐个接收每一块dma-buf
    for(uint32_t i = 0; i< info.buffer_count; i++)
    {
        FrameBufferRegister message{};
        int received_fd = -1;
        if(!receive_frame_buffer_register(socket_fd,message,received_fd))
        {
            cerr << "接收dma-buf注册失败" << endl;
            cleanup_mapped_frame_pool(buffers);
            return false;
        }
        //5.检查buffer_id
        if(message.buffer_id >= buffers.size())
        {
            cerr << "收到非法buffer_id=" << message.buffer_id << endl;
            close(received_fd);
            cleanup_mapped_frame_pool(buffers);
            return false;
        }
        MappedFrameBuffer& buffer = buffers[message.buffer_id];
        //不允许同一个buffer重复注册
        if(buffer.fd >= 0)
        {
            cerr << "buffer重复注册,buffer_id=" << message.buffer_id << endl;
            close(received_fd);
            cleanup_mapped_frame_pool(buffers);
            return false;
        }
        //6.这块dma-buf只mmap一次
        void* addr = mmap(nullptr,buffer_capacity,PROT_READ,MAP_SHARED,received_fd,0);
        if(addr == MAP_FAILED)
        {
            perror("RTSPS mmap dma-buf失败");
            close(received_fd);
            cleanup_mapped_frame_pool(buffers);
            return false;
        }
        //7.保存
        buffer.fd = received_fd;
        buffer.addr = addr;
        buffer.capacity = buffer_capacity;
    }
    cout << "RTSPS buffer pool 注册完成:" << info.buffer_count << "块，每块" << info.buffer_capacity<<endl;
    return true;
}
//FFmpeg最后不再使用某一帧时调用
//这里通知CODEC这一帧用完了，这块buffer可以重新FREE
static void release_packet_lease(void* opaque,uint8_t* data)
{
    (void)data;
    PacketLease* lease = static_cast<PacketLease*>(opaque);
    if(lease == nullptr)
    {
        return;
    }
    FrameReleaseMessage message{};
    message.buffer_id = lease->buffer_id;
    message.sequence = lease-> sequence;
    if(!send_frame_release(lease->socket_fd,message))
    {
        cerr << "发送FRAME_RELEASE失败" << endl;
    }
    delete lease;
}
//手动通知CODEC释放一帧，用于收到FRAME_READY以后，但这一帧因为参数非法等原因无法继续处理
static bool release_frame_to_codec(int socket_fd,const FrameDesc& desc)
{
    FrameReleaseMessage message{};
    message.buffer_id = desc.buffer_id;
    message.sequence = desc.sequence;
    return send_frame_release(socket_fd , message);
}
//根据buffer_id 找到已经mmap好的dma-buf
//创建AVPacket 这里没有H264 memcpy。AVPacket直接引用dma-buf中的数据
static AVPacket* create_packet_from_frame_buffer(int socket_fd,MappedFrameBuffer& buffer,const FrameDesc& desc)
{
    //1.检查H264实际数据长度
    if(desc.size == 0 || desc.size > buffer.capacity || desc.size > static_cast<uint32_t>(INT_MAX))
    {
        cerr << "非法H264数据大小:" << desc.size << endl;
        return nullptr;
    }
    //2.先创建AVPacket
    AVPacket* packet = av_packet_alloc();
    if(packet == nullptr)
    {
        cerr << "创建AVPacket失败" << endl;
        return nullptr;
    }
    //3.创建这一次buffer使用应对的lease
    PacketLease* lease = new(nothrow) PacketLease;
    if(lease == nullptr)
    {
        cerr << "创建PacketLease失败" << endl;
        av_packet_free(&packet);
        return nullptr;
    }
    lease->socket_fd = socket_fd;
    lease->buffer_id = desc.buffer_id;
    lease->sequence = desc.sequence;
    //4.把已经存在的dma-buf内存交给ffmpeg的AVBufferRef引用，并且规定如果这块数据最后没人使用就调用release_packet_lease
    //注意: 这里不申请新的h264内存，data直接指向启动时已经mmap好的dma-buf
    AVBufferRef* buffer_ref = av_buffer_create(static_cast<uint8_t*>(buffer.addr),desc.size,
                                                release_packet_lease,lease,0);
    if(buffer_ref == nullptr)
    {
        cerr << "创建AVBufferRef失败" << endl;
        delete lease;
        av_packet_free(&packet);
        return nullptr;
    }
    //5.AVPacket 直接引用dma-buf
    packet->buf = buffer_ref;
    packet->data = static_cast<uint8_t*>(buffer.addr);
    packet->size = static_cast<int>(desc.size);
    //6.恢复时间戳
    packet->pts = desc.pts;
    //当前H264编码没有使用B帧
    packet->dts = desc.pts;
    //当前时间基是1/fps 
    packet -> duration = 1;
    //7.恢复关键帧标志
    if(desc.flags& FRAME_FLAG_KEY)
    {
        packet->flags |= AV_PKT_FLAG_KEY;
    }
    return packet; 
}
//RTSPS主程序
int main(int argc,char* argv[])
{
    //例如: 
    // ./rtsp_service \
    // rtsp://127.0.0.1:8554/live \
    // 60 1920 1080
    if(argc < 2)
    {
        cerr << "用法:" << endl << argv[0] << "<rtsp_url> [fps] [width] [height]" << endl;
        return -1;
    }
    string rtsp_url = argv[1];
    int fps = 30;
    int width = 1920;
    int height = 1080;
    if(argc >= 3)
    {
        fps = stoi(argv[2]);
    }
    if(argc >= 4)
    {
        width = stoi(argv[3]);
    }
    if(argc >= 5)
    {
        height = stoi(argv[4]);
    }
    if(fps<=0 || width <=0 || height<=0)
    {
        cerr << "fps/width/height参数非法" << endl;
        return -1;
    }
    //1.初始化FFmpeg网络
    avformat_network_init();
    //2.RTSPS创建Unix Domain Socket服务器
    int server_fd = create_frame_receiver_server();
    if(server_fd < 0)
    {
        avformat_network_deinit();
        return -1;
    }
    //3.等待CODEC连接
    int client_fd = accept_codec_connection(server_fd);
    if(client_fd < 0)
    {
        close_frame_receiver_server(server_fd);
        avformat_network_deinit();
        return -1;
    }
    //4.接收CODEC注册的整个dma-buf pool
    //这里每块dma-buf只接一次fd 并且只mmap一次
    vector<MappedFrameBuffer>frame_buffers;
    if(!receive_and_map_frame_buffer_pool(client_fd,frame_buffers))
    {
        cerr << "初始化RTSPS buffer pool失败" << endl;
        close(client_fd);
        close_frame_receiver_server(server_fd);
        avformat_network_deinit();
        return -1;
    }
    //5.准备H264编码参数
    AVCodecParameters* codecpar = avcodec_parameters_alloc();
    if(codecpar == nullptr)
    {
        cerr << "创建AVCodecParameters失败" << endl;
        cleanup_mapped_frame_pool(frame_buffers);
        close(client_fd);
        close_frame_receiver_server(server_fd);
        avformat_network_deinit();
        return -1;
    }
    codecpar -> codec_type = AVMEDIA_TYPE_VIDEO;
    codecpar -> codec_id = AV_CODEC_ID_H264;
    codecpar -> codec_tag = 0;
    codecpar -> width = width;
    codecpar -> height = height;
    //6.CODEC产生的packet时间基
    AVRational input_time_base{1,fps};
    //7.初始化RTSP输出
    RtspOutput rtsp_output{};
    if(init_rtsp_output(rtsp_output,rtsp_url,codecpar,input_time_base)< 0)
    {
        cerr << "初始化RTSP失败" << endl;
        avcodec_parameters_free(&codecpar);
        cleanup_mapped_frame_pool(frame_buffers);
        close(client_fd);
        close_frame_receiver_server(server_fd);
        avformat_network_deinit();
        return -1;
    }
    cout << "RTSPS开始接收CODEC实时H264" << endl;
    cout << "推流地址:" << rtsp_url << endl;
    uint64_t packet_count = 0;
    //8.RTSPS主循环
    while(true)
    {
        //现在每帧只接收FRAME_READY
        //不再接收dma-buf fd
        FrameReadyMessage message{};
        if(!receive_frame_ready(client_fd,message))
        {
            cerr << "CODEC连接断开或接收FRAME_READY失败" << endl;
            break;
        }
        FrameDesc& desc = message.frame;
        //9.检查buffer_id
        if(desc.buffer_id >= frame_buffers.size())
        {
            cerr << "收到非法buffer_id:" << desc.buffer_id << endl;
            release_frame_to_codec(client_fd,desc);
            break;
        }
        //10.目前RTSPS只处理H264
        if(desc.codec != static_cast<uint32_t>(FrameCodec::H264))
        {
            cerr << "收到非H264数据，忽略" << endl;
            //即使不用这一帧也必须还给CODEC
            release_frame_to_codec(client_fd,desc);
            continue;
        }
        MappedFrameBuffer& buffer = frame_buffers[desc.buffer_id];
        //11.检查数据长度
        if(desc.size == 0 || desc.size > buffer.capacity)
        {
            cerr << "FRAME_READY数据长度非法:" << desc.size <<endl;
            release_frame_to_codec(client_fd,desc);
            continue;
        }
        //12.dma-buf -> AVPacket 直接使用启动时已经mmap好的buffer
        AVPacket* packet = create_packet_from_frame_buffer(client_fd,buffer,desc);
        if(packet == nullptr)
        {
            cerr << "创建H264AVPacket失败" << endl;
            //create失败时还没有建立FFmpeg引用，所以这里手动还给CODEC
            release_frame_to_codec(client_fd,desc);
            break;
        }
        //13.直接交给RTSP输出: AVPacket -> FFmpeg -> MediaMTX
        int ret = write_rtsp_packet(rtsp_output,packet,input_time_base);
        if(ret < 0)
        {
            cerr << "发送h264到mediamtx失败" << endl;
            //释放packet以后 release_packet_lease()会被调用 制动发送FRAME_RELEASE
            av_packet_free(&packet);
            break;
        }
        //14.释放当前AVPacket
        //FFmpeg最后释放buffer_ref时 release_packet_lease()会发送FRAME_RELEASE给CODEC
        av_packet_free(&packet);
        packet_count++;
        if(packet_count % 300 == 0)
        {
            cout <<"RTSPS已经转发"<< packet_count << "个H264 packet" << endl;
        }
    }
    //15.清理RTSP 先清理ffmpeg确保它不再引用任何dma-buf
    cleanup_rtsp_output(rtsp_output);
    //16.释放codecpar
    avcodec_parameters_free(&codecpar);
    //17.现在ffmpeg已经不用这些buffer 才可以真正munmap+close
    cleanup_mapped_frame_pool(frame_buffers);
    //18.关闭CODEC连接
    close(client_fd);
    //19.关闭RTSPS Unix Domain Socket服务器
    close_frame_receiver_server(server_fd);
    //20.关闭FFmpeg网络
    avformat_network_deinit();
    cout << "RTSPS已经退出" << endl;
    return 0;
}