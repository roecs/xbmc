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

/*
 *  This file originates from TSFileSource, a GPL directshow push
 *  source filter that provides an MPEG transport stream output.
 *  Copyright (C) 2004-2006 bear
 *  Copyright (C) 2005      nate
 *
 *  bear and nate can be reached on the forums at
 *    http://forums.dvbowners.com/
 */

//TODO: code below is Windows specific. Make platform independent (use pthreads under Linux/OSX)

#ifdef TSREADER

#include "client.h"
#include "Thread.h"

#include "threads/platform/ThreadImpl.cpp"

using namespace ADDON;

CThread::CThread(const char* ThreadName)
{
  m_hStopEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
  m_hDoneEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
  m_ThreadOpaque.handle = INVALID_HANDLE_VALUE;
  m_bThreadRunning=FALSE;

  m_bStop = false;

  m_bAutoDelete = false;
  m_ThreadId = 0;

  m_pRunnable=NULL;

  if (ThreadName)
    m_ThreadName = ThreadName;
}

CThread::CThread(IRunnable* pRunnable, const char* ThreadName)
{
  m_hStopEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
  m_hDoneEvent = CreateEvent(NULL, TRUE, TRUE, NULL);
  m_ThreadOpaque.handle = INVALID_HANDLE_VALUE;
  m_bThreadRunning=FALSE;

  m_bStop = false;

  m_bAutoDelete = false;
  m_ThreadId = 0;

  m_pRunnable=pRunnable;

  if (ThreadName)
    m_ThreadName = ThreadName;
}


CThread::~CThread()
{
  WaitForThreadExit();
  CloseHandle(m_hStopEvent);
  CloseHandle(m_hDoneEvent);
}

bool CThread::IsThreadRunning()
{
  return m_bThreadRunning;
}

void CThread::Process()
{
  ResetEvent(m_hDoneEvent);
  m_bThreadRunning=TRUE;
  try
  {
    Run();
  }
  catch (LPWSTR pStr)
  {
    pStr = NULL;
  }
  SetEvent(m_hDoneEvent);
  m_bThreadRunning=FALSE;
}

THREADFUNC CThread::staticThread(void* data)
{
  CThread* pThread = (CThread*)(data);
  std::string name;
  ThreadIdentifier id;
  bool autodelete;

  if (!pThread) {
    XBMC->Log(LOG_ERROR,"%s, sanity failed. thread is NULL.",__FUNCTION__);
    return 1;
  }

  name = pThread->m_ThreadName;
  id = pThread->m_ThreadId;
  autodelete = pThread->m_bAutoDelete;

  pThread->SetThreadInfo();

  XBMC->Log(LOG_NOTICE,"Thread %s start, auto delete: %s", name.c_str(), (pThread->IsAutoDelete() ? "true" : "false"));
  pThread->OnStartup();
  pThread->Process();
  pThread->OnExit();

  pThread->m_ThreadId = 0;
  pThread->TermHandler();

  if (autodelete)
  {
    XBMC->Log(LOG_DEBUG,"Thread %s %li terminating (autodelete)", name.c_str(), id);
    delete pThread;
    pThread = NULL;
  }
  else
    XBMC->Log(LOG_DEBUG,"Thread %s %li terminating", name.c_str(), id);

  return 0;
}

bool CThread::IsAutoDelete() const
{
  return m_bAutoDelete;
}

#endif //TSREADER
