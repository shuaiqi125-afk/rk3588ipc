#pragma once
#include "frame_desc.h"

int sned_frame_fd(int socket_fd,int dma_buf_fd,const FrameDesc& frame);
int recv_frame_fd(int socket_fd,int& dma_buf_fd,FrameDesc& frame);