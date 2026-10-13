#include "frame_buffer_pool.h"

#include <limits>

using namespace std;

//初始化buffer pool
bool init_frame_buffer_pool(FrameBufferPool& pool,const FrameBufferPoolConfig& config)
{
    //1.检查配置
    if(config.buffer_count == 0 || config.buffer_capacity == 0 || config.heap_path.empty())
    {
        return false;
    }
    //2.如果pool之前已经初始化过,先把旧资源释放
    release_frame_buffer_pool(pool);
    //3.保存配置运行
    pool.config = config;
    pool.dropped_frames = 0;
    //4.一次性创建固定数量的buffer
    pool.buffers.resize(config.buffer_count);
    //5.给每一个buffer申请真正的dma-buf
    for(uint32_t i = 0; i< config.buffer_count;i++)
    {
        SharedFrameBuffer& buffer = pool.buffers[i];
        //buffer编号
        buffer.id = i;
        //初始状态都是FREE 
        buffer.state = FrameBufferState::FREE;
        buffer.sequence = 0;
        buffer.data_size = 0;
        //真正申请dma-buf
        if(!allocate_dma_buffer(config.heap_path,config.buffer_capacity,buffer.dma))
        {
            //只要有一块申请失败，整个pool初始化就算失败
            for(uint32_t j=0;j<i;j++)
            {
                release_dma_buffer(pool.buffers[j].dma);
            }
            pool.buffers.clear();
            return false;
        }
    }
    return true;
}
//销毁整个buffer pool
void release_frame_buffer_pool(FrameBufferPool& pool)
{
    lock_guard<mutex> lock(pool.mutex);
    //1.释放所有mda-buf
    for(SharedFrameBuffer& buffer : pool.buffers)
    {
        release_dma_buffer(buffer.dma);
        buffer.state = FrameBufferState::FREE;
        buffer.sequence = 0;
        buffer.data_size = 0;
    }
    //2.清空buffer数组
    pool.buffers.clear();
    //3.清空统计
    pool.dropped_frames = 0;
    //4.清空配置
    pool.config.buffer_count =0;
    pool.config.buffer_capacity =0;
}
//CODEC取得一块FREE buffer
SharedFrameBuffer* acquire_free_frame_buffer(FrameBufferPool& pool,uint64_t sequence)
{
    lock_guard<mutex>lock(pool.mutex);
    //1.历遍整个buffer pool
    for(SharedFrameBuffer& buffer : pool.buffers)
    {
        //2.找到FREE buffer
        if(buffer.state == FrameBufferState::FREE)
        {
            //3.先标记为WRITING 防止别人再次取得这块buffer
            buffer.state = FrameBufferState::WRITING;
            //记录这一次装的哪一帧
            buffer.sequence = sequence;
            buffer.data_size = 0;
            return &buffer;
        }
    }
    //4.没有FREE buffer,不等待直接统计一次丢帧
    pool.dropped_frames++;
    return nullptr;
}
//CODEC写完h264以后，把 buffer从WRITING变成IN_USE
bool mark_frame_buffer_in_use(FrameBufferPool& pool,uint32_t buffer_id,uint64_t sequence,uint32_t data_size)
{
    lock_guard<mutex>lock (pool.mutex);
    //1.buffer_id 是否合法
    if(buffer_id >= pool.buffers.size())
    {
        return false;
    }
    SharedFrameBuffer& buffer = pool.buffers[buffer_id];
    //2.必须是CODEC正在写的buffer
    if(buffer.state != FrameBufferState::WRITING)
    {
        return false;
    }
    //3.必须还是同一帧
    if(buffer.sequence != sequence)
    {
        return false;
    }
    //4.数据不能超过dma-buf容量
    if(data_size == 0 ||static_cast<size_t>(data_size)>buffer.dma.size)
    {
        return false;
    }
    //5.记录真正的H264有效长度
    buffer.data_size = data_size;
    //6.交给rtsps使用
    buffer.state = FrameBufferState::IN_USE;
    return true;
}
//RTSPS使用完成以后CODEC把这块buffer重新变成free
bool release_frame_buffer(FrameBufferPool& pool,uint32_t buffer_id,uint64_t sequence)
{
    lock_guard<mutex>lock(pool.mutex);
    //1.检查buffer_id
    if(buffer_id >= pool.buffers.size())
    {
        return false;
    }
    SharedFrameBuffer& buffer = pool.buffers[buffer_id];
    //2.必须是正在被RTSPS使用的buffer
    if(buffer.state != FrameBufferState::IN_USE)
    {
        return false;
    }
    //3.检查sequence 防止迟到的旧release错误释放已经重新使用的buffer
    if(buffer.sequence != sequence)
    {
        return false;
    }
    //4.重新变成FREE
    buffer.state = FrameBufferState::FREE;
    buffer.data_size = 0;
    return true;
}
//根据buffer_id 寻找buffer
SharedFrameBuffer* get_frame_buffer(FrameBufferPool& pool,uint32_t buffer_id)
{
    lock_guard<mutex>lock(pool.mutex);
    if(buffer_id >= pool.buffers.size())
    {
        return nullptr;
    }
    return &pool.buffers[buffer_id];
}
