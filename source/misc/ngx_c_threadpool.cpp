#include <unistd.h>
#include <pthread.h>

#include "ngx_c_threadpool.h"
#include "ngx_c_memory.h"
#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_global.h"

//初始化静态成员变量
pthread_mutex_t CThreadPool::m_pthreadMutex = PTHREAD_MUTEX_INITIALIZER;  //#define PTHREAD_MUTEX_INITIALIZER ((pthread_mutex_t) -1)
pthread_cond_t CThreadPool::m_pthreadCond = PTHREAD_COND_INITIALIZER;     //#define PTHREAD_COND_INITIALIZER ((pthread_cond_t) -1)
bool CThreadPool::m_shutdown = false;    //刚开始标记整个线程池的线程是不退出的      

CThreadPool::CThreadPool()
{
    m_iRunningThreadNum = 0;
    m_iLastEmgTime = 0;

    m_iRecvMsgQueueCount = 0;
}

CThreadPool::~CThreadPool()
{
    //资源释放在StopAll()里
    clearMsgRecvQueue(); 
}

void CThreadPool::clearMsgRecvQueue()
{
	char* sTmpMempoint;
	CMemory* p_memory = CMemory::GetInstance();

	//尾声阶段，不需要互斥，该退的都退出了，该停止的都停止了，应该不需要退出了
	while (!m_MsgRecvQueue.empty())
	{
		sTmpMempoint = m_MsgRecvQueue.front();
		m_MsgRecvQueue.pop_front();
		p_memory->FreeMemory(sTmpMempoint);
	}
}

bool CThreadPool::Create(int threadNum)
{
	m_iThreadNum = threadNum;

	int err;
	ThreadItem* pNew;

	for (int i = 0; i < threadNum; ++i)
	{
		pNew = new ThreadItem(this);
		m_threadVector.push_back(pNew);
		err = pthread_create(&pNew->_Handle, nullptr, this->ThreadFunc, pNew); //出错的话不会返回到errno里，而是通过返回值来说明，意思跟errno其实是一样的
		if (err != 0)
		{
			ngx_log_core(NGX_LOG_ERROR, err, "CThreadPool::Create()创建线程%d失败，返回的错误码为%d!", i, err);
			return false;
		}
		else
		{
			//创建线程成功，可以考虑打个日志
		}
	}

	//创建线程后，必须保证线程运行正常，也就是全部卡在pthread_cond_wait()这一行,那么Create函数才能返回，否则等待。
	std::vector<ThreadItem*>::iterator iter;
lblfor:
	for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if ((*iter)->ifrunning == false) //这个条件保证所有线程完全启动起来，以保证整个线程池中的线程正常工作；
		{
			//这说明有没有启动完全的线程
			usleep(100 * 1000);  //100毫秒
			goto lblfor;
		}
	}
	return true;
}

/*这是静态成员函数*/
void* CThreadPool::ThreadFunc(void* threadData)
{
	ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
	CThreadPool* pThreadPoolObj = pThread->_pThis;

	CMemory* p_memory = CMemory::GetInstance();
	int err;

	pthread_t tid = pthread_self();
	while (true)
	{
		err = pthread_mutex_lock(&m_pthreadMutex);
		if (err != 0)
		{
			ngx_log_core(NGX_LOG_ERROR,err, "CThreadPool::ThreadFunc()中pthread_mutex_lock()失败，返回的错误码为%d!", err);
		}

		/*循环，防止虚假唤醒*/
		/*这里size的复杂度是O(N)*/
		while ((pThreadPoolObj->m_MsgRecvQueue.size() == 0) && m_shutdown != false)
		{
			if (pThread->ifrunning == false)
			{
				pThread->ifrunning = true;
			}

			pthread_cond_wait(&m_pthreadCond, &m_pthreadMutex);
		}

		if (m_shutdown == true)
		{
			pthread_mutex_unlock(&m_pthreadMutex);
			break;
		}

		/*开始处理消息, 目前互斥锁还锁着*/
		char* jobBuf = pThreadPoolObj->m_MsgRecvQueue.front();
		pThreadPoolObj->m_MsgRecvQueue.pop_front();
		--pThreadPoolObj->m_iRecvMsgQueueCount;

		err = pthread_mutex_unlock(&m_pthreadMutex); /*把数据从消息队列中拿掉就能解锁了，因为锁是用来锁消息队列的*/
		if (err != 0)
		{
			ngx_log_stderr(err, "CThreadPool::ThreadFunc()中pthread_mutex_unlock()失败，返回的错误码为%d!", err);//有问题，要及时报告
		}

		++pThreadPoolObj->m_iRunningThreadNum;

		/*真正处理消息的函数*/
		g_socket.threadRecvProcFunc(jobBuf);

		/*消息处理完了，*/
		p_memory->FreeMemory(jobBuf);
		--pThreadPoolObj->m_iRunningThreadNum;
	}

	return nullptr;
}