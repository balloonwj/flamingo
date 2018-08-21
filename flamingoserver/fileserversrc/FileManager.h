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

    bool Init(const char* basepath);

    bool IsFileExsit(const char* filename);
    void addFile(const char* filename);

private:
    //上传的文件都以文件的md5值命名
    std::list<std::string>      m_listFiles;
    std::mutex                  m_mtFile;
    std::string                 m_basepath;
};