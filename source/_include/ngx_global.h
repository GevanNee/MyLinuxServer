#pragma once

#include <signal.h>
#include "ngx_c_socket.h"

typedef struct 
{
	int log_level;
	int fd;
}ngx_log_t;

extern size_t		g_envneedmem;
extern size_t		g_argvneedmem;

extern int			g_og_argc; /*main函数的参数个数*/
extern char**		g_os_argv; /**/
extern char*		gp_envmem; /*新的环境变量地址*/


extern ngx_log_t	ngx_log; /*保存日志模块的文件地址和等级*/
extern pid_t		ngx_pid; /*当前进程的pid*/
extern pid_t		ngx_parent; /*当前进程的ppid*/
extern CSocket       g_socket;
extern int			ngx_process; /*当前进程的身份*/
extern sig_atomic_t ngx_reap; /*标志位，标记是否回收过子进程*/