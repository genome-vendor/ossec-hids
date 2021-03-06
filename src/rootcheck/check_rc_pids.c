/* @(#) $Id: ./src/rootcheck/check_rc_pids.c, 2011/09/08 dcid Exp $
 */

/* Copyright (C) 2009 Trend Micro Inc.
 * All right reserved.
 *
 * This program is a free software; you can redistribute it
 * and/or modify it under the terms of the GNU General Public
 * License (version 2) as published by the FSF - Free Software
 * Foundation
 */

#ifndef WIN32
#include "shared.h"
#include "rootcheck.h"


int noproc;


/** int proc_read(int pid)
 * If /proc is mounted, check to see if the pid is present
 */
int proc_read(int pid)
{
    char dir[OS_SIZE_1024 +1];

    if(noproc)
        return(0);

    snprintf(dir, OS_SIZE_1024, "%d", pid);
    if(isfile_ondir(dir, "/proc"))
    {
        return(1);
    }
    return(0);
}


/** int proc_chdir(int pid)
 * If /proc is mounted, check to see if the pid is present
 */
int proc_chdir(int pid)
{
    int ret = 0;
    char curr_dir[OS_SIZE_1024 + 1];
    char dir[OS_SIZE_1024 + 1];

    if(noproc)
        return(0);

    if(!getcwd(curr_dir, OS_SIZE_1024))
    {
        return(0);
    }

    if(chdir("/proc") == -1)
        return(0);

    snprintf(dir, OS_SIZE_1024, "/proc/%d", pid);
    if(chdir(dir) == 0)
    {
        ret = 1;
    }

    /* Returning to the previous directory */
    chdir(curr_dir);

    return(ret);
}


/** int proc_stat(int pid)
 * If /proc is mounted, check to see if the pid is present there.
 */
int proc_stat(int pid)
{
    char proc_dir[OS_SIZE_1024 + 1];

    if(noproc)
        return(0);

    snprintf(proc_dir, OS_SIZE_1024, "%s/%d", "/proc", pid);

    if(is_file(proc_dir))
    {
        return(1);
    }

    return(0);
}


/** void loop_all_pids(char *ps, pid_t max_pid, int *_errors, int *_total)
 * Check all the available PIDs for hidden stuff.
 */
void loop_all_pids(char *ps, pid_t max_pid, int *_errors, int *_total)
{
    int _kill0 = 0;
    int _kill1 = 0;
    int _gsid0 = 0;
    int _gsid1 = 0;
    int _gpid0 = 0;
    int _gpid1 = 0;
    int _ps0 = -1;
    int _proc_stat  = 0;
    int _proc_read  = 0;
    int _proc_chdir = 0;

    pid_t i = 1;
    pid_t my_pid;

    char command[OS_SIZE_1024 +1];

    my_pid = getpid();

    for(;;i++)
    {
        if((i <= 0)||(i > max_pid))
            break;

        (*_total)++;

        _kill0 = 0;
        _kill1 = 0;
        _gsid0 = 0;
        _gsid1 = 0;
        _gpid0 = 0;
        _gpid1 = 0;
        _ps0 = -1;
        _proc_stat  = 0;
        _proc_read  = 0;
        _proc_chdir = 0;


        /* kill test */
        if(!((kill(i, 0) == -1)&&(errno == ESRCH)))
        {
            _kill0 = 1;
        }

        /* getsid to test */
        if(!((getsid(i) == -1)&&(errno == ESRCH)))
        {
            _gsid0 = 1;
        }

        /* getpgid test */
        if(!((getpgid(i) == -1)&&(errno == ESRCH)))
        {
            _gpid0 = 1;
        }


        /* proc stat */
        _proc_stat = proc_stat(i);

        /* proc readdir */
        _proc_read = proc_read(i);

        /* proc chdir */
        _proc_chdir = proc_chdir(i);


        /* IF PID does not exist, keep going */
        if(!_kill0 && !_gsid0 && !_gpid0 &&
           !_proc_stat && !_proc_read && !_proc_chdir)
        {
            continue;
        }

        /* We do not need to look at our own pid */
        else if(i == my_pid)
        {
            continue;
        }

        /* Checking the number of errors */
        if((*_errors) > 15)
        {
            char op_msg[OS_SIZE_1024 +1];
            snprintf(op_msg,OS_SIZE_1024,"Excessive number of hidden processes"
                    ". It maybe a false-positive or "
                    "something really bad is going on.");
            notify_rk(ALERT_SYSTEM_CRIT, op_msg);
            return;
        }


        /* checking if process appears on ps */
        if(*ps)
        {
            snprintf(command, OS_SIZE_1024, "%s -p %d > /dev/null 2>&1",
                                                        ps,
                                                        (int)i);

            /* Found PID on ps */
            _ps0 = 0;
            if(system(command) == 0)
                _ps0 = 1;
        }

        /* If we are being run by the ossec hids, sleep here (no rush) */
        #ifdef OSSECHIDS
        sleep(2);
        #endif

        /* Everyone returned ok */
        if(_ps0 && _kill0 && _gsid0 && _gpid0 && _proc_stat && _proc_read)
        {
            continue;
        }



        /* If our kill or getsid system call, got the
         * PID , but ps didn't, we need to find if it was a problem
         * with a PID being deleted (not used anymore)
         */
        {
            if(!((getsid(i) == -1)&&(errno == ESRCH)))
            {
                _gsid1 = 1;
            }

            if(!((kill(i, 0) == -1)&&(errno == ESRCH)))
            {
                _kill1 = 1;
            }

            if(!((getpgid(i) == -1)&&(errno == ESRCH)))
            {
                _gpid1 = 1;
            }


            _proc_stat = proc_stat(i);

            _proc_read = proc_read(i);

            _proc_chdir = proc_chdir(i);

            /* If it matches, process was terminated */
            if(!_gsid1 &&!_kill1 &&!_gpid1 &&!_proc_stat &&
               !_proc_read &&!_proc_chdir)
            {
                continue;
            }
        }

        #ifdef AIX
        /* Ignoring AIX wait and sched programs. */
        if((_gsid0 == _gsid1) &&
           (_kill0 == _kill1) &&
           (_gpid0 == _gpid1) &&
           (_ps0 == 1) &&
           (_gsid0 == 1) &&
           (_kill0 == 0))
        {
            /* The wait and sched programs do not respond to kill 0.
             * So, if everything else finds it, including ps, getpid, getsid,
             * but not
             * kill, we can safely ignore on AIX.
             * A malicious program would specially try to hide from ps..
             */
            continue;
        }
        #endif


        if((_gsid0 == _gsid1)&&
           (_kill0 == _kill1)&&
           (_gsid0 != _kill0))
        {
            /* If kill found, but getsid and getpgid didnt', it may
             * be a defunct process -- ignore.
             */
            if(!((_kill0 == 1)&&(_gsid0 == 0)&&(_gpid0 == 0)&&(_gsid1 == 0)))
            {
                char op_msg[OS_SIZE_1024 +1];

                snprintf(op_msg, OS_SIZE_1024, "Process '%d' hidden from "
                        "kill (%d) or getsid (%d). Possible kernel-level"
                        " rootkit.", (int)i, _kill0, _gsid0);

                notify_rk(ALERT_ROOTKIT_FOUND, op_msg);
                (*_errors)++;
            }
        }
        else if((_kill1 != _gsid1)||
                (_gpid1 != _kill1)||
                (_gpid1 != _gsid1))
        {
            /* See defunct process comment above. */
            if(!((_kill1 == 1)&&(_gsid1 == 0)&&(_gpid0 == 0)&&(_gsid1 == 0)))
            {
                char op_msg[OS_SIZE_1024 +1];
                snprintf(op_msg, OS_SIZE_1024, "Process '%d' hidden from "
                        "kill (%d), getsid (%d) or getpgid. Possible "
                        "kernel-level rootkit.", (int)i, _kill1, _gsid1);

                notify_rk(ALERT_ROOTKIT_FOUND, op_msg);
                (*_errors)++;
            }
        }
        else if((_proc_read != _proc_stat)||
                (_proc_read != _proc_chdir)||
                (_proc_stat != _kill1))
        {
            /* checking if the pid is a thread (not showing on proc */
            if(!noproc && !check_rc_readproc((int)i))
            {
                char op_msg[OS_SIZE_1024 +1];
                snprintf(op_msg, OS_SIZE_1024, "Process '%d' hidden from "
                        "/proc. Possible kernel level rootkit.", (int)i);
                notify_rk(ALERT_ROOTKIT_FOUND, op_msg);
                (*_errors)++;
            }
        }
        else if(_gsid1 && _kill1 && !_ps0)
        {
            /* checking if the pid is a thread (not showing on ps */
            if(!check_rc_readproc((int)i))
            {
                char op_msg[OS_SIZE_1024 +1];
                snprintf(op_msg, OS_SIZE_1024, "Process '%d' hidden from "
                             "ps. Possible trojaned version installed.",
                             (int)i);

                notify_rk(ALERT_ROOTKIT_FOUND, op_msg);
                (*_errors)++;
            }
        }
    }
}


/*  check_rc_sys: v0.1
 *  Scan the whole filesystem looking for possible issues
 */
void check_rc_pids()
{
    int _total = 0;
    int _errors = 0;

    char ps[OS_SIZE_1024 +1];

    char proc_0[] = "/proc";
    char proc_1[] = "/proc/1";

    pid_t max_pid = MAX_PID;

    noproc = 1;

    /* Checking where ps is */
    memset(ps, '\0', OS_SIZE_1024 +1);
    strncpy(ps, "/bin/ps", OS_SIZE_1024);
    if(!is_file(ps))
    {
        strncpy(ps, "/usr/bin/ps", OS_SIZE_1024);
        if(!is_file(ps))
            ps[0] = '\0';
    }


    /* Proc is mounted */
    if(is_file(proc_0) && is_file(proc_1))
    {
        noproc = 0;
    }

    loop_all_pids(ps, max_pid, &_errors, &_total);

    if(_errors == 0)
    {
        char op_msg[OS_SIZE_1024 +1];
        snprintf(op_msg, OS_SIZE_1024, "No hidden process by Kernel-level "
                                    "rootkits.\n      %s is not trojaned. "
                                    "Analyzed %d processes.", ps, _total);
        notify_rk(ALERT_OK, op_msg);
    }

    return;
}

/* EOF */
#else
void check_rc_pids()
{
    return;
}
#endif
