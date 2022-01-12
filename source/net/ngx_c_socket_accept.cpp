#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <stdlib.h>
#include <fcntl.h>

#include "ngx_comm.h"
#include "ngx_func.h"
#include "ngx_c_socket.h"
#include "ngx_c_conf.h"
#include "ngx_macro.h"
#include "ngx_global.h"

void CSocket::ngx_event_accept(lpngx_connection_t oldc)
{
	struct sockaddr addr;
	socklen_t addrsize = sizeof(struct sockaddr);
	int isUseAccept4 = 1;
	int accept_fd;

	do
	{
		if (isUseAccept4)
		{
			accept_fd = accept4(oldc->fd, &addr, &addrsize, SOCK_NONBLOCK);
		}
		else
		{
			accept_fd = accept(oldc->fd, &addr, &addrsize);
		}
		
		if (accept_fd == -1)
		{
			int err = errno;
			if (err == EAGAIN)
			{
				return;
			}

			int level = NGX_LOG_ALERT;
			if (err == ECONNABORTED) //这个错误发生在对方意外关闭套接字后(拔网线之类的),自己主机主动放弃了这个连接(可能是超时关闭之类的)
			{
				//该错误被描述为“software caused connection abort”，即“软件引起的连接中止”。原因在于当服务和客户进程在完成用于 TCP 连接的“三次握手”后，
				//客户 TCP 却发送了一个 RST （复位）分节，在服务进程看来，就在该连接已由 TCP 排队，等着服务进程调用 accept 的时候 RST 却到达了。
				//POSIX 规定此时的 errno 值必须 ECONNABORTED。源自 Berkeley 的实现完全在内核中处理中止的连接，服务进程将永远不知道该中止的发生。
				//服务器进程一般可以忽略该错误，直接再次调用accept。

				level = NGX_LOG_ERROR;
			}
			else if (err == EMFILE || err == ENFILE) //进程fd用完了
			{
				level = NGX_LOG_CRITICAL;
			}

			if (isUseAccept4 && (err == ENOSYS)) //accept4()函数没实现，坑爹？
			{
				isUseAccept4 = 0;  //标记不使用accept4()函数，改用accept()函数
				continue;         //回去重新用accept()函数搞
			}

			if (err == ECONNABORTED)  //对方关闭套接字
			{
				//这个错误因为可以忽略，所以不用干啥
				//do nothing
			}

			if (m_onlineUserCount >= m_worker_connection)
			{
				ngx_log_core(NGX_LOG_WARN, 0, "accept()后超出系统允许连接的最大连接数，accepted_fd = %d", accept_fd);
				close(accept_fd);
			}
			ngx_log_core(level, err, "ngx_event_accept()中 accept返回-1了");
			return;
		}

		lpngx_connection_t newConnect = ngx_get_connection(accept_fd);
		memcpy(&newConnect->s_sockaddr, &addr, addrsize);

		if (!isUseAccept4)
		{
			set_nonblocking(accept_fd);
		}

		newConnect->listening = oldc->listening;

		newConnect->r_handler = &CSocket::ngx_read_request_handler;
		newConnect->w_handler = &CSocket::ngx_write_request_handler;

		/*EPOLLRDHUP:表示收到了对方的FIN或者RST就触发这个事件*/
		if (ngx_epoll_oper_event(accept_fd, EPOLL_CTL_ADD, EPOLLIN | EPOLLRDHUP, 0, newConnect) == -1)
		{
			/*出错了，日志在上面的函数里写过了，这里直接回收就行了*/
			ngx_close_connection(newConnect);
		}
		else
		{
			/*在这里可以输出一下对方连接信息用于测试*/
		}

		if (m_ifTimeOutKick == 1)
		{
			AddToTimerQueue(newConnect);
		}
		++m_onlineUserCount;
	} while (0); //循环一次就跳出去
	
	return;
}