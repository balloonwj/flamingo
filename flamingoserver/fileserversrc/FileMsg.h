/** 
 * 协议类型定义, FileMsg.h
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

#pragma pack(push, 1)
//协议头
struct file_msg
{
    int64_t  packagesize;       //指定包体的大小
};

#pragma pack(pop)

/** 
 *  文件上传
 */
/**
    客户端：cmd = msg_type_upload_req, seq, filemd5, offset ,filesize, filedata
    服务器应答： cmd = msg_type_upload_resp, seq, errorcode, filemd5, offset, filesize
 **/

/** 
 *  文件下载
 */
/** 
    客户端：cmd = msg_type_download_req, seq, filemd5, offset, filesize, filedata
    服务器: cmd = msg_type_download_resp, seq, errorcode, filemd5, offset, filesize, filedata

 **/

