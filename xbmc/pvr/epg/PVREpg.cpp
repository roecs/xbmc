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

#include "settings/AdvancedSettings.h"
#include "settings/GUISettings.h"
#include "threads/SingleLock.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"

#include "PVREpg.h"
#include "pvr/PVRManager.h"
#include "pvr/channels/PVRChannelGroupsContainer.h"
#include "pvr/addons/PVRClients.h"
#include "epg/EpgContainer.h"
#include "epg/EpgDatabase.h"
#include "PVREpgSearchFilter.h"

using namespace PVR;
using namespace EPG;

PVR::CPVREpg::CPVREpg(CPVRChannel *channel, bool bLoadedFromDb /* = false */) :
  CEpg(channel->EpgID(), channel->ChannelName(), channel->EPGScraper(), bLoadedFromDb)
{
  SetChannel(channel);
}

bool PVR::CPVREpg::HasValidEntries(void) const
{
  CSingleLock lock(m_critSection);

  return m_Channel != NULL && m_Channel->ChannelID() > 0 && CEpg::HasValidEntries();
}

bool PVR::CPVREpg::IsRemovableTag(const CEpgInfoTag *tag) const
{
  const CPVREpgInfoTag *epgTag = (CPVREpgInfoTag *) tag;
  return (!epgTag || !epgTag->HasTimer());
}

void PVR::CPVREpg::Clear(void)
{
  CSingleLock lock(m_critSection);

  if (m_Channel)
    m_Channel->m_EPG = NULL;

  CEpg::Clear();
}

bool PVR::CPVREpg::UpdateEntry(const EPG_TAG *data, bool bUpdateDatabase /* = false */)
{
  if (!data)
    return false;

  CPVREpgInfoTag tag(*data);
  return CEpg::UpdateEntry(tag, bUpdateDatabase);
}

bool PVR::CPVREpg::UpdateFromScraper(time_t start, time_t end)
{
  bool bGrabSuccess = false;

  if (m_Channel && m_Channel->EPGEnabled() && ScraperName() == "client")
  {
    if (g_PVRClients->GetAddonCapabilities(m_Channel->ClientID())->bSupportsEPG)
    {
      CLog::Log(LOGINFO, "%s - updating EPG for channel '%s' from client '%i'",
          __FUNCTION__, m_Channel->ChannelName().c_str(), m_Channel->ClientID());
      PVR_ERROR error;
      g_PVRClients->GetEPGForChannel(*m_Channel, this, start, end, &error);
      bGrabSuccess = error == PVR_ERROR_NO_ERROR;
    }
    else
    {
      CLog::Log(LOGINFO, "%s - channel '%s' on client '%i' does not support EPGs",
          __FUNCTION__, m_Channel->ChannelName().c_str(), m_Channel->ClientID());
    }
  }
  else
  {
    bGrabSuccess = CEpg::UpdateFromScraper(start, end);
  }

  return bGrabSuccess;
}

bool PVR::CPVREpg::IsRadio(void) const
{
  return m_Channel->IsRadio();
}

bool PVR::CPVREpg::Update(const CEpg &epg, bool bUpdateDb /* = false */)
{
  bool bReturn = CEpg::Update(epg, false); // don't update the db yet

  SetChannel((CPVRChannel*) epg.Channel());

  if (bUpdateDb)
    bReturn = Persist(false);

  return bReturn;
}

CEpgInfoTag *PVR::CPVREpg::CreateTag(void)
{
  CEpgInfoTag *newTag = new CPVREpgInfoTag();
  if (!newTag)
  {
    CLog::Log(LOGERROR, "PVREPG - %s - couldn't create new infotag",
        __FUNCTION__);
  }

  return newTag;
}

bool PVR::CPVREpg::LoadFromClients(time_t start, time_t end)
{
  bool bReturn(false);
  if (m_Channel)
  {
    CPVREpg tmpEpg(m_Channel);
    if (tmpEpg.UpdateFromScraper(start, end))
      bReturn = UpdateEntries(tmpEpg, !g_guiSettings.GetBool("epg.ignoredbforclient"));
  }
  else
  {
    CLog::Log(LOGERROR, "PVREPG - %s - no channel tag set for table '%s' id %d",
        __FUNCTION__, m_strName.c_str(), m_iEpgID);
  }

  return bReturn;
}

int PVR::CPVREpg::Get(CFileItemList *results, const PVREpgSearchFilter &filter) const
{
  int iInitialSize = results->Size();

  if (!HasValidEntries())
    return -1;

  CSingleLock lock(m_critSection);

  for (unsigned int iTagPtr = 0; iTagPtr < size(); iTagPtr++)
  {
    CPVREpgInfoTag *tag = (CPVREpgInfoTag *) at(iTagPtr);
    if (filter.FilterEntry(*tag))
    {
      CFileItemPtr entry(new CFileItem(*at(iTagPtr)));
      entry->SetLabel2(at(iTagPtr)->StartAsLocalTime().GetAsLocalizedDateTime(false, false));
      results->Add(entry);
    }
  }

  return size() - iInitialSize;
}
