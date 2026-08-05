/**
 *  QueryResult 抽象基类的实现，详见 QueryResult.h。
 *  balloonwj@qq.com   2026-07-31
 */
#include "QueryResult.h"

QueryResult::QueryResult(uint64_t rowCount, uint32_t fieldCount)
    : m_CurrentRow(NULL), m_FieldCount(fieldCount), m_RowCount(rowCount)
{
    //按列数分配当前行缓冲区。列数为 0 时 new Field[0] 返回一个合法但不可解引用的指针，
    //此时 fetch() 拿到的数组不含任何字段，与旧实现的行为一致。
    m_CurrentRow = new Field[m_FieldCount];
}

QueryResult::~QueryResult()
{
    //子类析构函数已经释放过各自的原生结果集句柄，这里只负责回收行缓冲区。
    //不能在这里调用虚函数 endQuery()，原因见 QueryResult.h 中 freeRowBuffer() 的注释。
    freeRowBuffer();
}

void QueryResult::endQuery()
{
    freeRowBuffer();
}

void QueryResult::freeRowBuffer()
{
    if (m_CurrentRow != NULL)
    {
        delete [] m_CurrentRow;
        m_CurrentRow = NULL;
    }
}
