#include "ngx_macro.h"
#include "ngx_global.h"
#include "ngx_func.h"
void ngx_process_events_and_timers()
{
	int ret = g_socket.ngx_epoll_process_events(-1);
	//ngx_log_core(NGX_LOG_DEBUG, 0, "g_socket.ngx_epoll_process_events(-1)返回值是:%d", ret);
	g_socket.printTDInfo();
}