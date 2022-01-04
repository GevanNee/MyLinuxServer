#pragma once

#include <vector>
#include <pthread.h>
#include <atomic>
#include <list>

class CThreadPool
{
public:
	CThreadPool();
	~CThreadPool();

public:
	bool Create(int threadNum); 
	void StopAll(); //线程池中的所有线程退出

	void inMsgRecvQueueAndSignan(char* buf); //消息队列入队
	void Call(); //唤醒一个线程
	int getRecvMsgQueueCount() { return m_iRecvMsgQueueCount; } //获取接收消息队列的大小

private:
	static void* ThreadFunc(void* threadData); //新线程的线程函数
	//char* outMsgRecvQueue();
	void clearMsgRecvQueue(); //清空接收消息的队列

private:
	//定义一个 线程池中的 线程 的结构，以后可能做一些统计之类的 功能扩展，所以引入这么个结构来 代表线程 感觉更方便一些；
	struct ThreadItem
	{
		pthread_t _Handle;		//线程句柄
		CThreadPool* _pThis;	//记录线程池的指针
		bool        ifrunning;	//标记是否正式启动起来

		ThreadItem(CThreadPool* p_this) :_pThis(p_this), ifrunning(false) {}
		~ThreadItem(){}
	};

private:
	static pthread_mutex_t		m_pthreadMutex;	//线程同步的互斥量
	static pthread_cond_t		m_pthreadCond;	//线程同步的条件变量
	static bool					m_shutdown;		//线程退出标志

	int							m_iThreadNum;	//要创建的线程数量

	std::atomic<int>			m_iRunningThreadNum;	//运行中的线程数量, 原子
	time_t						m_iLastEmgTime;			//上次发生线程不够用的时间

	std::vector<ThreadItem*>	m_threadVector;

	//接收消息队列相关
	std::list<char*>			m_MsgRecvQueue;		//接收数据的消息队列
	int							m_iRecvMsgQueueCount;	//收消息的队列大小
};