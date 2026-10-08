#pragma once 

#include<cstddef>
#include<string>

struct DmaBuffer
{
    //dma-buf文件描述符
    int fd = -1;
    //dma-buf大小
    size_t size = 0;
    //mmap以后得到的cpu虚拟地址
    void* addr = nullptr;
};
//申请并映射一个dma-buf
bool allocate_dma_buffer(const std::string& heap_path,size_t size,DmaBuffer& buffer);
//释放dma-buf
void release_dma_buffer(DmaBuffer& buffer);
