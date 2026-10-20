/** 
 *  文件管理类, FileManager.h
 *  zhangyl 2017.03.17
 **/
#pragma once
#include <string>
#include <list>
#include <mutex>

class FileManager final
{
public:
    FileManager();
    ~FileManager();

    FileManager(const FileManager& rhs) = delete;
    FileManager& operator = (const FileManager& rhs) = delete;

    /**
     *  初始化文件缓存目录。
     *  @param basepath 缓存目录路径（fileserver 的 filecachedir 或 imgserver 的 imgcachedir），
     *                  调用方保证非空；目录不存在时记一条 INFO 并新建（只建最后一级）
     *  @return 目录已存在或新建成功返回 true；新建失败返回 false 并记 ERROR 日志
     */
    bool init(const char* basepath);

    bool isFileExsit(const char* filename);
    void addFile(const char* filename);

private:
    //上传的文件都以文件的md5值命名
    std::list<std::string>      m_listFiles;
    std::mutex                  m_mtFile;
    std::string                 m_basepath;
};