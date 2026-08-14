/**
 *  数据库事务的 RAII 守卫 CDbTransaction：构造时开启事务，析构时若既没提交也没显式回滚
 *  就自动回滚，把「中途 return 忘了回滚」这一类错误从源头挡掉。
 *
 *  典型用法（错误路径直接 return 即可，不必也不该手写 ROLLBACK）：
 *
 *      std::unique_ptr<IDatabase> pConn(DbFactory::createConnection());
 *      if (!pConn)
 *          return false;
 *
 *      CDbTransaction transaction(pConn.get());
 *      if (!transaction.IsActive())
 *          return false;                       //事务没开起来，直接失败
 *
 *      if (!pConn->execute(sqlDelete))
 *          return false;                       //析构时自动回滚
 *
 *      if (!pConn->execute(sqlInsert))
 *          return false;                       //析构时自动回滚
 *
 *      if (!transaction.Commit())
 *          return false;                       //提交失败，析构时补一次回滚
 *
 *      return true;
 *
 *  守卫只负责事务的开启与了结，不接管连接的生命周期：连接由调用方（通常是一个
 *  std::unique_ptr<IDatabase>）持有，且<u>必须比守卫活得久</u>，否则析构时会访问到
 *  已经销毁的连接对象。按上面的写法把连接声明在守卫之前，析构顺序自然就是对的。
 *
 *  balloonwj@qq.com   2026-08-14
 */
#pragma once

#include "IDatabase.h"
#include "../base/AsyncLog.h"

/**
 *  数据库事务守卫。一个对象对应一个事务，不可拷贝、不可赋值，也不支持嵌套 ——
 *  同一条连接上套两层守卫时，内层的 IsActive() 会是 false（IDatabase::beginTransaction
 *  明确禁止嵌套），外层事务不受影响。
 */
class CDbTransaction
{
public:
    /**
     *  构造即开启事务。
     *  @param pConn 数据库连接，所有权<u>不</u>转移，仍归调用方；生命周期必须覆盖本守卫。
     *               传 NULL 是允许的（省得调用方多写一层判断），此时守卫处于未激活状态。
     *  构造函数无法返回失败，所以开启的结果记在内部状态里，**调用方必须紧接着检查
     *  IsActive()**；未激活时不要继续往下发语句，那些语句会各自独立提交。
     */
    explicit CDbTransaction(IDatabase* pConn)
        : m_pConn(pConn)
        , m_bActive(false)
    {
        if (m_pConn == NULL)
        {
            LOGE("db transaction not started, the connection is null");
            return;
        }

        m_bActive = m_pConn->beginTransaction();
    }

    /**
     *  析构。守卫仍持有一个未了结的事务时自动回滚，也就是「构造之后既没成功提交、
     *  也没显式回滚就出了作用域」的情形 —— 中途 return 或抛出异常都会走到这里。
     *  已经提交成功的守卫在析构时什么都不做，不会重复回滚。
     */
    ~CDbTransaction()
    {
        if (!m_bActive)
            return;

        LOGW("db transaction was neither committed nor rolled back explicitly, rolling it back now");
        Rollback();
    }

    /**
     *  本守卫当前是否持有一个未了结的事务。
     *  @return true 表示事务已开启且尚未提交 / 回滚。
     *  构造之后必须检查一次：false 说明事务根本没开起来，此时继续发语句是危险的。
     */
    bool IsActive() const
    {
        return m_bActive;
    }

    /**
     *  提交事务。
     *  @return true 表示提交成功、数据已落库，此后析构不会再回滚。
     *          false 表示提交失败或守卫本就没持有事务；提交失败时守卫<u>仍然持有</u>该事务，
     *          由析构（或调用方显式调用 Rollback）补一次回滚 —— SQLite 上 COMMIT 撞
     *          SQLITE_BUSY 后事务是还开着的，必须回滚才算了结，这一层差异由守卫吸收掉。
     */
    bool Commit()
    {
        if (!m_bActive)
        {
            LOGW("db transaction commit ignored, the guard holds no active transaction");
            return false;
        }

        if (m_pConn->commit())
        {
            m_bActive = false;
            return true;
        }

        //刻意不清除 m_bActive：见本方法注释里对提交失败的说明
        LOGE("db transaction commit failed, the transaction is left for rollback");
        return false;
    }

    /**
     *  显式回滚事务，丢弃本次事务里的全部改动。
     *  @return true 表示 ROLLBACK 确实发出并执行成功。
     *          事务已经被数据库单方面终止（MySQL 事务中途断线、SQLite 出错后自动回滚）时
     *          返回 false 并记一条告警 —— 此时数据同样没有提交，调用方一般不需要区分。
     *  调用之后守卫不再持有事务，析构时不会重复回滚。
     */
    bool Rollback()
    {
        if (!m_bActive)
        {
            LOGW("db transaction rollback ignored, the guard holds no active transaction");
            return false;
        }

        m_bActive = false;

        //连接层可能已经因为断线或数据库自动回滚而丢掉了事务，这时再发一条 ROLLBACK
        //只会换来一条 "no transaction is active" 的错误日志，徒增排查噪音
        if (!m_pConn->inTransaction())
        {
            LOGW("db transaction is already gone before rollback (connection lost or aborted by the database), nothing to roll back");
            return false;
        }

        return m_pConn->rollback();
    }

private:
    //禁止拷贝与赋值：一个事务只能有一个负责了结它的守卫，复制一份必然导致重复回滚
    CDbTransaction(const CDbTransaction&);
    CDbTransaction& operator=(const CDbTransaction&);

    IDatabase*  m_pConn;    //事务所在的连接，所有权归调用方；生命周期须覆盖本守卫，构造后不再变更
    bool        m_bActive;  //本守卫是否仍持有一个未了结的事务；提交成功或显式回滚后置 false
};
