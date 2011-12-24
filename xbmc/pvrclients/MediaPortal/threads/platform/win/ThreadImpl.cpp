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

#include <windows.h>

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

  m_ThreadOpaque.handle = CreateThread(NULL,stacksize, (LPTHREAD_START_ROUTINE)&staticThread, this, 0, &m_ThreadId);
  if (m_ThreadOpaque.handle == INVALID_HANDLE_VALUE)
    return E_FAIL;

  return S_OK;
}

void CThread::TermHandler()
{
  CloseHandle(m_ThreadOpaque.handle);
  m_ThreadOpaque.handle = NULL;
}

void CThread::SetThreadInfo()
{
  const unsigned int MS_VC_EXCEPTION = 0x406d1388;
  struct THREADNAME_INFO
  {
    DWORD dwType; // must be 0x1000
    LPCSTR szName; // pointer to name (in same addr space)
    DWORD dwThreadID; // thread ID (-1 caller thread)
    DWORD dwFlags; // reserved for future use, most be zero
  } info;

  info.dwType = 0x1000;
  info.szName = m_ThreadName.c_str();
  info.dwThreadID = m_ThreadId;
  info.dwFlags = 0;

  try
  {
    RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR *)&info);
  }
  catch(...)
  {
  }
}

ThreadIdentifier CThread::GetCurrentThreadId()
{
  return ::GetCurrentThreadId();
}

bool CThread::IsCurrentThread(const ThreadIdentifier tid)
{
  return (::GetCurrentThreadId() == tid);
}

int CThread::GetMinPriority(void)
{
  return(THREAD_PRIORITY_IDLE);
}

int CThread::GetMaxPriority(void)
{
  return(THREAD_PRIORITY_HIGHEST);
}

int CThread::GetNormalPriority(void)
{
  return(THREAD_PRIORITY_NORMAL);
}

int CThread::GetSchedRRPriority(void)
{
  return GetNormalPriority();
}

bool CThread::SetPriority(const int iPriority)
{
  bool bReturn = false;

  CSingleLock lock(m_CriticalSection);
  if (m_ThreadOpaque.handle)
  {
    bReturn = SetThreadPriority(m_ThreadOpaque.handle, iPriority) == TRUE;
  }

  return bReturn;
}

int CThread::GetPriority()
{
  CSingleLock lock(m_CriticalSection);

  int iReturn = THREAD_PRIORITY_NORMAL;
  if (m_ThreadOpaque.handle)
  {
    iReturn = GetThreadPriority(m_ThreadOpaque.handle);
  }
  return iReturn;
}

bool CThread::WaitForThreadExit(unsigned long dwTimeoutMilliseconds)
{
  bool bReturn = true;

  m_hStopEvent->SetEvent();

  if (!m_hDoneEvent->Wait(dwTimeoutMilliseconds))
  {
    TerminateThread(m_ThreadOpaque.handle, -1);
    TermHandler();
    bReturn = false;
  }

  m_ThreadOpaque.handle = INVALID_HANDLE_VALUE;

  return bReturn;
}

bool CThread::ThreadIsStopping(unsigned long dwTimeoutMilliseconds)
{
  return m_hStopEvent->Wait(dwTimeoutMilliseconds);
}
