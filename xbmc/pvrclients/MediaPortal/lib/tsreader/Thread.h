#pragma once
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
 *  Copyright (C) 2005      nate
 *  Copyright (C) 2006      bear
 *
 *  nate can be reached on the forums at
 *    http://forums.dvbowners.com/
 */

#ifdef TSREADER

#include "os-dependent.h"
#include <string>

#include "threads/ThreadImpl.h"
#include "SingleLock.h"
#include "CriticalSection.h"
#include "WaitEvent.h"

class IRunnable
{
public:
  virtual void Run()=0;
  virtual ~IRunnable() {}
};

class CThread: public IRunnable
{
  protected:
    CThread(const char* ThreadName);
  public:
    CThread(IRunnable* pRunnable, const char* ThreadName);
    virtual ~CThread();
    long Create(bool bAutoDelete = false, unsigned stacksize = 0);
    bool WaitForThreadExit(unsigned long dwTimeoutMilliseconds = 1000);

    bool SetPriority(const int iPriority);
    int GetPriority(void);
    int GetMinPriority(void);
    int GetMaxPriority(void);
    int GetNormalPriority(void);
    int GetSchedRRPriority(void);
    bool SetPrioritySched_RR(int iPriority);
    bool IsAutoDelete() const;

    bool ThreadIsStopping(unsigned long dwTimeoutMilliseconds = 10);
    bool IsThreadRunning();
    //tThreadId ThreadId(void);
    static bool IsCurrentThread(const ThreadIdentifier tid);
    static ThreadIdentifier GetCurrentThreadId();
 
  protected:
    virtual void OnStartup(){};
    virtual void OnExit(){};
    virtual void OnException(){} // signal termination handler
    virtual void Process();
    CWaitEvent* m_TermEvent;
    CWaitEvent* m_StopEvent;
    CWaitEvent* m_StartEvent;
    volatile bool m_bStop;

  private:
    ThreadOpaque m_ThreadOpaque;
    CCriticalSection m_CriticalSection;
    bool   m_bThreadRunning;
    IRunnable* m_pRunnable;
    static THREADFUNC staticThread(void *data);
    void TermHandler();
    //THREADHANDLE m_ThreadHandle;
    std::string m_ThreadName;
    void SetThreadInfo();
	ThreadIdentifier m_ThreadId;
	bool m_bAutoDelete;
};

#endif //TSREADER
