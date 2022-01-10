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
			//可以考虑打个日志，
		}
	}
	ngx_log_core(NGX_LOG_INFO, 0, "CThreadPool::Create()创建线程成功, 总数为%d", threadNum);
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

void CThreadPool::StopAll()
{
	if (m_shutdown == true)
	{
		return;
	}
	m_shutdown = true;

	int err = pthread_cond_broadcast(&m_pthreadCond);
	if (err != 0)
	{
		ngx_log_core(NGX_LOG_CRITICAL, err, "CThreadPool::StopAll()中pthread_cond_broadcast()失败，返回的错误码为%d!", err);
		return;
	}

	std::vector< ThreadItem*>::iterator iter;
	for (iter = m_threadVector.begin(); iter != m_threadVector.begin(); ++iter)
	{
		pthread_join((*iter)->_Handle, nullptr);
	}

	pthread_mutex_destroy(&m_pthreadMutex);
	pthread_cond_destroy(&m_pthreadCond);

	for (iter = m_threadVector.begin(); iter != m_threadVector.begin(); ++iter)
	{
		if (*iter != nullptr)
		{
			delete (*iter);
		}
	}
	m_threadVector.clear();

	ngx_log_core(NGX_LOG_INFO, 0, "CThreadPool::StopAll()成功返回，线程池中线程全部正常结束!");
	return;
}

/*消息队列入队*/
void CThreadPool::inMsgRecvQueueAndSignan(char* buf)
{
	int err = pthread_mutex_lock(&m_pthreadMutex);
	if (err != 0)
	{
		ngx_log_core(NGX_LOG_ERROR, err, "inMsgRecvQueueAndSignan()中，pthread_mutex_lock出错");
	}
	m_MsgRecvQueue.push_back(buf);
	++m_iRecvMsgQueueCount;

	err = pthread_mutex_unlock(&m_pthreadMutex);
	if (err != 0)
	{
		ngx_log_core(NGX_LOG_ERROR, err, "inMsgRecvQueueAndSignan()中，pthread_mutex_lock出错");
	}

	Call();
	return;
}

void CThreadPool::Call()
{
	int err = pthread_cond_signal(&m_pthreadCond);
	if (err != 0)
	{
		ngx_log_core(NGX_LOG_ERROR, err, "inMsgRecvQueueAndSignan()中，pthread_mutex_lock出错");
	}

	if (m_iThreadNum == m_iRunningThreadNum) /*如果忙线程数量达到上限*/
	{
		time_t currtime = time(nullptr); /*返回单位：秒*/
		if (currtime - m_iLastEmgTime > 10) //最少间隔10秒钟才报一次线程池中线程不够用的问题；
		{
			//两次报告之间的间隔必须超过10秒，不然如果一直出现当前工作线程全忙，但频繁报告日志也够烦的
			m_iLastEmgTime = currtime;  //更新时间
			//写日志，通知这种紧急情况给用户，用户要考虑增加线程池中线程数量了
			ngx_log_core(NGX_LOG_EMERGENCY, 0, "CThreadPool::Call()中发现线程池中当前空闲线程数量为0，要考虑扩容线程池了!");
		}
	}
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
		while ((pThreadPoolObj->m_MsgRecvQueue.size() == 0) && m_shutdown == false)
		{
			if (pThread->ifrunning == false)
			{
				pThread->ifrunning = true;
			}
			ngx_log_core(NGX_LOG_DEBUG, 0, "ThreadFunc中线程启动成功, 线程id为:%ud",tid);
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