#include <string.h>
#include <pthread.h>

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

void CSocket::ngx_free_connection(lpngx_connection_t pConn)
{
	//因为有线程可能要动连接池中连接，所以在合理互斥也是必要的
	//CLock lock(&m_connectionMutex);

	pConn->PutOneToFree();

	m_freeConnectionList.push_back(pConn);
	++m_free_connection_n;

	return;
}