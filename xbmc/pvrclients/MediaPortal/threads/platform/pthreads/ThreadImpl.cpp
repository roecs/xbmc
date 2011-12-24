/*
 *      Copyright (C) 2005-2011 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#include <limits.h>
#include <sys/syscall.h>
#include <sys/resource.h>
#include <string.h>
#include "client.h"

using namespace ADDON;

long CThread::Create(bool bAutoDelete, unsigned stacksize)
{
  if (m_ThreadId != 0)
  {
    XBMC->Log(LOG_ERROR, "%s - fatal error creating thread- old thread id not null", __FUNCTION__);
    return E_FAIL;
  }

  m_bAutoDelete = bAutoDelete;
  m_bStop = false;

  m_StartEvent->ResetEvent();
  m_StopEvent->ResetEvent();
  m_TermEvent->ResetEvent();
  
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  if (stacksize > PTHREAD_STACK_MIN)
    pthread_attr_setstacksize(&attr, stacksize);
  pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
  if (pthread_create(&m_ThreadId, &attr, (void*(*)(void*))staticThread, this) != 0)
  {
    XBMC->Log(LOG_NOTICE, "%s - fatal error creating thread",__FUNCTION__);
    return E_FAIL;
  }
  pthread_attr_destroy(&attr);
  return S_OK;
}

void CThread::TermHandler()
{

}

void CThread::SetThreadInfo()
{
  m_ThreadOpaque.LwpId = syscall(SYS_gettid);

  // start thread with nice level of appication
  int appNice = getpriority(PRIO_PROCESS, getpid());
  if (setpriority(PRIO_PROCESS, m_ThreadOpaque.LwpId, appNice) != 0)
    XBMC->Log(LOG_ERROR, "%s: error %s", __FUNCTION__, strerror(errno));
}

ThreadIdentifier CThread::GetCurrentThreadId()
{
  return pthread_self();
}

bool CThread::IsCurrentThread(const ThreadIdentifier tid)
{
  return pthread_equal(pthread_self(), tid);
}

int CThread::GetMinPriority(void)
{
  // one level lower than application
  return -1;
}

int CThread::GetMaxPriority(void)
{
  // one level higher than application
  return 1;
}

int CThread::GetNormalPriority(void)
{
  // same level as application
  return 0;
}

bool CThread::SetPriority(const int iPriority)
{
  bool bReturn = false;

  // wait until thread is running, it needs to get its lwp id
  m_StartEvent->Wait();

  CSingleLock lock(m_CriticalSection);

  // get min prio for SCHED_RR
  int minRR = GetMaxPriority() + 1;

  if (!m_ThreadId)
    bReturn = false;
  else if (iPriority >= minRR)
    bReturn = SetPrioritySched_RR(iPriority);
  else
  {
    // get user max prio
    struct rlimit limit;
    int userMaxPrio;
    if (getrlimit(RLIMIT_NICE, &limit) == 0)
    {
      userMaxPrio = limit.rlim_cur - 20;
    }
    else
      userMaxPrio = 0;

    // keep priority in bounds
    int prio = iPriority;
    if (prio >= GetMaxPriority())
      prio = std::min(GetMaxPriority(), userMaxPrio);
    if (prio < GetMinPriority())
      prio = GetMinPriority();

    // nice level of application
    int appNice = getpriority(PRIO_PROCESS, getpid());
    if (prio)
      prio = prio > 0 ? appNice-1 : appNice+1;

    if (setpriority(PRIO_PROCESS, m_ThreadOpaque.LwpId, prio) == 0)
      bReturn = true;
    else
      XBMC->Log(ADDON::LOG_ERROR, "%s: error %s", __FUNCTION__, strerror(errno));
  }

  return bReturn;
}

int CThread::GetPriority()
{
  int iReturn;

  // lwp id is valid after start signel has fired
  m_StartEvent->Wait();

  CSingleLock lock(m_CriticalSection);
  
  int appNice = getpriority(PRIO_PROCESS, getpid());
  int prio = getpriority(PRIO_PROCESS, m_ThreadOpaque.LwpId);
  iReturn = appNice - prio;

  return iReturn;
}

bool CThread::WaitForThreadExit(unsigned long milliseconds)
{
  bool bReturn = true;

  // lwp id is valid after start signel has fired
  if ( m_StartEvent->Wait(milliseconds) )
  {
    // Thread actually started

    m_StopEvent->SetEvent();

    if (!m_TermEvent->Wait(milliseconds))
    {
      //TODO force kill thread here
      TermHandler();
      bReturn = false;
    }

    m_ThreadOpaque.LwpId = 0;
    m_StartEvent->ResetEvent();
  }

  return bReturn;
}

bool CThread::ThreadIsStopping(unsigned long dwTimeoutMilliseconds)
{
  return m_StopEvent->Wait(dwTimeoutMilliseconds);
}

