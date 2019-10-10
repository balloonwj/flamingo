/** 
 * 文件传输协议类型定义, FileMsg.h
 * zhangyl 2017.03.17
 **/
#pragma once

#include <stdint.h>

enum file_msg_type
{
    file_msg_type_unknown,
    msg_type_upload_req,
    msg_type_upload_resp,
    msg_type_download_req,
    msg_type_download_resp,
};

enum file_msg_error_code
{
    file_msg_error_unknown,        //未知错误
    file_msg_error_progress,       //文件上传或者下载进行中
    file_msg_error_complete,       //文件上传或者下载完成
    file_msg_error_not_exist       //文件不存在
};

//客户端网络类型
enum client_net_type
{
    client_net_type_broadband,    //宽带
    client_net_type_cellular      //移动网络
};

#pragma pack(push, 1)
//协议头
struct file_msg_header
{
    int64_t  packagesize;       //指定包体的大小
};

#pragma pack(pop)

/** 
 *  文件上传
 */
/**
    //filemd5为整个文件内容的md5值，offset为每次文件上传的偏移量, filesize为文件大小，filedata为每次上传的文件字节内容
    客户端：cmd = msg_type_upload_req(int32), seq(int32), filemd5(string, 长度是32), offset(int64) ,filesize(int64), filedata(string)
    服务器应答： cmd = msg_type_upload_resp(int32), seq(int32), errorcode(int32), filemd5(string, 长度是32), offset(int64), filesize(int64)
 **/

/** 
 *  文件下载
 */
/** 
    //filemd5为整个文件内容的md5值，offset为每次文件上传的偏移量, filesize为文件大小，filedata为每次上传的文件字节内容, 
    //clientNetType(客户端网络类型，宽带或者Wifi传0，3G/4G/5G传1）
    客户端：cmd = msg_type_download_req, seq(int32), filemd5(string, 长度是32), offset(int64) ,filesize(int64), filedata(string), clientNetType(int32)
    服务器: cmd = msg_type_download_resp(int32), seq(int32), errorcode(int32), filemd5(string, 长度是32), offset(int64), filesize(int64)
 **/

