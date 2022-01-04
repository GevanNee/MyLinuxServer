#include <stdio.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>

#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_global.h"

typedef struct
{
	int signo;
	const char* signame;
	void (*handler)(int signo, siginfo_t* siginfo, void* ucontext);

}ngx_signal_t;

static void ngx_signal_handler(int signo, siginfo_t* siginfo, void* ucontext);
static void ngx_process_get_status();

ngx_signal_t signals[] = {
	/*signo*/   /*signame*/ /*handler*/
	{ SIGHUP,	"SIGHUP",	ngx_signal_handler},	/*终端断开*/
	{ SIGINT,	"SIGINT",	ngx_signal_handler},	/*直接中断，按了CTRL+C，无条件退出*/
	{ SIGTERM,	"SIGTERM",	ngx_signal_handler},	/*请求中断，可以选择忽视*/
	{ SIGCHLD,	"SIGCHLD",	ngx_signal_handler},	/*子进程退出*/
	{ SIGQUIT,	"SIGQUIT",	ngx_signal_handler},	/*强制退出*/
	{ SIGIO,	"SIGIO",	ngx_signal_handler},	/*异步IO*/
	{ SIGSYS,	"SIGSYS",	nullptr},				/*无效系统调用*/
	/*待添加信号*/

	/*数组结束标志，信号不可能是0*/
	{ 0,		nullptr,	nullptr}
};

int ngx_init_signals()
{
	ngx_signal_t *sig;
	struct sigaction sa;

	for (sig = signals; sig->signo != 0; ++sig)
	{
		memset(&sa, 0, sizeof(sa));

		if (sig->handler != nullptr)
		{
			sa.sa_sigaction = sig->handler;
			sa.sa_flags = SA_SIGINFO;
		}
		else
		{
			sa.sa_handler = SIG_IGN;
		}

		sigemptyset(&sa.sa_mask);

		if (sigaction(sig->signo, &sa, nullptr) == -1)
		{
			ngx_log_core(NGX_LOG_EMERGENCY, errno, "sigaction(%s) failed", sig->signame);
			return -1;
		}
		else
		{
			/*设置成功，可以打印一个日志*/
		}
	}
	return 0;
}

static void ngx_signal_handler(int signo, siginfo_t* siginfo, void* ucontext)
{
	ngx_signal_t* sig;
	char*	      action;

	action = (char*)"";
	for (sig = signals; sig->signo != 0; sig++)
	{
		if (sig->signo == signo)
		{
			break;
		}
	}

	if (ngx_process == NGX_PROCESS_MASTER)
	{
		switch (signo)
		{
		case SIGHUP:
			action = (char*)"";
			break;
		case SIGINT:
			ngx_log_core(NGX_LOG_CRITICAL, 0, "signal %d (%s) received from %P,action:%s", signo, sig->signame, siginfo->si_pid, action);
			exit(-1);
			action = (char*)"";
			break;
		case SIGTERM:
			action = (char*)"";
			break;
		case SIGCHLD:
			ngx_reap = 1;
			break;
		case SIGIO:
			action = (char*)"";
			break;
		case SIGSYS:
			action = (char*)"";
			break;
		default:
			break;
		}
	}
	else if (ngx_process == NGX_PROCESS_WORKER)
	{
		/**/
	}

	/*打印日志*/
	if (siginfo && siginfo->si_pid) 
	{
		ngx_log_core(NGX_LOG_NOTICE, 0, "signal %d (%s) received from %P,action:%s", signo, sig->signame, siginfo->si_pid, action);
	}
	else
	{
		ngx_log_core(NGX_LOG_NOTICE, 0, "signal %d (%s) received", signo, sig->signame);
	}

	/*专门用一个函数处理SIGCHLD信号*/
	if (signo == SIGCHLD)
	{
		ngx_process_get_status();
	}
	
	return;
}

static void ngx_process_get_status()
{
	pid_t pid;
	int err;
	int status;
	int one = 0; /*标记是否已经处理过一个子进程*/

	for (;;) 
	{
		pid = waitpid(-1, &status, WNOHANG);
		
		if (pid == 0)
		{
			ngx_log_core(NGX_LOG_INFO, 0, "没有子进程需要回收，返回");
			return;
		}

		if (pid == -1)
		{
			err = errno;
			if (err == EINTR) //信号调用被另一个信号中断
			{
				continue;
			}

			if (err == ECHILD && one)
			{
				ngx_log_core(NGX_LOG_INFO, err, "waitpid() failed!");
				return;
			}

			ngx_log_core(NGX_LOG_ALERT, err, "waitpid() failed");
			return;
		}
		
		/*到这里表示waitpid成功回收, 打印一下回收成功日志*/
		one = 1;
		if (WTERMSIG(status) != 0)/*该函数返回值是int或者long int, 含义是如果子进程是因为信号退出的，则返回导致子进程退出的信号值*/
		{
			ngx_log_core(NGX_LOG_NOTICE, 0, "pid = %P exited on signal %d!", pid, WTERMSIG(status));
		}
		else /*这里表示子进程是在main函数中return了*/
		{
			/*WEXITSTATUS(status)的返回值是子进程的exit值*/
			ngx_log_core(NGX_LOG_NOTICE, 0, "pid = %P exited with code %d!", pid, WEXITSTATUS(status));
		}
	}
	return;
}