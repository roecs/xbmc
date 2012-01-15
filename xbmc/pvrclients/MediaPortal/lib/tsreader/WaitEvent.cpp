/*
 *      Copyright (C) 2005-2010 Team XBMC
 *      http://www.xbmc.org
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "WaitEvent.h"
#include "client.h"

#ifdef TARGET_WINDOWS

CWaitEvent::CWaitEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, int bManualReset, int bInitialState, const char* lpName)
{
  m_waitevent = ::CreateEvent(lpEventAttributes, bManualReset, bInitialState, (LPCTSTR) lpName);
}

CWaitEvent::~CWaitEvent(void)
{
  ::CloseHandle(m_waitevent);
}

void CWaitEvent::SetEvent()
{
  ::SetEvent(m_waitevent);
}

void CWaitEvent::ResetEvent()
{
   ::ResetEvent(m_waitevent);
}

bool CWaitEvent::Wait()
{
  DWORD dwResult=::WaitForSingleObject(m_waitevent,500);
  if (dwResult==WAIT_OBJECT_0)
    return true;

  return false;
}

bool CWaitEvent::Wait(unsigned long dwTimeoutMilliseconds)
{
  DWORD dwResult=::WaitForSingleObject(m_waitevent,dwTimeoutMilliseconds);
  switch (dwResult)
  {
    case WAIT_OBJECT_0:
      return true;
    case WAIT_TIMEOUT:
      return false;
    case WAIT_FAILED:
    {
      DWORD err = GetLastError();
      XBMC->Log(ADDON::LOG_ERROR, "%s: Error %l\n", __FUNCTION__, HRESULT_FROM_WIN32(err));
    }
    default:
      return false;
  }
  return false;
}


#elif (defined TARGET_LINUX) || (defined TARGET_DARWIN)
#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 600
#endif
#include <semaphore.h>
#include <time.h>

CWaitEvent::CWaitEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, int bManualReset, int bInitialState, const char* lpName)
{
  sem_init(&m_waitevent,   // handle to the event semaphore
                         0,       // not shared
                         0);      // initially set to non signaled state
}

CWaitEvent::~CWaitEvent(void)
{
  sem_destroy(&m_waitevent);   // Event semaphore handle
}

void CWaitEvent::SetEvent()
{ 
  sem_post(&m_waitevent); // Signal the event semaphore
}

void CWaitEvent::ResetEvent()
{
   
}

bool CWaitEvent::Wait()
{
  // Wait for the event be signaled (infinite time)
  //int retCode = sem_trywait(&m_waitevent);
  // Wait for the event be signaled with time-out
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = 500000000;
  int retCode = sem_timedwait(&m_waitevent, &ts);

  if (retCode == 0) {
    return true;
  } else {
    return false;
  }
}

bool CWaitEvent::Wait(unsigned long dwTimeoutMilliseconds)
{
  // Wait for the event be signaled (infinite time)
  //int retCode = sem_trywait(&m_waitevent);
  // Wait for the event be signaled with time-out
  struct timespec ts;
  ts.tv_sec = 0;
  ts.tv_nsec = dwTimeoutMilliseconds * 1000000;
  int retCode = sem_timedwait(&m_waitevent, &ts);

  if (retCode == 0) {
    return true;
  } else {
    return false;
  }
}


#else
#error FIXME: Add a WaitEvent implementation for your OS
#endif //WIN32
