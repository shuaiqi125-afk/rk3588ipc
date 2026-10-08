#include "common/dma_buffer.h"
#include "common/frame_desc.h"
#include "common/frame_ipc.h"

#include <iostream>
#include <cstring>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>


using namespace std;

int main()
{
    //1.建立测试Unix Socket
    int sockets[2];
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)<0)
    {
        perror("socketpair失败");
        return -1;
    }
    //2.fork
    pid_t pid = fork();
    if(pid < 0)
    {
        perror("fork失败");
        return -1;
    }
    //子进程:模拟rtsps
    if(pid == 0)
    {
        close(sockets[0]);
        FrameDesc desc {};
        int received_fd = -1;
        //3.接收FrameDesc + dma-buf fd
        if(!receive_frame(sockets[1],desc,received_fd))
        {
            return -1;
        }
        cout << "RTSPS收到dma-buf fd:" << received_fd << endl;
        cout << "RTSPS收到FrameDesc:" << endl;
        cout << "size=" << desc.size << endl;
        cout << "pts=" << desc.pts << endl;
        cout << "sequence=" << desc.sequence<<endl;
        cout << "flags=" << desc.flags << endl;
        //4.RTSPS根据desc.size mmap 
        //测试里buffer 本身申请的是1mb，mmap仍然映射完整1mb
        size_t buffer_size = 1024*1024;
        void* addr = mmap(nullptr,buffer_size,PROT_READ,MAP_SHARED, received_fd,0);
        if(addr == MAP_FAILED)
        {
            perror("RTSPS mmap失败");
            close(received_fd);
            return -1;
        }
        //5.读取dma-buf
        cout << "RTSPS读取dma-buf:" << static_cast<char*>(addr)<<endl;
        //6.清理
        munmap(addr,buffer_size);
        close(received_fd);
        close(sockets[1]);
        return 0;
    }
    //父进程:模拟codec
    close(sockets[1]);
    //7.codec申请dma-buf
    DmaBuffer buffer;
    size_t buffer_size = 1024*1024;
    if(!allocate_dma_buffer("/dev/dma_heap/system",buffer_size,buffer))
    {
        return -1;
    }
    //8.codec准备测试数据
    const char* text = "hello dma-buf from CODEC";
    size_t text_size = strlen(text)+1;
    memcpy(buffer.addr,text,text_size);
    //9.codec构造FrameDesc
    FrameDesc desc {};
    desc.codec = static_cast<uint32_t>(FrameCodec::H264);
    desc.size = static_cast<uint32_t>(text_size);
    desc.pts = 100;
    desc.sequence = 1;
    desc.flags = FRAME_FLAG_KEY;
    cout << "CODEC准备发送:" << endl;
    cout << "dma-buf fd=" << buffer.fd << endl;
    cout << "size=" << desc.size << endl;
    //10.一次发送: FrameDesc + dma-buf fd
    if(!send_frame(sockets[0],desc,buffer.fd))
    {
        release_dma_buffer(buffer);
        return -1;
    }
    //11.等待RTSPS
    waitpid(pid,nullptr,0);
    //12.codec清理
    release_dma_buffer(buffer);
    close(sockets[0]);
    return 0;
}