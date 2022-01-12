#include <iostream>
#include <cstring>
#include <vector>
#include <time.h>
#include <sys/time.h>

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>

#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_global.h"
#include "ngx_macro.h"
#include "ngx_c_slogic.h"

using namespace std;

void freeResource();

/*设置标题有关的全局变量*/
int				g_og_argc; /*main函数的参数个数*/
char**			g_os_argv; /**/
char*			gp_envmem; /*新的环境变量地址*/
size_t			g_envneedmem;
size_t			g_argvneedmem;

pid_t			ngx_pid;
pid_t			ngx_parent;
int				ngx_process; //进程类型，master，worker还是别的
sig_atomic_t	ngx_reap; //是否回收过子进程
int				g_stopEvent; //进程退出标志

CLogicSocket			g_socket;
CThreadPool				g_threadpool;


int main(int argc, char* const * argv, char** env)
{
	int   exitcode = 0;
	g_stopEvent = 0;

	ngx_log.fd = -1;
	ngx_pid = getpid();
	ngx_parent = getppid();
	ngx_process = NGX_PROCESS_MASTER;
	ngx_reap = 0;
	
	
	g_argvneedmem = 0;
	for (int i = 0; i < argc; i++)
	{
		g_argvneedmem += strlen(argv[i]) + 1;
	}
	
	g_envneedmem = 0;
	for (int i = 0; environ[i]; ++i)
	{
		g_envneedmem += strlen(environ[i]) + 1;
	}
	
	g_og_argc = argc;
	g_os_argv = (char**)argv;

	ngx_init_signals();
	ngx_init_setproctitle();

	ngx_c_conf* p_config = ngx_c_conf::getInstance();
	if (p_config == nullptr)
	{
		ngx_log_stderr(0, "config init fail!!!!");
	}

	if (p_config->load("./ngx_conf.conf") == false)
	{
		ngx_log_stderr(0, "config load fail!!!!");
	}

	ngx_log_init();
	ngx_log_core(NGX_LOG_DEBUG, 0, "\nLog初始化成功");

	if (p_config->getInt("Daemon", 0) == 1)
	{
		int ret = ngx_daemon();
		if (ret == -1)
		{
			ngx_log_stderr(errno, "失败了");
			return -1;
		}
		if (ret == 1)
		{
			ngx_log_stderr(errno, "初始进程退出了");
			freeResource();
			exitcode = 0;
			return exitcode;
		}
	}

	if (g_socket.Initialize() == false)//初始化socket
	{
		ngx_log_stderr(errno, "main()中g_socket.Initialize() == false");
		exitcode = 1;
		goto lblexit;
	}


	/******开始业务*******/

	ngx_master_process_cycle();
	
	
	/*
	if (fork() == 0)
	{
		//int n = 1; 
		//kill(getpid(), SIGKILL);
		int n = 1;
		while (n)
		{
			ngx_log_stderr(0, "子进程循环中...");
			sleep(1);
		}
	}
	int n = 1;
	while (n)
	{
		ngx_log_stderr(0, "主进程循环中...");
		sleep(1);
	}
	*/

lblexit:
	freeResource();
	return exitcode;
}

void freeResource()
{
	if (ngx_log.fd > 0)
	{
		close(ngx_log.fd);
		ngx_log.fd = -1;
	}
}
