#pragma once
#include "frame_desc.h"

//发送:
//FrameDesc + 一个dma-buf fd
bool send_frame(int socket_fd,const FrameDesc& desc,int dma_buf_fd);
//接收:
//FrameDesc + 接收端自己的dma-buf fd
bool receive_frame(int socket_fd,FrameDesc& desc,int& received_fd);
