#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/time.h>

#include "ngx_comm.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_c_memory.h"
#include "ngx_c_lockmutex.h"

CSocket::CSocket()
{
	m_worker_connection = 1;
	m_ListenPortCount = 1;
	m_epollhandle = -1;

	m_iLenPkgHeader = sizeof(COMM_PKG_HEADER);
	m_iLenMsgHeader = sizeof(STRUC_MSG_HEADER);
}

CSocket::~CSocket()
{

}


/*应该在main函数中调用该函数*/
bool CSocket::Initialize()
{
	ngx_log_core(NGX_LOG_DEBUG, 0, "进入CSocket::Initialize()");
	this->ReadConf();

	bool reco = ngx_open_listening_sockets();

	return reco;
}


bool CSocket::Initialize_subprocess()
{
	//发消息互斥量初始化
	if (pthread_mutex_init(&m_sendMessageQueueMutex, NULL) != 0)
	{
		ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_mutex_init(&m_sendMessageQueueMutex)失败.");
		return false;
	}

	//连接相关互斥量初始化
	if (pthread_mutex_init(&m_connectionMutex, NULL) != 0)
	{
		ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_mutex_init(&m_connectionMutex)失败.");
		return false;
	}

	//连接回收队列相关互斥量初始化
	if (pthread_mutex_init(&m_recyconnqueueMutex, NULL) != 0)
	{
		ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_mutex_init(&m_recyconnqueueMutex)失败.");
		return false;
	}
	
	//和时间处理队列有关的互斥量初始化
	if (pthread_mutex_init(&m_timequeueMutex, NULL) != 0)
	{
		ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_mutex_init(&m_timequeueMutex)失败.");
		return false;
	}

	//初始化发消息相关信号量，信号量用于进程/线程 之间的同步，虽然 互斥量[pthread_mutex_lock]和 条件变量[pthread_cond_wait]都是线程之间的同步手段，但
	//这里用信号量实现 则 更容易理解，更容易简化问题，使用书写的代码短小且清晰；
	//第二个参数=0，表示信号量在线程之间共享，确实如此 ，如果非0，表示在进程之间共享
	//第三个参数=0，表示信号量的初始值，为0时，调用sem_wait()就会卡在那里卡着
	if (sem_init(&m_semEventSendQueue, 0, 0) == -1)
	{
		ngx_log_stderr(0, "CSocekt::Initialize_subproc()中sem_init(&m_semEventSendQueue,0,0)失败.");
		return false;
	}

	//创建线程
	int err;
	ThreadItem* pSendQueue = new ThreadItem(this);    //专门用来发送数据的线程
	m_threadVector.push_back(pSendQueue);                         //创建 一个新线程对象 并入到容器中 
	err = pthread_create(&pSendQueue->_Handle, NULL, ServerSendQueueThread, pSendQueue); //创建线程，错误不返回到errno，一般返回错误码
	if (err != 0)
	{
		ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_create(ServerSendQueueThread)失败.");
		return false;
	}

	//---
	ThreadItem* pRecyconn = new ThreadItem(this);    //专门用来回收连接的线程
	m_threadVector.push_back(pRecyconn);
	err = pthread_create(&pRecyconn->_Handle, NULL, ServerRecyConnectionThread, pRecyconn);
	if (err != 0)
	{
		ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_create(ServerRecyConnectionThread)失败.");
		return false;
	}

	if (m_ifkickTimeCount == 1)  //是否开启踢人时钟，1：开启   0：不开启
	{
		ThreadItem* pTimemonitor = new ThreadItem(this);    //专门用来处理到期不发心跳包的用户踢出的线程
		m_threadVector.push_back(pTimemonitor);
		err = pthread_create(&pTimemonitor->_Handle, NULL, ServerTimerQueueMonitorThread, pTimemonitor);
		if (err != 0)
		{
			ngx_log_stderr(0, "CSocekt::Initialize_subproc()中pthread_create(ServerTimerQueueMonitorThread)失败.");
			return false;
		}
	}

	return true;
}



void CSocket::Shutdown_subproc()
{
	if (sem_post(&m_semEventSendQueue) == -1)  //让ServerSendQueueThread()流程走下来干活
	{
		ngx_log_stderr(0, "CSocekt::Shutdown_subproc()中sem_post(&m_semEventSendQueue)失败.");
	}

	std::vector<ThreadItem*>::iterator iter;
	for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		pthread_join((*iter)->_Handle, NULL); //等待一个线程终止
	}
	//(2)释放一下new出来的ThreadItem【线程池中的线程】    
	for (iter = m_threadVector.begin(); iter != m_threadVector.end(); iter++)
	{
		if (*iter)
			delete* iter;
	}
	m_threadVector.clear();

	//(3)队列相关
	clearMsgSendQueue();
	clearconnection();
	clearAllFromTimerQueue();

	//(4)多线程相关    
	pthread_mutex_destroy(&m_connectionMutex);          //连接相关互斥量释放
	pthread_mutex_destroy(&m_sendMessageQueueMutex);    //发消息互斥量释放    
	pthread_mutex_destroy(&m_recyconnqueueMutex);       //连接回收队列相关的互斥量释放
	pthread_mutex_destroy(&m_timequeueMutex);           //时间处理队列相关的互斥量释放
	sem_destroy(&m_semEventSendQueue);                  //发消息相关线程信号量释放

}

void CSocket::ReadConf()
{
	ngx_c_conf* p_config = ngx_c_conf::getInstance();
	m_worker_connection = p_config->getInt("worker_connections", m_worker_connection);              //epoll连接的最大项数
	m_ListenPortCount = p_config->getInt("ListenPortCount", m_ListenPortCount);                    //取得要监听的端口数量
	m_RecyConnectionWaitTime = p_config->getInt("Sock_RecyConnectionWaitTime", m_RecyConnectionWaitTime); //等待这么些秒后才回收连接

	m_ifkickTimeCount = p_config->getInt("Sock_WaitTimeEnable", 0);                                //是否开启踢人时钟，1：开启   0：不开启
	m_iWaitTime = p_config->getInt("Sock_MaxWaitTime", m_iWaitTime);                         //多少秒检测一次是否 心跳超时，只有当Sock_WaitTimeEnable = 1时，本项才有用	
	m_iWaitTime = (m_iWaitTime > 5) ? m_iWaitTime : 5;                                                 //不建议低于5秒钟，因为无需太频繁
	m_ifTimeOutKick = p_config->getInt("Sock_TimeOutKick", 0);                                   //当时间到达Sock_MaxWaitTime指定的时间时，直接把客户端踢出去，只有当Sock_WaitTimeEnable = 1时，本项才有用 

	m_floodAkEnable = p_config->getInt("Sock_FloodAttackKickEnable", 0);                          //Flood攻击检测是否开启,1：开启   0：不开启
	m_floodTimeInterval = p_config->getInt("Sock_FloodTimeInterval", 100);                            //表示每次收到数据包的时间间隔是100(毫秒)
	m_floodKickCount = p_config->getInt("Sock_FloodKickCounter", 10);                              //累积多少次踢出此人

	return;
}

bool CSocket::ngx_open_listening_sockets()
{
	ngx_log_core(NGX_LOG_DEBUG, 0, "进入CSocket::ngx_open_listening_sockets()");

	int isocket;
	struct sockaddr_in	server_addr; //本服务器的地址结构体
	int iport;
	char strinfo[100]; //配置文件的具体端口号。

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	ngx_c_conf* p_config = ngx_c_conf::getInstance();
	for (int i = 0; i < m_ListenPortCount; ++i)
	{
		isocket = socket(AF_INET, SOCK_STREAM, 0);

		if (isocket == -1)
		{
			ngx_log_core(NGX_LOG_CRITICAL, errno, "CSocket::Initialize()中socket()失败");
			return false;
		}

		int reuseaddr = 1;
		if (setsockopt(isocket, SOL_SOCKET, SO_REUSEADDR, (const void*)&reuseaddr, sizeof(reuseaddr)) == -1)
		{
			ngx_log_core(NGX_LOG_ERROR, errno, "CSocket::ngx_open_listening_sockets()中setsockopt()失败");
			close(isocket);
			return false;
		}

		/*reuseport*/

		if (set_nonblocking(isocket) == false)
		{
			ngx_log_core(NGX_LOG_ERROR, errno, "CSocket::ngx_open_listening_sockets()中set_nonblocking()失败");
			return false;
		}

		//strinfo[0] = 0;
		sprintf(strinfo, "ListenPort%d", i);
		iport = p_config->getInt(strinfo, iport);
		server_addr.sin_port = htons((in_port_t)iport);

		if (bind(isocket, (const struct sockaddr*)&server_addr, sizeof(struct sockaddr)) == -1)
		{
			ngx_log_core(NGX_LOG_CRITICAL, errno, "CSocket::Initialize()中bind()失败");
			return false;
		}

		if (listen(isocket, NGX_LISTEN_BACKLOG) == -1)
		{
			ngx_log_core(NGX_LOG_CRITICAL, errno, "CSocket::Initialize()中listen()失败");
			return false;
		}

		lpngx_listening_t p_listenSocketItem = new ngx_listening_t;
		memset(p_listenSocketItem, 0, sizeof(ngx_listening_t));

		p_listenSocketItem->fd = isocket;
		p_listenSocketItem->port = iport;

		ngx_log_core(NGX_LOG_INFO, 0, "监听%d端口成功!", iport);
		m_ListenSocketList.push_back(p_listenSocketItem);
	}

	if (m_ListenSocketList.size() <= 0)
	{
		return false;
	}
	return true;
}

void CSocket::ngx_close_listening_sockets()
{
	for (int i = 0; i < m_ListenPortCount; i++) //要关闭这么多个监听端口
	{
		close(m_ListenSocketList[i]->fd);
		ngx_log_core(NGX_LOG_INFO, 0, "关闭监听端口%d!", m_ListenSocketList[i]->port); //显示一些信息到日志中
	}//end for(int i = 0; i < m_ListenPortCount; i++)
	return;
}

bool CSocket::set_nonblocking(int sockfd)
{
	int nb = 1; //0：清除，1：设置
	if (ioctl(sockfd, FIONBIO, &nb) == -1)
	{
		return false;
	}
	return true;

	/*用fctl也可以*/
}

int CSocket::ngx_epoll_init()
{
	ngx_log_core(NGX_LOG_DEBUG, 0, "开始CSocket::ngx_epoll_init()");

	/*(1)创建epoll对象*/
	m_epollhandle = epoll_create(m_worker_connection);
	if (m_epollhandle == -1)
	{
		ngx_log_core(NGX_LOG_CRITICAL, errno, "CSocket::ngx_epoll_init()中epoll_create()失败");
		ngx_log_stderr(errno, "CSocket::ngx_epoll_init()中epoll_create()失败");
		exit(2);
	}
	ngx_log_core(NGX_LOG_DEBUG, 0, "CSocket::ngx_epoll_init()中创建epoll对象成功, 句柄值是%d", m_epollhandle);


	/*(2)创建连接池【数组】*/
	
	initconnection();

	ngx_log_core(NGX_LOG_DEBUG, 0, "CSocket::ngx_epoll_init()中连接池初始化完毕");
	ngx_log_core(NGX_LOG_DEBUG, 0, "m_ListenSocketList的大小是:%d", m_ListenSocketList.size());

	/*(3)把用于监听的socket添加到连接池中*/
	std::vector<lpngx_listening_t>::iterator pos;
	for (pos = m_ListenSocketList.begin(); pos != m_ListenSocketList.end(); pos++)
	{
		lpngx_connection_t p_Conn = ngx_get_connection((*pos)->fd);
		if (p_Conn == nullptr)
		{
			ngx_log_stderr(errno, "CSocekt::ngx_epoll_init()中ngx_get_connection()失败.");
			ngx_log_core(NGX_LOG_CRITICAL, errno, "CSocekt::ngx_epoll_init()中ngx_get_connection()失败.");
			exit(2);
		}

		p_Conn->listening = (*pos); //把连接对象和监听对象关联
		(*pos)->connection = p_Conn;

		/*负责监听的端口的读事件的处理方法*/
		p_Conn->r_handler = &CSocket::ngx_event_accept;

		/*往监听socket上添加事件*/
		if (ngx_epoll_oper_event((*pos)->fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLHUP, 0, p_Conn) == -1)
		{
			exit(2); //日志已经在ngx_epoll_oper_event里写过了
		}
	}

	ngx_log_core(NGX_LOG_DEBUG, 0, "CSocket::ngx_epoll_init()结束");
	return 0;
}

/*返回值：1 正常， 0 不正常*/
int CSocket::ngx_epoll_process_events(int timer)
{	
		int event_num = epoll_wait(m_epollhandle, m_events, NGX_MAX_EVENTS, timer);

		if (event_num == -1)
		{
			if (errno == EINTR)
			{
				/*信号所致，直接返回*/
				ngx_log_core(NGX_LOG_INFO, errno, "CSocket::ngx_epoll_process_events()中epoll_wait()失败!");
				return 1; //可以认为正常返回
			}
			else
			{
				ngx_log_core(NGX_LOG_INFO, errno, "CSocket::ngx_epoll_process_events()中epoll_wait()失败!");
				return 0; /*不正常*/
			}
		}

		if (event_num == 0) /*超时了但是没有事件过来*/
		{
			if (timer != -1)
			{
				/*说明阻塞时见到了，正常返回*/
				return 1;
			}

			ngx_log_core(NGX_LOG_ALERT, 0, "CSocekt::ngx_epoll_process_events()中epoll_wait()没超时却没返回任何事件!");
			return 0; /*有什么操作打断了阻塞，不正常*/
		}

		/*惊群测试*/
		//ngx_log_stderr(errno, "惊群测试1:%d", event_num);
		ngx_log_stderr(errno, "event_num = %d，fd = %d", event_num, ((lpngx_connection_t)m_events[0].data.ptr)->fd);
		
		lpngx_connection_t	p_Connect;
		uint32_t			revents;

		for (int i = 0; i < event_num; ++i)
		{
			p_Connect = (lpngx_connection_t)m_events[i].data.ptr;

			revents = m_events[i].events;

			if (p_Connect->fd == -1)
			{
				continue;
			}

			if (revents & EPOLLIN)
			{
				(this->*(p_Connect->r_handler) )(p_Connect);
			}

			if (revents & EPOLLOUT) //客户端关闭也可能触发这个事件
			{
				ngx_log_core(NGX_LOG_DEBUG, 0, "epoll_wait()检测到了写事件");
				if (revents & (EPOLLERR | EPOLLHUP | EPOLLRDHUP))
				{
					//EPOLLERR：对应的连接发生错误                     8     = 1000 
					//EPOLLHUP：对应的连接被挂起                       16    = 0001 0000
					//EPOLLRDHUP：表示TCP连接的远端关闭或者半关闭连接   8192   = 0010  0000   0000   0000
					
				}
				else //这里说明数据没有发送完毕
				{
					(this->*(p_Connect->w_handler))(p_Connect);
				}
			}
		} /*end of for()*/

	return 1; //1表示成功
}

int CSocket::ngx_epoll_oper_event(	int fd,
									uint32_t eventtype, 
									uint32_t flag, 
									int bcaction,  //第二个参数为EPOLL_CTL_MOD时这个参数才有用，0，表示flag增加，1表示flag去掉，2表示flag覆盖
									lpngx_connection_t pConn)
{
	struct epoll_event ev;
	memset(&ev, 0, sizeof(struct epoll_event));

	if (eventtype == EPOLL_CTL_ADD)
	{
		ev.data.ptr = (void*)pConn;
		ev.events = eventtype;
	}
	else if (eventtype == EPOLL_CTL_MOD)
	{
		ngx_log_core(NGX_LOG_DEBUG, 0, "CSocket::ngx_epoll_oper_event()中 eventtype == EPOLL_CTL_MOD");

		ev.events = pConn->events;
		switch (bcaction)
		{
		case 0:
			ev.events |= flag; //增加某个标记
			break;
		case 1:
			ev.events &= ~flag; //去掉某个标记
			break;
		case 2:
			ev.events = flag; //完全覆盖
			break;
		default:
			return -1;
		}
		pConn->events = ev.events; 
	}
	else /*EPOLL_CTL_DEL*/
	{
		/*可以不用处理，关闭socket会自动删除*/
		return 1; /**/
	}

	/*按理来说，EPOLL_ADD里有这行代码就行了，但是epoll_*/
	ev.data.ptr = (void*)pConn;

	if (epoll_ctl(m_epollhandle, eventtype, fd, &ev) == -1)
	{
		ngx_log_core(NGX_LOG_CRITICAL, errno, "CSocekt::ngx_epoll_oper_event()中epoll_ctl(% d, % ud, % ud, % d)失败.", fd, eventtype, flag, bcaction);
		return -1;
	}
	return 1;
}

/*将一个待发送消息入到发消息队列中*/
void CSocket::msgSend(char* psendbuf)
{
	CMemory* p_memory = CMemory::GetInstance();
	
	CLock lock(&m_sendMessageQueueMutex);

	/*总的发送消息队列太大*/
	if (m_iSendMsgQueueCount > 50000)
	{
		/*直接干掉要发送的数据，虽然对客户端不太友好，但是总比服务器崩溃强*/
		m_iDiscardSendPkgCount++;
		p_memory->FreeMemory(psendbuf);
		return;
	}

	/*检查一下本次发送消息的连接，是否堆积了消息，比如客户端恶意不收包，会导致服务器消息堆积*/
	LPSTRUC_MSG_HEADER pPkgHeader = (LPSTRUC_MSG_HEADER)(psendbuf);
	lpngx_connection_t p_Conn =  pPkgHeader->pConn;

	if (p_Conn->iSendCount > 400)
	{
		m_iDiscardSendPkgCount++;

		p_memory->FreeMemory(psendbuf);
		zdClosesocketProc(p_Conn);

		return;
	}

	++p_Conn->iSendCount;
	m_MsgSendQueue.push_back(psendbuf);
	++m_iSendMsgQueueCount; //++是原子操作

	if (sem_post(&m_semEventSendQueue) == 1)
	{
		ngx_log_core(NGX_LOG_ERROR, errno, "CSocekt::msgSend()中sem_post(&m_semEventSendQueue)失败.");
	}

	return;
}


/*主动关闭一个连接做的善后工作*/
/*多线程调用，但是内部负责互斥*/
void CSocket::zdClosesocketProc(lpngx_connection_t p_Conn)
{
	if (m_ifkickTimeCount == 1)
	{
		DeleteFromTimerQueue(p_Conn); //从时间队列中把连接干掉
	}

	if (p_Conn->fd != -1)
	{
		close(p_Conn->fd);
		p_Conn->fd = -1;
	}
	
	if (p_Conn->iThrowSendCount > 0)
	{
		--p_Conn->iThrowSendCount;   //归0
	}

	ngx_log_core(NGX_LOG_DEBUG, errno, "zdClosesocketProc()关闭了一个连接");

	inRecyConnectQueue(p_Conn);
	return;
}

bool CSocket::TestFlood(lpngx_connection_t pConn)
{
	struct  timeval sCurrTime;   //当前时间结构
	uint64_t        iCurrTime;   //当前时间（单位：毫秒）
	bool  reco = false;

	gettimeofday(&sCurrTime, NULL); //取得当前时间
	iCurrTime = (sCurrTime.tv_sec * 1000 + sCurrTime.tv_usec / 1000);  //毫秒
	if ((iCurrTime - pConn->FloodkickLastTime) < m_floodTimeInterval)   //两次收到包的时间 < 100毫秒
	{
		//发包太频繁记录
		pConn->FloodAttackCount++;
		pConn->FloodkickLastTime = iCurrTime;
	}
	else
	{
		//既然发布不这么频繁，则恢复计数值
		pConn->FloodAttackCount = 0;
		pConn->FloodkickLastTime = iCurrTime;
	}

	//ngx_log_stderr(0,"pConn->FloodAttackCount=%d,m_floodKickCount=%d.",pConn->FloodAttackCount,m_floodKickCount);

	if (pConn->FloodAttackCount >= m_floodKickCount)
	{
		//可以踢此人的标志
		reco = true;
	}
	return reco;
}


/*处理发送消息队列的线程*/
void* CSocket::ServerSendQueueThread(void* threadItem)
{
	ngx_log_core(NGX_LOG_DEBUG, 0, "发送消息线程启动成功，线程id是%ud", (unsigned long)pthread_self());

	ThreadItem* pThread = static_cast<ThreadItem*>(threadItem);
	CSocket* pSocketObject = pThread->_pThis;

	char* pMsgBuf; 
	LPSTRUC_MSG_HEADER pMsgHeader;
	LPCOMM_PKG_HEADER pPkgHeader;
	lpngx_connection_t  p_Conn;

	unsigned short      itmp;
	ssize_t             sendsize;

	CMemory* p_memory = CMemory::GetInstance();

	std::list<char*>::iterator pos, pos2, posend;
	int err;
	while (g_stopEvent == 0)
	{
		if (sem_wait(&pSocketObject->m_semEventSendQueue) == -1)
		{
			
			if (errno != EINTR)
			{
				ngx_log_stderr(errno, "CSocekt::ServerSendQueueThread()中sem_wait(&pSocketObj->m_semEventSendQueue)失败.");
			}
		}

		if (g_stopEvent != 0)
		{
			break;
		}

		if (pSocketObject->m_iSendMsgQueueCount > 0)
		{
			err = pthread_mutex_lock(&pSocketObject->m_sendMessageQueueMutex); //因为我们要操作发送消息对列m_MsgSendQueue，所以这里要临界            
			if (err != 0)
			{
				ngx_log_core(NGX_LOG_ERROR, err, "CThreadPool::ThreadFunc()中pthread_mutex_lock()失败，返回的错误码为%d!", err);
			}
			
			for (auto pos = pSocketObject->m_MsgSendQueue.begin(); pos != pSocketObject->m_MsgSendQueue.end(); /*循环内部控制pos++*/ );
			{
				pMsgBuf = *pos;
				pMsgHeader = (LPSTRUC_MSG_HEADER)pMsgBuf;
				pPkgHeader = (LPCOMM_PKG_HEADER)(pMsgBuf + pSocketObject->m_iLenMsgHeader);
				p_Conn = pMsgHeader->pConn;

				
				if (p_Conn->iCurrSequence != pMsgHeader->iCurrsequence)
				{
					/*说明这是一个过期的包，对面的连接已经断开了,此时连接已经被释放，那么这条数据就不用发送了*/
					auto pos2 = pos;
					pos++;
					pSocketObject->m_MsgSendQueue.erase(pos2);
					--pSocketObject->m_iSendMsgQueueCount;
					p_memory->FreeMemory(pMsgBuf);
					continue;
				}

				if (p_Conn->iThrowSendCount > 0)
				{
					//靠系统驱动来发送消息，所以这里不能再发送
					pos++;
					continue;
				}

				/*往下走准备开始发消息*/
				--p_Conn->iSendCount;

				p_Conn->psendMemPointer = pMsgBuf;

				/*将要发送的消息从发送队列里移除，然后准备将它发送，然后释放内存*/
				auto pos2 = pos;
				pos++;
				pSocketObject->m_MsgSendQueue.erase(pos2); /*静态成员函数可以访问私有变量*/
				--pSocketObject->m_iSendMsgQueueCount;

				p_Conn->psendbuf = (char*)pPkgHeader;
				p_Conn->isendlen = ntohs(pPkgHeader->pkgLenth);

				/*如果能走到这里，说明epoll里没有写事件，而是我们自己调用的写操作*/
				/*如果此次写操作把写缓冲区写满了，那么就把写事件放到epoll里*/

				ngx_log_stderr(errno,"即将发送数据%ud。",p_Conn->isendlen);

				ssize_t sendsize = pSocketObject->sendproc(p_Conn, p_Conn->psendbuf, p_Conn->isendlen);

				if (sendsize > 0)
				{
					if (sendsize == p_Conn->isendlen)
					{
						p_memory->FreeMemory(p_Conn->psendMemPointer);
						p_Conn->psendMemPointer = nullptr;
						p_Conn->iThrowSendCount = 0; //这行可有可无
					}
					else //没有全部发送完毕(EAGAIN)，数据只发出去了一部分，但肯定是因为 发送缓冲区满了,往epoll上加入写事件
					{
						/*记录发送到哪里了*/
						p_Conn->psendbuf += sendsize;
						p_Conn->isendlen -= sendsize;
						++p_Conn->iThrowSendCount; //这个是个标记位，用来说明发送缓冲区是否满了

						if (pSocketObject->ngx_epoll_oper_event(p_Conn->fd, EPOLL_CTL_MOD, EPOLLOUT, 0, p_Conn) == -1)
						{
							ngx_log_core(NGX_LOG_ERROR, errno, "CSocekt::ServerSendQueueThread()ngx_epoll_oper_event()失败.");
						}
						continue;
					}
					
				}
				else 
				{
					p_memory->FreeMemory(p_Conn->psendMemPointer);  //释放内存
					p_Conn->psendMemPointer = nullptr;
					p_Conn->iThrowSendCount = 0;  //这行其实可以没有，因此此时此刻这东西就是=0的  
					continue;
				}
			} // end of for()
		
			err = pthread_mutex_lock(&pSocketObject->m_sendMessageQueueMutex);
		} // end of if(pSocketObject->m_iSendMsgQueueCount > 0)
	} //end of while

	return nullptr;
}

/*清空发送消息队列*/
void CSocket::clearMsgSendQueue()
{
	char* sTmpMempoint;
	CMemory* p_memory = CMemory::GetInstance();

	while (!m_MsgSendQueue.empty())
	{
		sTmpMempoint = m_MsgSendQueue.front();
		m_MsgSendQueue.pop_front();
		p_memory->FreeMemory(sTmpMempoint);
	}

}
