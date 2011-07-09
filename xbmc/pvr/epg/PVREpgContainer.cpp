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
#include "PVREpgContainer.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/timers/PVRTimers.h"
#include "pvr/recordings/PVRRecordings.h"
#include "pvr/windows/GUIWindowPVR.h"
#include "guilib/GUIWindowManager.h"
#include "settings/GUISettings.h"
#include "utils/log.h"
#include "FileItem.h"

using namespace std;
using namespace PVR;
using namespace EPG;

void PVR::CPVREpgContainer::Clear(bool bClearDb /* = false */)
{
  CSingleLock lock(m_critSection);
  // XXX stop the timers from being updated while clearing tags
  /* remove all pointers to epg tables on timers */
  CPVRTimers *timers = g_PVRTimers;
  for (unsigned int iTimerPtr = 0; iTimerPtr < timers->size(); iTimerPtr++)
    timers->at(iTimerPtr)->SetEpgInfoTag(NULL);

  CEpgContainer::Clear(bClearDb);
}

bool PVR::CPVREpgContainer::CreateChannelEpgs(void)
{
  bool bReturn = g_PVRChannelGroups->GetGroupAllTV()->CreateChannelEpgs();
  return g_PVRChannelGroups->GetGroupAllRadio()->CreateChannelEpgs() && bReturn;
}

int PVR::CPVREpgContainer::GetEPGAll(CFileItemList* results, bool bRadio /* = false */)
{
  int iInitialSize = results->Size();
  const CPVRChannelGroup *group = g_PVRChannelGroups->GetGroupAll(bRadio);
  if (!group)
    return -1;

  CSingleLock lock(m_critSection);
  for (int iChannelPtr = 0; iChannelPtr < group->GetNumChannels(); iChannelPtr++)
  {
    CPVRChannel *channel = (CPVRChannel *) group->GetByIndex(iChannelPtr);
    if (!channel || channel->IsHidden())
      continue;

    channel->GetEPG(results);
  }

  return results->Size() - iInitialSize;
}

bool PVR::CPVREpgContainer::AutoCreateTablesHook(void)
{
  return CreateChannelEpgs();
}

CEpg* PVR::CPVREpgContainer::CreateEpg(int iEpgId)
{
  CPVRChannel *channel = (CPVRChannel *) g_PVRChannelGroups->GetChannelByEpgId(iEpgId);
  if (channel)
  {
    return new CPVREpg(channel, false);
  }
  else
  {
    CLog::Log(LOGERROR, "PVREpgContainer - %s - cannot find channel '%d'. not creating an EPG table.",
        __FUNCTION__, iEpgId);
    return NULL;
  }
}

const CDateTime PVR::CPVREpgContainer::GetFirstEPGDate(bool bRadio /* = false */)
{
  // TODO should use two separate containers, one for radio, one for tv
  return CEpgContainer::GetFirstEPGDate();
}

const CDateTime PVR::CPVREpgContainer::GetLastEPGDate(bool bRadio /* = false */)
{
  // TODO should use two separate containers, one for radio, one for tv
  return CEpgContainer::GetLastEPGDate();
}

int PVR::CPVREpgContainer::GetEPGSearch(CFileItemList* results, const PVREpgSearchFilter &filter)
{
  /* get filtered results from all tables */
  CSingleLock lock(m_critSection);
  for (unsigned int iEpgPtr = 0; iEpgPtr < size(); iEpgPtr++)
    ((CPVREpg *)at(iEpgPtr))->Get(results, filter);
  lock.Leave();

  /* remove duplicate entries */
  if (filter.m_bPreventRepeats)
    EpgSearchFilter::RemoveDuplicates(results);

  /* filter recordings */
  if (filter.m_bIgnorePresentRecordings)
    PVREpgSearchFilter::FilterRecordings(results);

  /* filter timers */
  if (filter.m_bIgnorePresentTimers)
    PVREpgSearchFilter::FilterRecordings(results);

  return results->Size();
}

int PVR::CPVREpgContainer::GetEPGNow(CFileItemList* results, bool bRadio)
{
  CPVRChannelGroup *channels = g_PVRChannelGroups->GetGroupAll(bRadio);
  CSingleLock lock(m_critSection);
  int iInitialSize           = results->Size();

  for (int iChannelPtr = 0; iChannelPtr < channels->GetNumChannels(); iChannelPtr++)
  {
    CPVRChannel *channel = (CPVRChannel *) channels->GetByIndex(iChannelPtr);
    CPVREpg *epg = channel->GetEPG();
    if (!epg->HasValidEntries())
      continue;

    const CPVREpgInfoTag *epgNow = (CPVREpgInfoTag *) epg->InfoTagNow();
    if (!epgNow)
      continue;

    CFileItemPtr entry(new CFileItem(*epgNow));
    entry->SetLabel2(epgNow->StartAsLocalTime().GetAsLocalizedTime("", false));
    entry->m_strPath = channel->ChannelName();
    entry->SetThumbnailImage(channel->IconPath());
    results->Add(entry);
  }

  return results->Size() - iInitialSize;
}

int PVR::CPVREpgContainer::GetEPGNext(CFileItemList* results, bool bRadio)
{
  CPVRChannelGroup *channels = g_PVRChannelGroups->GetGroupAll(bRadio);
  CSingleLock lock(m_critSection);
  int iInitialSize           = results->Size();

  for (int iChannelPtr = 0; iChannelPtr < channels->GetNumChannels(); iChannelPtr++)
  {
    CPVRChannel *channel = (CPVRChannel *) channels->GetByIndex(iChannelPtr);
    CPVREpg *epg = channel->GetEPG();
    if (!epg->HasValidEntries())
      continue;

    const CPVREpgInfoTag *epgNext = (CPVREpgInfoTag *) epg->InfoTagNext();
    if (!epgNext)
      continue;

    CFileItemPtr entry(new CFileItem(*epgNext));
    entry->SetLabel2(epgNext->StartAsLocalTime().GetAsLocalizedTime("", false));
    entry->m_strPath = channel->ChannelName();
    entry->SetThumbnailImage(channel->IconPath());
    results->Add(entry);
  }

  return results->Size() - iInitialSize;
}

bool PVR::CPVREpgContainer::InterruptUpdate(void) const
{
  return (CEpgContainer::InterruptUpdate() ||
      (g_guiSettings.GetBool("epg.preventupdateswhileplayingtv") &&
       g_PVRManager.IsStarted() &&
       g_PVRManager.IsPlaying()));
}

bool CPVREpgContainer::CheckPlayingEvents(void)
{
  if (CEpgContainer::CheckPlayingEvents())
  {
    m_iLastEpgActiveTagCheck -= m_iLastEpgActiveTagCheck % 60;
    return true;
  }
  return false;
}
