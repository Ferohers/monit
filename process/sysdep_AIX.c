/*
 * Copyright (C) 2010 Tildeslash Ltd. All rights reserved.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * In addition, as a special exception, the copyright holders give
 * permission to link the code of portions of this program with the
 * OpenSSL library under certain conditions as described in each
 * individual source file, and distribute linked combinations
 * including the two.
 *
 * You must obey the GNU General Public License in all respects
 * for all of the code used other than OpenSSL.  If you modify
 * file(s) with this exception, you may extend this exception to your
 * version of the file(s), but you are not obligated to do so.  If you
 * do not wish to do so, delete this exception statement from your
 * version.  If you delete this exception statement from all source
 * files in the program, then also delete it here.
 */


#include <config.h>

#include "monitor.h"

#ifdef HAVE_STDIO_H
#include <stdio.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifdef HAVE_SYS_STAT_H
#include <sys/stat.h>
#endif

#ifdef HAVE_FCNTL_H
#include <fcntl.h>
#endif

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#ifdef TIME_WITH_SYS_TIME
#include <time.h>

#ifdef HAVE_SYS_TIME_H
#include <sys/time.h>
#endif
#else
#include <time.h>
#endif

#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_PROCINFO_H
#include <procinfo.h>
#endif
 
#ifdef HAVE_SYS_TYPES_H
#include <sys/types.h>
#endif

#ifdef HAVE_SYS_PROC_H
#include <sys/proc.h>
#endif

#ifdef HAVE_PROCFS_H
#include <procfs.h>
#endif

#ifdef HAVE_CF_H
#include <cf.h>
#endif

#ifdef HAVE_SYS_CFGODM_H
#include <sys/cfgodm.h>
#endif

#ifdef HAVE_SYS_CFGDB_H
#include <sys/cfgdb.h>
#endif

#ifdef HAVE_SYS_SYSTEMCFG_H
#include <sys/systemcfg.h>
#endif

#ifdef HAVE_SYS_PROC_H
#include <sys/proc.h>
#endif

#ifdef HAVE_LIBPERFSTAT_H
#include <libperfstat.h>
#endif


#include "process.h"
#include "process_sysdep.h"

/**
 *  System dependent resource gathering code for AIX
 *
 *  @author Richard Schwaninger <risc@vmilch.at>
 *  @author Jan-Henrik Haukeland, <hauk@tildeslash.com>
 *  @author Christian Hopp <chopp@iei.tu-clausthal.de>
 *  @author Martin Pala <martinp@tildeslash.com>
 *
 *  @file
 */

/* There is no prototype for getprocs64 as of AIX 5.3 yet */
int getprocs64(struct procentry64 *ProcessBuffer, int ProcessSize, struct fdsinfo64 *FileBuffer, int FileSize, pid_t *IndexPointer, int Count);


static int                page_size;
static int                cpu_initialized = 0;
static unsigned long long cpu_total_old = 0ULL;
static unsigned long long cpu_user_old  = 0ULL;
static unsigned long long cpu_syst_old  = 0ULL;
static unsigned long long cpu_wait_old  = 0ULL;

struct procentry64 *procs = NULL;


int init_process_info_sysdep(void) {
  perfstat_memory_total_t mem;

  if (perfstat_memory_total(NULL, &mem, sizeof(perfstat_memory_total_t), 1) < 1) {
    LogError("system statistic error -- perfstat_memory_total failed: %s\n", STRERROR);
    return FALSE;
  }

  page_size                = getpagesize();
  systeminfo.mem_kbyte_max = (unsigned long)(mem.real_total * (page_size / 1024));
  systeminfo.cpus          = sysconf(_SC_NPROCESSORS_ONLN);

  return TRUE;
}


/**
 * This routine returns 'nelem' double precision floats containing
 * the load averages in 'loadv'; at most 3 values will be returned.
 * @param loadv destination of the load averages
 * @param nelem number of averages
 * @return: 0 if successful, -1 if failed (and all load averages are 0).
 */
int getloadavg_sysdep (double *loadv, int nelem) {
  perfstat_cpu_total_t cpu;

  if (perfstat_cpu_total(NULL, &cpu, sizeof(perfstat_cpu_total_t), 1) < 1) {
      LogError("system statistic error -- perfstat_cpu_total failed: %s\n", STRERROR);
      return -1;
  }
      
  switch (nelem) {
    case 3:
      loadv[2] = (double)(cpu.loadavg[2] / (1<<SBITS));

    case 2:
      loadv[1] = (double)(cpu.loadavg[1] / (1<<SBITS));

    case 1:
      loadv[0] = (double)(cpu.loadavg[0] / (1<<SBITS));
  }

  return TRUE;
}


/**
 * Read all processes to initialize the process tree
 * @param reference  reference of ProcessTree
 * @return treesize>0 if succeeded otherwise =0.
 */
int initprocesstree_sysdep(ProcessTree_T ** reference) {
  int             i;
  int             treesize;
  struct userinfo user;
  ProcessTree_T  *pt;
  pid_t           firstproc = 0;

  memset(&user, 0, sizeof(struct userinfo));

  if ((treesize = getprocs64(NULL, 0, NULL, 0, &firstproc, PID_MAX)) < 0) {
    LogError("system statistic error -- getprocs64 failed: %s\n", STRERROR);
    return FALSE;
  }

  procs = xcalloc(sizeof(struct procentry64), treesize);

  firstproc = 0;
  if ((treesize = getprocs64(procs, sizeof(struct procentry64), NULL, 0, &firstproc, treesize)) < 0) {
    FREE(procs);
    LogError("system statistic error -- getprocs64 failed: %s\n", STRERROR);
    return FALSE;
  }

  pt = xcalloc(sizeof(ProcessTree_T), treesize);

  for (i = 0; i < treesize; i++) {
    pt[i].cputime     = 0;
    pt[i].cpu_percent = 0;
    pt[i].mem_kbyte   = 0;
    pt[i].pid         = procs[i].pi_pid;
    pt[i].ppid        = procs[i].pi_ppid;
    pt[i].starttime   = procs[i].pi_start;

    if (procs[i].pi_state == SZOMB) {
      pt[i].status_flag |= PROCESS_ZOMBIE;
    } else if (getuser(&(procs[i]), sizeof(struct procinfo), &user, sizeof(struct userinfo)) != -1) {
      pt[i].mem_kbyte = (user.ui_drss + user.ui_trss) * (page_size / 1024);
      pt[i].cputime   = (user.ui_ru.ru_utime.tv_sec + user.ui_ru.ru_utime.tv_usec * 1.0e-6 + user.ui_ru.ru_stime.tv_sec + user.ui_ru.ru_stime.tv_usec * 1.0e-6) * 10;
    }
  }

  FREE(procs);
  *reference = pt;

  return treesize;
}


/**
 * This routine returns kbyte of real memory in use.
 * @return: TRUE if successful, FALSE if failed (or not available)
 */
int used_system_memory_sysdep(SystemInfo_T *si) {
  perfstat_memory_total_t  mem;
  struct swaptable        *s;
  char                    *strtab;
  unsigned long long       total = 0ULL;
  unsigned long long       used  = 0ULL;

  /* Memory */
  if (perfstat_memory_total(NULL, &mem, sizeof(perfstat_memory_total_t), 1) < 1) {
    LogError("system statistic error -- perfstat_memory_total failed: %s\n", STRERROR);
    return FALSE;
  }
  si->total_mem_kbyte = (unsigned long)((mem.real_total - mem.real_free - mem.numperm) * (page_size / 1024));

  /* Swap */
again:
 if ((num = swapctl(SC_GETNSWP, 0)) == -1) {
    LogError("system statistic error -- swap usage gathering failed: %s\n", STRERROR);
    return FALSE;
  }
  if (num == 0) {
    DEBUG("system statistic -- no swap configured\n");
    si->swap_kbyte_max = 0;
    return TRUE;
  }
  s = (struct swaptable *)xmalloc(num * sizeof(struct swapent) + sizeof(struct swaptable));
  strtab = (char *)xmalloc((num + 1) * MAXSTRSIZE);
  for (i = 0; i < (num + 1); i++)
    s->swt_ent[i].ste_path = strtab + (i * MAXSTRSIZE);
  s->swt_n = num + 1;
  if ((n = swapctl(SC_LIST, s)) < 0) {
    LogError("system statistic error -- swap usage gathering failed: %s\n", STRERROR);
    si->swap_kbyte_max = 0;
    FREE(s);
    FREE(strtab);
    return FALSE;
  }
  if (n > num) {
    DEBUG("system statistic -- new swap added: deferring swap usage statistics to next cycle\n");
    FREE(s);
    FREE(strtab);
    goto again;
  }
  for (i = 0; i < n; i++) {
    if (!(s->swt_ent[i].ste_flags & ST_INDEL) && !(s->swt_ent[i].ste_flags & ST_DOINGDEL)) {
      total += s->swt_ent[i].ste_pages;
      used  += s->swt_ent[i].ste_pages - s->swt_ent[i].ste_free;
    }
  }
  FREE(s);
  FREE(strtab);
  si->swap_kbyte_max   = (unsigned long)(double)(total * page_size) / 1024.;
  si->total_swap_kbyte = (unsigned long)(double)(used  * page_size) / 1024.;

  return TRUE;
}


/**
 * This routine returns system/user CPU time in use.
 * @return: TRUE if successful, FALSE if failed (or not available)
 */
int used_system_cpu_sysdep(SystemInfo_T *si) {
  perfstat_cpu_total_t cpu;
  unsigned long long cpu_total;
  unsigned long long cpu_total_new = 0ULL;
  unsigned long long cpu_user      = 0ULL;
  unsigned long long cpu_syst      = 0ULL;
  unsigned long long cpu_wait      = 0ULL;

  if (perfstat_cpu_total(NULL, &cpu, sizeof(perfstat_cpu_total_t), 1) < 0) {
      LogError("system statistic error -- perfstat_cpu_total failed: %s\n", STRERROR);
      return -1;
  }

  cpu_total_new = (cpu.user + cpu.sys + cpu.wait + cpu.idle) / cpu.ncpus;
  cpu_total     = cpu_total_new - cpu_total_old;
  cpu_total_old = cpu_total_new;
  cpu_user      = cpu.user / cpu.ncpus;
  cpu_syst      = cpu.sys / cpu.ncpus;
  cpu_wait      = cpu.wait / cpu.ncpus;

  if (cpu_initialized) {
    if (cpu_total > 0) {
      si->total_cpu_user_percent = 1000 * ((double)(cpu_user - cpu_user_old) / (double)cpu_total);
      si->total_cpu_syst_percent = 1000 * ((double)(cpu_syst - cpu_syst_old) / (double)cpu_total);
      si->total_cpu_wait_percent = 1000 * ((double)(cpu_wait - cpu_wait_old) / (double)cpu_total);
    } else {
      si->total_cpu_user_percent = 0;
      si->total_cpu_syst_percent = 0;
      si->total_cpu_wait_percent = 0;
    }
  }

  cpu_user_old = cpu_user;
  cpu_syst_old = cpu_syst;
  cpu_wait_old = cpu_wait;

  cpu_initialized = 1;

  return TRUE;
}

