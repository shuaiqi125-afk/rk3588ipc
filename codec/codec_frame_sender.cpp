#include "codec_frame_sender.h"

#include "../common/frame_ipc.h"
#include "../common/frame_desc.h"

#include <cstring>
#include <cerrno>
#include <iostream>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <poll.h>

using namespace std;


static const char* FRAME_SOCKET_PATH =
    "/tmp/rk3588ipc_codec_rtsp.sock";


//连接RTSPS
int connect_frame_receiver()
{
    //1.创建Unix Domain Socket
    int socket_fd =
        socket(
            AF_UNIX,
            SOCK_SEQPACKET,
            0
        );

    if(socket_fd < 0)
    {
        cerr
            << "创建frame socket失败: "
            << strerror(errno)
            << endl;

        return -1;
    }


    //2.准备RTSPS地址
    sockaddr_un address{};

    address.sun_family =
        AF_UNIX;

    strncpy(
        address.sun_path,
        FRAME_SOCKET_PATH,
        sizeof(address.sun_path) - 1
    );


    //3.连接RTSPS
    if(
        connect(
            socket_fd,
            reinterpret_cast<sockaddr*>(
                &address
            ),
            sizeof(address)
        ) < 0
    )
    {
        cerr
            << "连接RTSPS失败: "
            << strerror(errno)
            << endl;

        close(socket_fd);

        return -1;
    }


    return socket_fd;
}


//CODEC连接RTSPS以后
//把整个buffer pool注册给RTSPS
bool register_frame_buffer_pool(
    int socket_fd,
    FrameBufferPool& pool
)
{
    if(socket_fd < 0)
    {
        return false;
    }


    //========================================================
    // 1.先发送整个pool的基本信息
    //========================================================

    FramePoolInfo info{};

    info.buffer_count =
        pool.config.buffer_count;

    info.buffer_capacity =
        pool.config.buffer_capacity;


    if(
        !send_frame_pool_info(
            socket_fd,
            info
        )
    )
    {
        cerr
            << "发送buffer pool信息失败"
            << endl;

        return false;
    }


    //========================================================
    // 2.把每一块dma-buf fd注册给RTSPS
    //========================================================

    for(
        SharedFrameBuffer& buffer :
        pool.buffers
    )
    {
        if(buffer.dma.fd < 0)
        {
            cerr
                << "buffer "
                << buffer.id
                << " dma-buf fd无效"
                << endl;

            return false;
        }


        FrameBufferRegister message{};

        message.buffer_id =
            buffer.id;


        if(
            !send_frame_buffer_register(
                socket_fd,
                message,
                buffer.dma.fd
            )
        )
        {
            cerr
                << "注册buffer失败, buffer_id="
                << buffer.id
                << endl;

            return false;
        }
    }


    return true;
}


//处理RTSPS已经返回的FRAME_RELEASE
//
//这个函数不会一直等。
//如果当前socket里没有release消息，就直接返回。
bool receive_frame_release(
    int socket_fd,
    FrameBufferPool& pool
)
{
    if(socket_fd < 0)
    {
        return false;
    }


    while(true)
    {
        //====================================================
        // 1.先检查socket现在有没有数据
        //
        // timeout = 0
        // 表示只检查，不等待
        //====================================================

        pollfd pfd{};
        pfd.fd =socket_fd;//检查codec和rtsps的socket
        pfd.events =POLLIN;//关心这个socket有没有数据可以读
        int ret =poll(&pfd,1, 0);

        if(ret < 0)
        {
            //被信号打断
            if(errno == EINTR)
            {
                continue;
            }

            cerr
                << "检查FRAME_RELEASE失败: "
                << strerror(errno)
                << endl;

            return false;
        }


        //当前没有RTSPS返回的release
        if(ret == 0)
        {
            return true;
        }


        //连接已经出问题
        if(
            pfd.revents &
            (
                POLLERR |
                POLLHUP |
                POLLNVAL
            )
        )
        {
            return false;
        }


        //没有可读消息
        if(
            !(pfd.revents & POLLIN)
        )
        {
            return true;
        }


        //====================================================
        // 2.接收一个FRAME_RELEASE
        //====================================================

        FrameReleaseMessage message{};


        if(
            !receive_frame_release_message(
                socket_fd,
                message
            )
        )
        {
            cerr
                << "接收FRAME_RELEASE失败"
                << endl;

            return false;
        }


        //====================================================
        // 3.把对应buffer重新变成FREE
        //====================================================

        if(
            !release_frame_buffer(
                pool,
                message.buffer_id,
                message.sequence
            )
        )
        {
            cerr
                << "释放buffer失败, buffer_id="
                << message.buffer_id
                << ", sequence="
                << message.sequence
                << endl;

            return false;
        }


        //不要return
        //
        //继续while，看看socket里是不是还有更多
        //FRAME_RELEASE没有处理。
    }
}


//发送一帧H264
bool send_h264_packet(
    int socket_fd,
    FrameBufferPool& pool,
    const AVPacket* packet,
    uint64_t sequence,
    uint32_t channel_id,
    uint32_t stream_id
)
{
    //========================================================
    // 1.检查packet
    //========================================================

    if(
        socket_fd < 0 ||
        packet == nullptr ||
        packet->data == nullptr ||
        packet->size <= 0
    )
    {
        return false;
    }


    //========================================================
    // 2.先处理RTSPS之前已经使用完成的buffer
    //
    // 例如：
    //
    // buffer[0] IN_USE
    // RTSPS已经发送RELEASE
    //
    // 在这里把它变回FREE
    //========================================================

    if(
        !receive_frame_release(
            socket_fd,
            pool
        )
    )
    {
        return false;
    }


    //========================================================
    // 3.检查当前H264 packet能不能装进一块dma-buf
    //========================================================

    if(
        static_cast<size_t>(
            packet->size
        ) >
        pool.config.buffer_capacity
    )
    {
        cerr
            << "H264 packet超过buffer容量: "
            << packet->size
            << " > "
            << pool.config.buffer_capacity
            << endl;

        return false;
    }


    //========================================================
    // 4.从pool里取得一块FREE buffer
    //========================================================

    SharedFrameBuffer* buffer =
        acquire_free_frame_buffer(
            pool,
            sequence
        );


    if(buffer == nullptr)
    {
        //这里不是程序错误。
        //
        //只是目前所有buffer都还在被RTSPS使用。
        //acquire函数内部已经增加dropped_frames。
        //
        //当前帧直接丢弃。
        return true;
    }


    //此时状态：
    //
    // FREE
    //  ↓
    // WRITING


    //========================================================
    // 5.把H264数据写进已经提前申请好的dma-buf
    //========================================================

    memcpy(
        buffer->dma.addr,
        packet->data,
        static_cast<size_t>(
            packet->size
        )
    );


    //========================================================
    // 6.告诉buffer pool：
    // CODEC已经写完
    //
    // WRITING
    //    ↓
    // IN_USE
    //========================================================

    if(
        !mark_frame_buffer_in_use(
            pool,
            buffer->id,
            sequence,
            static_cast<uint32_t>(
                packet->size
            )
        )
    )
    {
        cerr
            << "buffer状态切换到IN_USE失败"
            << endl;

        return false;
    }


    //========================================================
    // 7.准备FRAME_READY
    //========================================================

    FrameReadyMessage message{};


    message.frame.buffer_id =
        buffer->id;


    message.frame.channel_id =
        channel_id;


    message.frame.stream_id =
        stream_id;


    message.frame.codec =
        static_cast<uint32_t>(
            FrameCodec::H264
        );


    message.frame.size =
        static_cast<uint32_t>(
            packet->size
        );


    message.frame.pts =
        packet->pts;


    message.frame.sequence =
        sequence;


    if(
        packet->flags &
        AV_PKT_FLAG_KEY
    )
    {
        message.frame.flags =
            FRAME_FLAG_KEY;
    }
    else
    {
        message.frame.flags =
            FRAME_FLAG_NONE;
    }


    //========================================================
    // 8.通知RTSPS：
    // H264已经在buffer里面了
    //
    // 注意：
    // 这里不再传dma-buf fd。
    // 只发送FrameReadyMessage。
    //========================================================

    if(
        !send_frame_ready(
            socket_fd,
            message
        )
    )
    {
        cerr
            << "发送FRAME_READY失败"
            << endl;


        //RTSPS没有成功收到这一帧，
        //所以这块buffer不能永远保持IN_USE。
        //
        //重新归还给pool。
        release_frame_buffer(
            pool,
            buffer->id,
            sequence
        );


        return false;
    }


    return true;
}


//关闭CODEC和RTSPS之间的socket
void close_frame_sender(
    int socket_fd
)
{
    if(socket_fd >= 0)
    {
        close(
            socket_fd
        );
    }
}