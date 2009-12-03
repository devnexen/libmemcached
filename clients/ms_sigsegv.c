/*
 * File:   ms_sigsegv.c
 * Author: Mingqiang Zhuang
 *
 * Created on March 15, 2009
 *
 * (c) Copyright 2009, Schooner Information Technology, Inc.
 * http://www.schoonerinfotech.com/
 *
 */

#include "config.h"

#include <memory.h>
#include <stdlib.h>
#include <stdio.h>
#include <signal.h>
#include <ucontext.h>
#include <dlfcn.h>
#include <execinfo.h>
#include <pthread.h>

#include "ms_memslap.h"
#include "ms_setting.h"

#if defined(__cplusplus) && defined(HAVE_ABI_CXA_DEMANGLE)
# include <cxxabi.h>
#endif

#undef REG_RIP

#if defined(REG_RIP)
# define SIGSEGV_STACK_IA64
# define REGFORMAT    "%016lx"
#elif defined(REG_EIP)
# define SIGSEGV_STACK_X86
# define REGFORMAT    "%08x"
#else
# define SIGSEGV_STACK_GENERIC
# define REGFORMAT    "%x"
#endif

/* prototypes */
int ms_setup_sigsegv(void);
int ms_setup_sigpipe(void);
int ms_setup_sigint(void);


/* signal seg reaches, this function will run */
static void ms_signal_segv(int signum, siginfo_t *info, void *ptr)
{
  int i;

  UNUSED_ARGUMENT(signum);
  UNUSED_ARGUMENT(info);
  UNUSED_ARGUMENT(ptr);

  pthread_mutex_lock(&ms_global.quit_mutex);
  fprintf(stderr, "Segmentation fault occurred.\n");

#if defined(SIGSEGV_STACK_X86) || defined(SIGSEGV_STACK_IA64)
  int f= 0;
  Dl_info dlinfo;
  void **bp= 0;
  void *ip= 0;
#else
  void *bt[20];
  char **strings;
  int  sz;
#endif

#if defined(SIGSEGV_STACK_X86) || defined(SIGSEGV_STACK_IA64)
# if defined(SIGSEGV_STACK_IA64)
  ip= (void *)ucontext->uc_mcontext.gregs[REG_RIP];
  bp= (void **)ucontext->uc_mcontext.gregs[REG_RBP];
# elif defined(SIGSEGV_STACK_X86)
  ip= (void *)ucontext->uc_mcontext.gregs[REG_EIP];
  bp= (void **)ucontext->uc_mcontext.gregs[REG_EBP];
# endif

  fprintf(stderr, "Stack trace:\n");
  while (bp && ip)
  {
    if (! dladdr(ip, &dlinfo))
      break;

    const char *symname= dlinfo.dli_sname;
# if defined(HAVE_ABI_CXA_DEMANGLE) && defined(__cplusplus)
    int status;
    char *tmp= __cxa_demangle(symname, NULL, 0, &status);

    if ((status == 0) && tmp)
      symname= tmp;
# endif

    fprintf(stderr, "% 2d: %p <%s+%u> (%s)\n",
            ++f,
            ip,
            symname,
            (unsigned)(ip - dlinfo.dli_saddr),
            dlinfo.dli_fname);

# if defined(HAVE_ABI_CXA_DEMANGLE) && defined(__cplusplus)
    if (tmp)
      free(tmp);
# endif

    if (dlinfo.dli_sname && ! strcmp(dlinfo.dli_sname, "main"))
      break;

    ip= bp[1];
    bp= (void **)bp[0];
  }
#else
  fprintf(stderr, "Stack trace:\n");
  sz= backtrace(bt, 20);
  strings= backtrace_symbols(bt, sz);

  for (i= 0; i < sz; ++i)
  {
    fprintf(stderr, "%s\n", strings[i]);
  }
#endif
  fprintf(stderr, "End of stack trace\n");
  pthread_mutex_unlock(&ms_global.quit_mutex);
  exit(1);
} /* ms_signal_segv */


/* signal pipe reaches, this function will run */
static void ms_signal_pipe(int signum, siginfo_t *info, void *ptr)
{
  UNUSED_ARGUMENT(signum);
  UNUSED_ARGUMENT(info);
  UNUSED_ARGUMENT(ptr);

  pthread_mutex_lock(&ms_global.quit_mutex);
  fprintf(stderr, "\tMemslap encountered a server error. Quitting...\n");
  fprintf(stderr, "\tError info: SIGPIPE captured (from write?)\n");
  fprintf(stderr,
          "\tProbably a socket I/O error when the server is down.\n");
  pthread_mutex_unlock(&ms_global.quit_mutex);
  exit(1);
} /* ms_signal_pipe */


/* signal int reaches, this function will run */
static void ms_signal_int(int signum, siginfo_t *info, void *ptr)
{
  UNUSED_ARGUMENT(signum);
  UNUSED_ARGUMENT(info);
  UNUSED_ARGUMENT(ptr);

  pthread_mutex_lock(&ms_global.quit_mutex);
  fprintf(stderr, "SIGINT handled.\n");
  pthread_mutex_unlock(&ms_global.quit_mutex);
  exit(1);
} /* ms_signal_int */


/**
 * redirect signal seg
 *
 * @return if success, return 0, else return -1
 */
int ms_setup_sigsegv(void)
{
  struct sigaction action;

  memset(&action, 0, sizeof(action));
  action.sa_sigaction= ms_signal_segv;
  action.sa_flags= SA_SIGINFO;
  if (sigaction(SIGSEGV, &action, NULL) < 0)
  {
    perror("sigaction");
    return 0;
  }

  return -1;
} /* ms_setup_sigsegv */


/**
 * redirect signal pipe
 *
 * @return if success, return 0, else return -1
 */
int ms_setup_sigpipe(void)
{
  struct sigaction action_2;

  memset(&action_2, 0, sizeof(action_2));
  action_2.sa_sigaction= ms_signal_pipe;
  action_2.sa_flags= SA_SIGINFO;
  if (sigaction(SIGPIPE, &action_2, NULL) < 0)
  {
    perror("sigaction");
    return 0;
  }

  return -1;
} /* ms_setup_sigpipe */


/**
 * redirect signal int
 *
 * @return if success, return 0, else return -1
 */
int ms_setup_sigint(void)
{
  struct sigaction action_3;

  memset(&action_3, 0, sizeof(action_3));
  action_3.sa_sigaction= ms_signal_int;
  action_3.sa_flags= SA_SIGINFO;
  if (sigaction(SIGINT, &action_3, NULL) < 0)
  {
    perror("sigaction");
    return 0;
  }

  return -1;
} /* ms_setup_sigint */


#ifndef SIGSEGV_NO_AUTO_INIT
static void __attribute((constructor)) ms_init(void)
{
  ms_setup_sigsegv();
  ms_setup_sigpipe();
  ms_setup_sigint();
}
#endif
