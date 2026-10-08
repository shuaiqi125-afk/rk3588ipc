#include "dma_buffer.h"

#include <iostream>
#include <cstring>
#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include <linux/dma-heap.h>

using namespace std;

//申请并映射一个dma-buf
bool allocate_dma_buffer(const string& heap_path,size_t size,DmaBuffer& buffer)
{
    //1.检查参数
    if(size == 0)
    {
        cerr << "dma-buf大小不能为0" << endl;
        return false;
    }
    //2.打开dma-heap 例如:/dev/dma_heap/system
    int heap_fd = open(heap_path.c_str(),O_RDWR | O_CLOEXEC);
    if(heap_fd < 0)
    {
        cerr << "打开dma-heap失败:" << heap_path << " " <<strerror(errno) << endl;
        return false;
    }
    //3.准备dma-buf申请参数
    dma_heap_allocation_data allocation {};
    //要申请多少字节
    allocation.len = size;
    //希望返回的dma-buf fd 具有这些属性
    allocation.fd_flags = O_RDWR | O_CLOEXEC;
    //当前保持0
    allocation.heap_flags = 0;
    //4.向dma-heap申请dma-buf
    if(ioctl(heap_fd,DMA_HEAP_IOCTL_ALLOC,&allocation) < 0)//参数二是申请一块DMA-BUF内存 参数三里边包含申请信息
    {
        cerr << "申请dma_buf失败:" << strerror(errno) << endl;
        close(heap_fd);
        return false;
    }
    //dma-heap只负责帮我们创建dma-buf
    //dma-buf 已经创建完成以后 
    //heap_fd 就可以关闭了
    close(heap_fd);
    //5.保存dma-buf fd
    buffer.fd = allocation.fd;
    buffer.size = size;
    //6.把dma-buf映射到当前进程虚拟空间地址
    buffer.addr = mmap(nullptr,buffer.size,PROT_READ | PROT_WRITE,MAP_SHARED,buffer.fd,0);
    if(buffer.addr == MAP_FAILED)//MAP_FAILED是mmap()失败时返回的特殊值
    {
        cerr << "mmap dma-buf 失败:" << strerror(errno) << endl;
        close(buffer.fd);
        buffer.fd = -1;
        buffer.size = 0;
        buffer.addr = nullptr;
        return false;
    }
    cout << "dma-buf 申请成功" << endl;
    cout << "fd:" << buffer.fd << endl;
    cout << "size:" << buffer.size << endl;
    cout << "addr:" << buffer.addr <<endl;
    return true;
}

//释放dma-buf
void release_dma_buffer(DmaBuffer& buffer)
{
    //1.取消mmap映射
    if(buffer.addr && buffer.addr != MAP_FAILED)
    {
        munmap(buffer.addr,buffer.size);
        buffer.addr = nullptr;
    }
    //2.关闭dma-buf fd 
    if(buffer.fd >= 0)
    {
        close(buffer.fd);
        buffer.fd = -1;
    }
    buffer.size = 0;
}


