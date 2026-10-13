#pragma once

#include "dma_buffer.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

//dma-buf 当前状态
enum class FrameBufferState : uint32_t
{
    FREE = 0, //可以被CODEC拿来写
    WRITING,  // CODEC正在写这一帧
    IN_USE    //已经发布，消费者正在使用
};

//buffer pool中的一块dma-buf
struct SharedFrameBuffer
{
    //pool中的编号
    uint32_t id =0;
    //真正的dma-buf
    DmaBuffer dma;
    //当前状态
    FrameBufferState state = FrameBufferState::FREE;
    //当前这块buffer里是哪一帧
    uint64_t sequence = 0;
    //当前实际有效数据长度
    uint32_t data_size = 0;
};

//pool运行时配置
struct FrameBufferPoolConfig
{
    //一共申请多少块dma-buf
    uint32_t buffer_count = 0;
    //每块dma-buf多大
    size_t buffer_capacity = 0;
    //dma heap
    std::string heap_path = "/dev/dma_heap/system";
};

//CODEC持有的dma-buf池
struct FrameBufferPool
{
    //缓冲池的配置参数,width height 这类的
    FrameBufferPoolConfig config;
    //真正保存所有dma-buf 的数组
    std::vector<SharedFrameBuffer>buffers;
    //互斥锁
    std::mutex mutex;
    //没有空闲buffer时丢多少帧
    uint64_t dropped_frames = 0;
};

//初始化buffer pool
bool init_frame_buffer_pool(FrameBufferPool& pool,const FrameBufferPoolConfig& config);
//销毁buffer bool
void release_frame_buffer_pool(FrameBufferPool& pool);
//CODEC找一块FREE buffer 没有就返回nullptr
SharedFrameBuffer* acquire_free_frame_buffer(FrameBufferPool& pool,uint64_t sequence);
//CODEC写完H264以后 把buffer标记为IN_USE
bool mark_frame_buffer_in_use(FrameBufferPool& pool,uint32_t bufffer_id,uint64_t sequence,uint32_t data_size);
//RTSPS使用完成以后 CODEC收到释放消息 再把buffer变回free
bool release_frame_buffer(FrameBufferPool& pool,uint32_t buffer_id,uint64_t sequence);
//根据id寻找buffer
SharedFrameBuffer* get_frame_buffer(FrameBufferPool& pool,uint32_t buffer_id);

