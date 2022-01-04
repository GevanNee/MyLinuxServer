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

#include "ngx_comm.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"

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

void CSocket::ReadConf()
{
	ngx_log_core(NGX_LOG_DEBUG, 0, "进入CSocket::ReadConf()");

	ngx_c_conf* p_config = ngx_c_conf::getInstance();

	m_worker_connection = p_config->getInt("worker_connections", m_worker_connection);
	m_ListenPortCount = p_config->getInt("ListenPortCount", m_ListenPortCount);

	ngx_log_core(NGX_LOG_DEBUG, 0, "m_worker_connection的值是%d", m_worker_connection);
	ngx_log_core(NGX_LOG_DEBUG, 0, "退出CSocket::ReadConf()");
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

void CSocket::zdClosesocketProc(lpngx_connection_t p_Conn)
{
	if (m_ifkickTimeCount == 1)
	{
		//DeleteFromTimerQueue(p_Conn); //从时间队列中把连接干掉
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

	//inRecyConnectQueue(p_Conn);
	return;
}

bool CSocket::TestFlood(lpngx_connection_t p_Conn)
{
	ngx_log_core(NGX_LOG_EMERGENCY, 0, "CSocket::TestFlood还没实现");
	usleep(1000 * 100);
	return false;
}