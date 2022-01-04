#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_global.h"

int ngx_daemon()
{
	switch (fork())
	{
	case -1:
		ngx_log_core(NGX_LOG_ERROR, errno, "创建守护进程失败");
		return -1;
	case 0:
		break;
	default:
		/*父进程*/
		return 1;
	}

	ngx_parent = getppid();
	ngx_pid = getpid();

	if (setsid() == -1)
	{
		ngx_log_core(NGX_LOG_EMERGENCY, errno, "守护进程初始化中设置会话id失败！！");
		return -1;
	}

	umask(0);

	int fd = open("/dev/null", O_RDWR);

	if (dup2(fd, STDOUT_FILENO) == -1)
	{
		ngx_log_core(NGX_LOG_EMERGENCY, errno, "守护进程初始化中重定向STDOUT失败！！");
		return -1;
	}

	if (dup2(fd, STDIN_FILENO) == -1)
	{
		ngx_log_core(NGX_LOG_EMERGENCY, errno, "守护进程初始化中重定向STDIN失败！！");
		return -1;
	}

	if (dup2(fd, STDERR_FILENO) == -1)
	{
		ngx_log_core(NGX_LOG_EMERGENCY, errno, "守护进程初始化中重定向STDERR失败！！");
		return -1;
	}

	if (fd > STDERR_FILENO)
	{
		if (close(fd) == -1)
		{
			ngx_log_core(NGX_LOG_EMERGENCY, errno, "守护进程初始化中关闭空设备失败！！");
			return -1;
		}
	}

	return 0;
}