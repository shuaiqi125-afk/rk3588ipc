#pragma once
//RTSP创建Unix Domain Socket服务器
//成功 返回socket fd 失败返回 -1
int create_frame_receiver_server();

//等待codec连接 成功返回与codec通信的socket fd 失败返回-1
int accept_codec_connection(int server_fd);
//关闭服务器
void close_frame_receiver_server(int server_fd);
