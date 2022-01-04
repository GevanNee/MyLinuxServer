
#include "ngx_global.h"

void ngx_process_events_and_timers()
{
	g_socket.ngx_epoll_process_events(-1);

	/*其他代码*/
}