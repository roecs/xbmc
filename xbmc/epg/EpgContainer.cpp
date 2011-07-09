/*
 *      Copyright (C) 2005-2010 Team XBMC
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

#include "threads/SingleLock.h"
#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "dialogs/GUIDialogExtendedProgressBar.h"
#include "dialogs/GUIDialogProgress.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

#include "EpgContainer.h"
#include "Epg.h"
#include "EpgInfoTag.h"
#include "EpgSearchFilter.h"

using namespace std;
using namespace EPG;

CEpgContainer::CEpgContainer(void)
{
  m_progressDialog = NULL;
  m_bStop = true;
  Clear(false);
}

CEpgContainer::~CEpgContainer(void)
{
  Clear();
}

CEpgContainer &CEpgContainer::Get(void)
{
  static CEpgContainer epgInstance;
  return epgInstance;
}

void CEpgContainer::Unload(void)
{
  Stop();
  Clear(false);
}

void CEpgContainer::Clear(bool bClearDb /* = false */)
{
  CSingleLock lock(m_critSection);

  /* make sure the update thread is stopped */
  bool bThreadRunning = !m_bStop;
  if (bThreadRunning && !Stop())
  {
    CLog::Log(LOGERROR, "%s - cannot stop the update thread", __FUNCTION__);
    return;
  }

  /* clear all epg tables and remove pointers to epg tables on channels */
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
  {
    CEpg *epg = at(iEpgPtr);
    epg->Clear();
  }

  /* remove all EPG tables */
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
  {
    delete at(iEpgPtr);
  }
  clear();

  /* clear the database entries */
  if (bClearDb)
  {
    if (m_database.Open())
    {
      m_database.DeleteEpg();
      m_database.Close();
    }
  }

  m_iLastEpgUpdate  = 0;
  m_bDatabaseLoaded = false;

  lock.Leave();

  if (bThreadRunning)
    Start();
}

void CEpgContainer::Start(void)
{
  CSingleLock lock(m_critSection);

  m_bStop = false;
  g_guiSettings.RegisterObserver(this);
  LoadSettings();

  Create();
  SetName("XBMC EPG thread");
  SetPriority(-1);
  CLog::Log(LOGNOTICE, "%s - EPG thread started", __FUNCTION__);
}

bool CEpgContainer::Stop(void)
{
  StopThread();

  g_guiSettings.UnregisterObserver(this);

  return true;
}

void CEpgContainer::Notify(const Observable &obs, const CStdString& msg)
{
  /* settings were updated */
  if (msg == "settings")
    LoadSettings();
}

void CEpgContainer::Process(void)
{
  time_t iNow       = 0;
  m_iLastEpgUpdate  = 0;
  m_iLastEpgActiveTagCheck = 0;
  CDateTime::GetCurrentDateTime().GetAsTime(m_iLastEpgCleanup);

  if (m_database.Open())
  {
    m_database.DeleteOldEpgEntries();
    m_database.Get(*this);
    m_database.Close();
  }

  AutoCreateTablesHook();

  while (!m_bStop)
  {
    CDateTime::GetCurrentDateTime().GetAsTime(iNow);

    /* load or update the EPG */
    if (!m_bStop && (iNow > m_iLastEpgUpdate + g_advancedSettings.m_iEpgUpdateCheckInterval || !m_bDatabaseLoaded))
    {
      UpdateEPG(!m_bDatabaseLoaded);
    }

    /* clean up old entries */
    if (!m_bStop && iNow > m_iLastEpgCleanup + g_advancedSettings.m_iEpgCleanupInterval)
      RemoveOldEntries();

    /* check for updated active tag */
    if (!m_bStop)
      CheckPlayingEvents();

    /* call the update hook */
    ProcessHook(iNow);

    Sleep(1000);
  }
}

CEpg *CEpgContainer::GetById(int iEpgId) const
{
  CEpg *epg = NULL;

  CSingleLock lock(m_critSection);
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
  {
    if (at(iEpgPtr)->EpgID() == iEpgId)
    {
      epg = at(iEpgPtr);
      break;
    }
  }

  return epg;
}

CEpg *CEpgContainer::GetByIndex(unsigned int iIndex) const
{
  CEpg *epg = NULL;

  CSingleLock lock(m_critSection);
  if (iIndex < size())
    epg = at(iIndex);

  return epg;
}

bool CEpgContainer::UpdateEntry(const CEpg &entry, bool bUpdateDatabase /* = false */)
{
  bool bReturn = false;
  CSingleLock lock(m_critSection);

  /* make sure the update thread is stopped */
  bool bThreadRunning = !m_bStop && m_bDatabaseLoaded;
  if (bThreadRunning && !Stop())
    return bReturn;

  CEpg *epg = GetById(entry.EpgID());

  if (!epg)
  {
    epg = CreateEpg(entry.EpgID());
    if (epg)
      push_back(epg);
  }

  bReturn = epg ? epg->Update(entry, bUpdateDatabase) : false;

  lock.Leave();

  if (bThreadRunning)
    Start();

  return bReturn;
}

bool CEpgContainer::LoadSettings(void)
{
  m_bIgnoreDbForClient = g_guiSettings.GetBool("epg.ignoredbforclient");
  m_iUpdateTime        = g_guiSettings.GetInt ("epg.epgupdate") * 60;
  m_iDisplayTime       = g_guiSettings.GetInt ("epg.daystodisplay") * 24 * 60 * 60;

  return true;
}

bool CEpgContainer::RemoveOldEntries(void)
{
  CLog::Log(LOGINFO, "EpgContainer - %s - removing old EPG entries",
      __FUNCTION__);

  CDateTime now = CDateTime::GetCurrentDateTime().GetAsUTCDateTime();

  /* call Cleanup() on all known EPG tables */
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
  {
    CEpg *epg = at(iEpgPtr);
    if (epg)
      at(iEpgPtr)->Cleanup(now);
  }

  /* remove the old entries from the database */
  if (!m_bIgnoreDbForClient)
  {
    if (m_database.Open())
    {
      m_database.DeleteOldEpgEntries();
      m_database.Close();
    }
  }

  CSingleLock lock(m_critSection);
  CDateTime::GetCurrentDateTime().GetAsTime(m_iLastEpgCleanup);

  return true;
}

CEpg *CEpgContainer::CreateEpg(int iEpgId)
{
  return new CEpg(iEpgId);
}

bool CEpgContainer::DeleteEpg(const CEpg &epg, bool bDeleteFromDatabase /* = false */)
{
  bool bReturn = false;
  CSingleLock lock(m_critSection);

  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
  {
    if (at(iEpgPtr)->EpgID() == epg.EpgID())
    {
      if (bDeleteFromDatabase && m_database.Open())
      {
        m_database.Delete(*at(iEpgPtr));
        m_database.Close();
      }

      delete at(iEpgPtr);
      bReturn = true;
    }
  }

  return bReturn;
}

bool CEpgContainer::UpdateSingleTable(CEpg *epg, const time_t start, const time_t end)
{
  bool bReturn(false);

  if (!epg)
    return bReturn;

  if (m_bDatabaseLoaded)
    epg->Cleanup();

  bReturn = m_bDatabaseLoaded || m_bIgnoreDbForClient ?
      epg->Update(start, end, m_iUpdateTime) :
      epg->Load();

  /* try to update the table from clients if nothing was loaded from the db */
  if (!m_bDatabaseLoaded && !m_bIgnoreDbForClient && !bReturn)
    bReturn = epg->Update(start, end, m_iUpdateTime);

  if (!bReturn && m_bDatabaseLoaded)
  {
    CLog::Log(LOGERROR, "EpgContainer - %s - failed to update table '%s'",
        __FUNCTION__, epg->Name().c_str());
  }

  return bReturn;
}

void CEpgContainer::CloseUpdateDialog(void)
{
  CSingleLock lock(m_critSection);
  if (m_progressDialog)
  {
    m_progressDialog->Close(true);
    m_progressDialog = NULL;
  }
}

bool CEpgContainer::InterruptUpdate(void) const
{
  return m_bStop;
}

bool CEpgContainer::UpdateEPG(bool bShowProgress /* = false */)
{
  CSingleLock lock(m_critSection);
  unsigned int iEpgCount = size();

  if (InterruptUpdate())
    return false;
  lock.Leave();

  bool bInterrupted(false);
  long iStartTime                         = CTimeUtils::GetTimeMS();
  bool bUpdateSuccess                     = true;

  if (!m_bDatabaseLoaded)
    CLog::Log(LOGNOTICE, "EpgContainer - %s - loading EPG entries for %i tables from the database",
        __FUNCTION__, iEpgCount);
  else
    CLog::Log(LOGNOTICE, "EpgContainer - %s - starting EPG update for %i tables (update time = %d)",
        __FUNCTION__, iEpgCount, m_iUpdateTime);

  /* set start and end time */
  time_t start;
  time_t end;
  CDateTime::GetCurrentDateTime().GetAsUTCDateTime().GetAsTime(start);
  end = start;
  start -= g_advancedSettings.m_iEpgLingerTime * 60;
  end += m_iDisplayTime;

  /* open the database */
  if (!m_database.Open())
  {
    CLog::Log(LOGERROR, "EpgContainer - %s - could not open the database", __FUNCTION__);
    return false;
  }

  /* show the progress bar */
  if (bShowProgress)
  {
    m_progressDialog = (CGUIDialogExtendedProgressBar *)g_windowManager.GetWindow(WINDOW_DIALOG_EXT_PROGRESS);
    m_progressDialog->Show();
    m_progressDialog->SetHeader(g_localizeStrings.Get(19004));
  }

  int iUpdatedTables = 0;

  /* load or update all EPG tables */
  for (unsigned int iEpgPtr = 0; iEpgPtr < iEpgCount; iEpgPtr++)
  {
    if (InterruptUpdate())
    {
      CLog::Log(LOGNOTICE, "EpgContainer - %s - EPG load/update interrupted", __FUNCTION__);
      bInterrupted = true;
      break;
    }

    CEpg *epg = GetByIndex(iEpgPtr);
    if (!epg)
      continue;

    bool bCurrent = UpdateSingleTable(epg, start, end);

    bUpdateSuccess = bCurrent && bUpdateSuccess;
    if (bCurrent)
      ++iUpdatedTables;

    lock.Enter();
    if (bShowProgress && m_progressDialog)
    {
      /* update the progress bar */
      m_progressDialog->SetProgress(iEpgPtr, iEpgCount);
      m_progressDialog->SetTitle(at(iEpgPtr)->Name());
      m_progressDialog->UpdateState();
    }

    /* We don't want to miss active epg tags updates if this process it taking long */
    CheckPlayingEvents();

    lock.Leave();

    if (m_bDatabaseLoaded)
      Sleep(50); /* give other threads a chance to get a lock on tables */
  }

  if (!bInterrupted)
  {
    lock.Enter();
    /* only try to load the database once */
    m_bDatabaseLoaded = true;
    CDateTime::GetCurrentDateTime().GetAsTime(m_iLastEpgUpdate);
    lock.Leave();

    /* update the last scan time if we did a full update */
    if (bUpdateSuccess && m_bDatabaseLoaded && !m_bIgnoreDbForClient)
        m_database.PersistLastEpgScanTime(0);
  }

  m_database.Close();

  CloseUpdateDialog();

  long lUpdateTime = CTimeUtils::GetTimeMS() - iStartTime;
  CLog::Log(LOGINFO, "EpgContainer - %s - finished %s %d EPG tables after %li.%li seconds",
      __FUNCTION__, m_bDatabaseLoaded ? "updating" : "loading", iEpgCount, lUpdateTime / 1000, lUpdateTime % 1000);

  /* notify observers */
  if (iUpdatedTables > 0)
  {
    SetChanged();
    NotifyObservers("epg", true);
  }

  return bUpdateSuccess;
}

int CEpgContainer::GetEPGAll(CFileItemList* results)
{
  int iInitialSize = results->Size();

  CSingleLock lock(m_critSection);
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
    at(iEpgPtr)->Get(results);

  return results->Size() - iInitialSize;
}

const CDateTime CEpgContainer::GetFirstEPGDate(void) const
{
  CDateTime returnValue;

  CSingleLock lock(m_critSection);
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
  {
    CDateTime entry = at(iEpgPtr)->GetFirstDate();
    if (entry.IsValid() && (!returnValue.IsValid() || entry < returnValue))
      returnValue = entry;
  }

  return returnValue;
}

const CDateTime CEpgContainer::GetLastEPGDate(void) const
{
  CDateTime returnValue;

  CSingleLock lock(m_critSection);
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
  {
    CDateTime entry = at(iEpgPtr)->GetLastDate();
    if (entry.IsValid() && (!returnValue.IsValid() || entry > returnValue))
      returnValue = entry;
  }

  return returnValue;
}

int CEpgContainer::GetEPGSearch(CFileItemList* results, const EpgSearchFilter &filter)
{
  /* get filtered results from all tables */
  CSingleLock lock(m_critSection);
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
    at(iEpgPtr)->Get(results, filter);
  lock.Leave();

  /* remove duplicate entries */
  if (filter.m_bPreventRepeats)
    EpgSearchFilter::RemoveDuplicates(results);

  return results->Size();
}

bool CEpgContainer::CheckPlayingEvents(void)
{
  time_t iNow;

  CDateTime::GetCurrentDateTime().GetAsTime(iNow);
  if (iNow >= m_iLastEpgActiveTagCheck + g_advancedSettings.m_iEpgActiveTagCheckInterval)
  {
  	bool bFoundChanges(false);
  	CSingleLock lock(m_critSection);

    for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
      if (at(iEpgPtr)->CheckPlayingEvent())
        bFoundChanges = true;
    CDateTime::GetCurrentDateTime().GetAsTime(m_iLastEpgActiveTagCheck);

    if (bFoundChanges)
    {
      SetChanged();
      NotifyObservers("epg-now", true);
    }
    return true;
  }
  return false;
}
