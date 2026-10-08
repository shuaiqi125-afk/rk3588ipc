// mda-buf 跨进程测试
#include "common/dma_buffer.h"
#include "common/fd_transfer.h"

#include <iostream>
#include <cstring>

#include <unistd.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <sys/wait.h>


using namespace std;

int main()
{
    //1.创建一堆已经互相连接的unix domain socket; socket[0]<->socket[1]
    //这里只是为了测试SCM_RIGHTS，后面真正项目会换成CODEC <->rtsps 的 unix socket
    int sockets[2];//这里有sockets[0] sockets[1]
    if(socketpair(AF_UNIX,SOCK_STREAM,0,sockets)<0)
    {
        perror("socketpair失败");
        return -1;
    }
    //2.fork创建两个进程
    pid_t pid = fork();
    if(pid < 0)
    {
        perror("fork失败");
        return -1;
    }
    //子进程 模拟rtsps
    if(pid == 0)
    {
        //子进程不用sockets[0]
        close(sockets[0]);
        cout << "RTSP测试进程等待fd..." << endl;
        //3.rtsps接收dma-buf fd
        int received_fd = receive_fd(sockets[1]);
        if(received_fd < 0)
        {
            return -1;
        }
        //4.已知测试buffer大小为1mb
        size_t buffer_size = 1024*1024;
        //5.rtsps mmap同一个dma-buf
        void* addr = mmap(nullptr,buffer_size,PROT_READ,MAP_SHARED,received_fd,0);
        if(addr == MAP_FAILED)
        {
            perror("RTSPS mmap 失败");
            close(received_fd);
            return -1;
        }
        cout << "RTSPS mmap地址:" << addr << endl;
        //6.读取codec写进去的数据
        cout << "RTSPS读取到:" <<static_cast<char*>(addr)<<endl;
        //7.清理rtsps资源
        munmap(addr,buffer_size);
        close(received_fd);
        close(sockets[1]);
        return 0;
    }
    //父进程不用socket[1]
    close(sockets[1]);
    //注意:dma-buf必须在fork以后申请，这样子进程不会因为fork自动继承dma-buf fd，
    DmaBuffer buffer;
    size_t buffer_size = 1024 * 1024;
    //8.codec申请dma-buf
    if(!allocate_dma_buffer("/dev/dma_heap/system",buffer_size,buffer))
    {
        return -1;
    }
    cout << "CODEC dma-buf fd:" << buffer.fd << endl;
    cout << "CODEC mmap地址:" <<buffer.addr <<endl;
    //9.codec往dma-buf写数据
    const char* text = "hello dma-buf from CODEC";
    memcpy(buffer.addr,text,strlen(text)+1);
    cout << "CODEC写入:" << text << endl;
    //10.codec把dma-buf fd 传给rtsps
    if(!send_fd(sockets[0],buffer.fd))
    {
        release_dma_buffer(buffer);
        return -1;
    }
    //11.等待rtsps读取完成
    waitpid(pid,nullptr,0);
    //12.codec清理
    release_dma_buffer(buffer);
    close(sockets[0]);
    return 0;
}