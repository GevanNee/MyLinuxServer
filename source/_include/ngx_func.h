#pragma once
#include <sys/types.h>
#include <stdarg.h>
#include <stdint.h>

/*字符串相关*/
void Rtrim(char* string);
void Ltrim(char* string);
void trim(char* string);

/*日志相关*/
void ngx_log_init();
void ngx_log_stderr(int err, const char* fmt, ...);
void ngx_log_core(int level, int err, const char* fmt, ...); 
u_char* ngx_log_errno(u_char* buf, u_char* last, int err);

/*字符串相关*/
u_char* ngx_snprintf(u_char* buf, size_t max, const char* fmt, ...);
u_char* ngx_slprintf(u_char* buf, u_char* last, const char* fmt, ...);
u_char* ngx_vslprintf(u_char* buf, u_char* last, const char* fmt, va_list args);

/*信号，流程*/
int		ngx_init_signals();
int		ngx_daemon();
void	ngx_master_process_cycle();
void	ngx_process_events_and_timers();

/*标题*/
void ngx_init_setproctitle();
void ngx_setproctitle(const char* title);