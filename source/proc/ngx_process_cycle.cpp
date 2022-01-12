#include <stdio.h>
#include <signal.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include "ngx_c_conf.h"
#include "ngx_func.h"
#include "ngx_macro.h"
#include "ngx_global.h"

static void ngx_start_worker_processes(int worker_num);
static int ngx_spawn_process(int inums, const char* procname);
static void ngx_worker_process_cycle(int inums, const char* procname);
static void ngx_worker_process_init();

static u_char master_process[] = "nginx:master"; /*尾部有空格*/

void ngx_master_process_cycle()
{
    ngx_log_core(NGX_LOG_DEBUG, 0, "进入ngx_master_process_cycle()");
	sigset_t set;

    sigaddset(&set, SIGCHLD);     //子进程状态改变
    sigaddset(&set, SIGALRM);     //定时器超时
    sigaddset(&set, SIGIO);       //异步I/O
    sigaddset(&set, SIGINT);      //终端中断符
    sigaddset(&set, SIGHUP);      //连接断开
    sigaddset(&set, SIGUSR1);     //用户定义信号
    sigaddset(&set, SIGUSR2);     //用户定义信号
    sigaddset(&set, SIGWINCH);    //终端窗口大小改变
    sigaddset(&set, SIGTERM);     //终止
    sigaddset(&set, SIGQUIT);     //终端退出符

    if (sigprocmask(SIG_BLOCK, &set, nullptr) == -1)
    {
        ngx_log_core(NGX_LOG_WARN, errno, "ngx_master_process_cycle() 中，sigpromask()失败");
    }

    size_t size = sizeof(master_process) + g_argvneedmem;

    if (size < 1000)
    {
        char title[1000] = {0};
        strcpy(title, (const char*)master_process);

        //for (int i = 0; i < g_og_argc; i++) /*这里把main的参数也设置进去进程标题*/
        //{
        //    strcat(title, g_os_argv[i]);
        //    strcat(title, " ");
        //}
        
        ngx_setproctitle(title);
        ngx_log_core(NGX_LOG_INFO, 0, "%s %P [master进程] 启动成功", title, ngx_pid);
    }

    ngx_c_conf* p_config = ngx_c_conf::getInstance();
    int servants_count = p_config->getInt("WorkerProcesses", 1);
    
    ngx_start_worker_processes(servants_count); /*创建worker子进程*/

    sigemptyset(&set);

    /*主进程就在这个循环里工作*/
    for (;;)
    {
        sigsuspend(&set);
    }

    return;
}

static void ngx_start_worker_processes(int worker_num)
{
    ngx_log_core(NGX_LOG_DEBUG, errno, "开始创建子进程，准备创建的个数是%d", worker_num);
    for (int i = 0; i < worker_num; i++)
    {
        ngx_spawn_process(i, "nginx:servant");
    }
}

static int ngx_spawn_process(int inum, const char* procname)
{
    pid_t pid = fork();
    switch (pid)
    {
    case -1:
        ngx_log_core(NGX_LOG_ERROR, errno, "fork ");
        return -1;
    case 0:
        ngx_parent = ngx_pid;
        ngx_pid = getpid();
        ngx_worker_process_cycle(inum, procname);
        break;
    default:
        break;
    }

    /*主进程*/
    /*其他代码*/

    return pid;
}

static void ngx_worker_process_cycle(int inums,const char* procname)
{
    ngx_process = NGX_PROCESS_WORKER;

    ngx_worker_process_init();

    ngx_setproctitle(procname);
    ngx_log_core(NGX_LOG_INFO, 0, "%s %P [servant进程] 启动并且开始运行", procname, getpid());

    //g_threadpool.StopAll();
    /*子进程循环*/
    for (;;)
    {
        ngx_process_events_and_timers();
        //sleep(1);
    }

    g_threadpool.StopAll();
    g_socket.Shutdown_subproc();
    return;
}

static void ngx_worker_process_init()
{
    sigset_t set;
    sigemptyset(&set);

    if (sigprocmask(SIG_BLOCK, &set, nullptr) == -1)
    {
        ngx_log_core(NGX_LOG_WARN, errno, "ngx_worker_process_init()中, sigprocmask()失败");
    }

    /*初始化线程池*/
    ngx_c_conf* p_config = ngx_c_conf::getInstance();
    int tmpthreadnums = p_config->getInt("ProcMsgRecvWorkThreadCount", 5); //处理接收到的消息的线程池中线程数量
    if (g_threadpool.Create(tmpthreadnums) == false)  //创建线程池中线程
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }

    if (g_socket.Initialize_subprocess() == false) //初始化子进程需要具备的一些多线程能力相关的信息
    {
        //内存没释放，但是简单粗暴退出；
        exit(-2);
    }

    g_socket.ngx_epoll_init();
}
