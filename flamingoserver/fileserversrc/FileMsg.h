/**
 *  文件 / 图片服务器进程内部定义，FileMsg.h
 *
 *  v2 文件传输的「字节流」段（cmd_file_upload / cmd_file_download）在
 *  fileserver / imgserver 连接上收发，复用 net/FrameCodec 的 24 字节帧头
 *  与 §11.1 的附件包体布局，协议常量与消息类型统一在 chatserversrc/Protocol.h，
 *  本文件只保留 fileserver / imgserver 进程自身需要的少量定义。
 *
 *  旧版 v1 的 file_msg_header / file_msg_type 等定义已随 v2 改造移除。
 *
 *  balloonwj@qq.com   2026-05-22
 */
#pragma once

//服务器种类：fileserver 与 imgserver 共用 fileserversrc 代码，仅放行的
//文件场景（scene）不同，借此区分。
enum file_server_kind
{
    file_server_kind_file  = 1,   //文件服务器：放行 scene=1 聊天文件 / scene=2 群文件
    file_server_kind_image = 2,   //图片服务器：放行 scene=3 头像 / scene=4 图片消息
};
