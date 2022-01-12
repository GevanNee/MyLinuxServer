#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <vector>
#include "ngx_c_socket.h"
#include "ngx_comm.h"
#include "ngx_func.h"
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

ngx_connection_s::ngx_connection_s()
{
	iCurrSequence = 0;
	pthread_mutex_init(&logicPorcMutex, nullptr);
}

ngx_connection_s::~ngx_connection_s()
{
	pthread_mutex_destroy(&logicPorcMutex);
}
 

void ngx_connection_s::GetOneToUse()
{
	++iCurrSequence;

	fd = -1;											//开始先给-1
	curStat =				_PKG_HD_INIT;				//收包状态处于 初始状态，准备接收数据包头【状态机】
	precvbuf =				dataHeadInfo;				//收包我要先收到这里来，因为我要先收包头，所以收数据的buff直接就是dataHeadInfo
	irecvlen =				sizeof(COMM_PKG_HEADER);	//这里指定收数据的长度，这里先要求收包头这么长字节的数据

	precvMemPointer =		nullptr;					//既然没new内存，那自然指向的内存地址先给NULL
	iThrowSendCount =		0;							//原子的
	psendMemPointer =		nullptr;					//发送数据头指针记录
	events = 0;											//epoll事件先给0 
	lastPingTime =			time(nullptr);				//上次ping的时间

	FloodkickLastTime =		0;							//Flood攻击上次收到包的时间
	FloodAttackCount =		0;							//Flood攻击在该时间内收到包的次数统计
	iSendCount =			0;							//发送队列中有的数据条目数，若client只发不收，则可能造成此数过大，依据此数做出踢出处理
}

void ngx_connection_s::PutOneToFree()
{
	++iCurrSequence;
	if (precvMemPointer != nullptr)
	{
		CMemory::GetInstance()->FreeMemory(precvMemPointer);
		precvMemPointer = nullptr;
	}

	if (psendMemPointer != nullptr)
	{
		CMemory::GetInstance()->FreeMemory(psendMemPointer);
		psendMemPointer = nullptr;
	}

	iThrowSendCount = 0;
	return;
}

/*从空闲连接池里拿出一个连接跟isock绑定*/
lpngx_connection_t CSocket::ngx_get_connection(int isock)
{
	ngx_log_core(NGX_LOG_DEBUG, 0, "开始CSocket::ngx_get_connection()");

	CLock lock(&m_connectionMutex);
	
	if (!m_freeConnectionList.empty())
	{
		lpngx_connection_t pConn = m_freeConnectionList.front();
		m_freeConnectionList.pop_front();
		pConn->GetOneToUse();
		--m_free_connection_n;
		pConn->fd = isock;
		return pConn;
	}
	
	/*走到这里就是没空闲连接*/
	CMemory* p_memory = CMemory::GetInstance();
	lpngx_connection_t pConn = (lpngx_connection_t)p_memory->AllocMemory(sizeof(ngx_connection_t), true);
	pConn = new(pConn) ngx_connection_t;
	m_connectionList.push_back(pConn);
	++m_total_connection_n;
	pConn->fd = isock;

	return pConn;
}

void CSocket::initconnection()
{
	lpngx_connection_t p_Conn;
	CMemory* p_memory = CMemory::GetInstance();

	int ilenConnect = sizeof(ngx_connection_t);

	for (int i = 0; i < m_worker_connection; ++i)
	{
		p_Conn = (lpngx_connection_t)p_memory->AllocMemory(ilenConnect, true);
		
		p_Conn = new(p_Conn) ngx_connection_t;
		p_Conn->GetOneToUse();
		m_connectionList.push_back(p_Conn);
		m_freeConnectionList.push_back(p_Conn);
	}

	m_free_connection_n = m_free_connection_n = m_connectionList.size();
}

void CSocket::ngx_close_connection(lpngx_connection_t pConn)
{
	ngx_free_connection(pConn);
}

//最终回收连接池，释放内存
void CSocket::clearconnection()
{
	lpngx_connection_t p_Conn;
	CMemory* p_memory = CMemory::GetInstance();
	m_connectionList.empty();
	while (!m_connectionList.empty())
	{
		p_Conn = m_connectionList.front();
		m_connectionList.pop_front();
		p_Conn->~ngx_connection_t();     //手工调用析构函数
		p_memory->FreeMemory(p_Conn);
	}
}


void CSocket::ngx_free_connection(lpngx_connection_t pConn)
{
	ngx_log_core(NGX_LOG_DEBUG, 0, "ngx_free_connection()回收了一个连接");
	//因为有线程可能要动连接池中连接，所以在合理互斥也是必要的
	CLock lock(&m_connectionMutex);

	pConn->PutOneToFree();

	m_freeConnectionList.push_back(pConn);
	++m_free_connection_n;

	return;
}

/*将要回收的连接放到一个队列中来，用一个专门的线程来处理这个队列中的连接，保证服务器稳定*/
void CSocket::inRecyConnectQueue(lpngx_connection_t pConn)
{
	std::list<lpngx_connection_t>::iterator pos;
	bool ifFind = false;

	CLock lock(&m_recyconnqueueMutex);

	/*防止该连接被多次扔到回收站里来*/
	for (pos = m_recyconnectionList.begin(); pos != m_recyconnectionList.end(); ++pos)
	{
		if ((*pos) == pConn)
		{
			ifFind = true;
			break;
		}
	}

	if (ifFind == true)
	{
		return;
	}

	pConn->inRecyTime = time(nullptr);		//单位：秒
	++pConn->iCurrSequence;
	m_recyconnectionList.push_back(pConn);
	++m_totol_recyconnection_n;				//待释放连接队列大小
	--m_onlineUserCount;					//连入用户数量

	return;
} 

/*处理连接回收的线程*/
void* CSocket::ServerRecyConnectionThread(void* threadData)
{
	ngx_log_core(NGX_LOG_DEBUG, 0, "回收连接线程启动成功，线程id是%ud", (unsigned long)pthread_self());

	ThreadItem* pThread = static_cast<ThreadItem*>(threadData);
	CSocket* soketObject = pThread->_pThis;

	time_t currTime;
	int err;
	lpngx_connection_t p_Conn;

	while (1)
	{
		usleep(200 * 1000);

		if(soketObject->m_totol_recyconnection_n > 0)
		{
			currTime = time(nullptr);
			err = pthread_mutex_lock(&soketObject->m_recyconnqueueMutex);
			if (err != 0)
			{
				ngx_log_core(NGX_LOG_ERROR, err, "CSocket::ServerRecyConnectionThread()中，pthread_mutex_lock失败");
			}

lblRRTD: //这个循环改了好几版，还是用goto最方便
			/*从头开始循环吧*/
			auto pos = soketObject->m_recyconnectionList.begin();
			auto posend = soketObject->m_recyconnectionList.end();

			for (; pos != posend; ++pos)
			{
				/*说明有数据*/
				p_Conn = (*pos);
				if ((p_Conn->inRecyTime + soketObject->m_RecyConnectionWaitTime) > currTime && g_stopEvent == 0)
				{
					continue;
				}

				/*这个条件一般不会成立*/
				if (p_Conn->iThrowSendCount > 0)
				{
					/*不知道为什么成立，打个日志提醒下*/
					ngx_log_core(NGX_LOG_ERROR, 0, "CSocekt::ServerRecyConnectionThread()中到释放时间却发现p_Conn.iThrowsendCount!=0，这个不该发生");
				}

				--soketObject->m_totol_recyconnection_n;        //待释放连接队列大小-1
				soketObject->m_recyconnectionList.erase(pos);   //迭代器已经失效，但pos所指内容在p_Conn里保存着呢
				soketObject->ngx_free_connection(p_Conn);	   //归还参数pConn所代表的连接到到连接池中

				goto lblRRTD;
			} //end of for
			err = pthread_mutex_unlock(&soketObject->m_recyconnqueueMutex);
			if (err != 0)
			{
				ngx_log_core(NGX_LOG_ERROR, err, "CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!", err);
			}
		}//end of if

		if (g_stopEvent == 1)
		{
			if (soketObject->m_totol_recyconnection_n > 0)
			{
				err = pthread_mutex_lock(&soketObject->m_recyconnqueueMutex);
				if (err != 0)
				{
					ngx_log_core(NGX_LOG_ERROR, err, "CSocekt::ServerRecyConnectionThread()pthread_mutex_lock()失败，返回的错误码为%d!", err);
				}

lblRRTD2: //这个循环改了好几版，还是用goto最方便
				auto pos = soketObject->m_recyconnectionList.begin();
				auto posend = soketObject->m_recyconnectionList.end();

				for (; pos != posend; ++pos)
				{
					/*说明有数据*/
					p_Conn = (*pos);
					--soketObject->m_totol_recyconnection_n;        //待释放连接队列大小-1
					soketObject->m_recyconnectionList.erase(pos);   //迭代器已经失效，但pos所指内容在p_Conn里保存着呢
					soketObject->ngx_free_connection(p_Conn);	   //归还参数pConn所代表的连接到到连接池中
					goto lblRRTD2;
				} //end of for
				err = pthread_mutex_unlock(&soketObject->m_recyconnqueueMutex);
				if (err != 0)
				{
					ngx_log_core(NGX_LOG_ERROR, err, "CSocekt::ServerRecyConnectionThread()pthread_mutex_unlock()失败，返回的错误码为%d!", err);
				}
			}

			break;
		}
	}

	return nullptr;
}
