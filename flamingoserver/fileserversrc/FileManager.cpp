/**
 *  文件管理类, FileManager.h
 *  zhangyl 2017.03.17
 **/
#include "FileManager.h"

#include <string.h>

#include "../base/AsyncLog.h"
#include "../base/Platform.h"


FileManager::FileManager()
{

}

FileManager::~FileManager()
{

}

bool FileManager::init(const char* basepath)
{
    m_basepath = basepath;

#ifdef WIN32
    //缓存目录不存在是正常情况（首次部署或改了缓存目录配置），记 INFO 后新建；只有新建失败才记 ERROR
    if (!PathFileExistsA(basepath))
    {
        LOGI("cache dir %s does not exist, creating it.", basepath);

        if (!CreateDirectoryA(basepath, NULL))
        {
            LOGE("failed to create cache dir %s, error: %lu", basepath, GetLastError());
            return false;
        }

        LOGI("cache dir %s created.", basepath);
    }

    return true;
#else
    DIR* dp = opendir(basepath);
    if (dp == NULL)
    {
        //opendir 失败不一定是目录不存在：只有 ENOENT 才表示「不存在、需要新建」，记 INFO；
        //其它原因（无权限、路径中某一段不是目录等）本身就是错误，记 ERROR。
        //两种情况都沿用原有流程尝试新建一次，新建失败再记 ERROR
        const int openErrno = errno;
        if (openErrno == ENOENT)
            LOGI("cache dir %s does not exist, creating it.", basepath);
        else
            LOGE("cannot open cache dir %s, errno: %d, %s", basepath, openErrno, strerror(openErrno));

        if (mkdir(basepath, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) == 0)
        {
            LOGI("cache dir %s created.", basepath);
            return true;
        }

        const int mkdirErrno = errno;
        LOGE("failed to create cache dir %s, errno: %d, %s", basepath, mkdirErrno, strerror(mkdirErrno));

        return false;
    }

    struct dirent* dirp;
    //struct stat filestat;
    while ((dirp = readdir(dp)) != NULL)
    {
        if (strcmp(dirp->d_name, ".") == 0 || strcmp(dirp->d_name, "..") == 0)
            continue;


        //if (stat(dirp->d_name, &filestat) != 0)
        //{
        //    LOGW << "stat filename: [" << dirp->d_name << "] error, errno: " << errno << ", " << strerror(errno);
        //    continue;
        //}

        m_listFiles.emplace_back(dirp->d_name);
        LOGI("filename: %s", dirp->d_name);
    }

    closedir(dp);
#endif

    return true;
}

bool FileManager::isFileExsit(const char* filename)
{
    std::lock_guard<std::mutex> guard(m_mtFile);
    //先查看缓存
    for (const auto& iter : m_listFiles)
    {
        if (iter == filename)
            return true;
    }

    //再查看文件系统
    std::string filepath = m_basepath;
    filepath += filename;
    FILE* fp = fopen(filepath.c_str(), "r");
    if (fp != NULL)
    {
        fclose(fp);
        m_listFiles.emplace_back(filename);
        return true;
    }

    return false;
}

void FileManager::addFile(const char* filename)
{
    std::lock_guard<std::mutex> guard(m_mtFile);
    m_listFiles.emplace_back(filename);
}
