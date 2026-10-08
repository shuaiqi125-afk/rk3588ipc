#pragma once 
//通过unix domain socket发送一个fd
bool send_fd(int socket_fd,int fd_to_send);
//通过unix domain socket接收一个fd
int receive_fd(int socket_fd);
//socket_fd = codec和rtsps之间已经建立好的unix domain socket
//fd_to_send = codec自己的dma-buf fd
