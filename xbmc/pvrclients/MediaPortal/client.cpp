/*
 *      Copyright (C) 2005-2011 Team XBMC
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
 *
 */

#include "client.h"
#include "xbmc_pvr_dll.h"
#include "pvrclient-mediaportal.h"
#include "utils.h"

using namespace std;

/* User adjustable settings are saved here.
 * Default values are defined inside client.h
 * and exported to the other source files.
 */
std::string g_szHostname           = DEFAULT_HOST;         ///< The Host name or IP of the MediaPortal TV Server
int         g_iPort                = DEFAULT_PORT;         ///< The TVServerXBMC listening port (default: 9596)
int         g_iConnectTimeout      = DEFAULT_TIMEOUT;      ///< The Socket connection timeout
int         g_iSleepOnRTSPurl      = DEFAULT_SLEEP_RTSP_URL; ///< An optional delay between tuning a channel and opening the corresponding RTSP stream in XBMC (default: 0)
bool        g_bOnlyFTA             = DEFAULT_FTA_ONLY;     ///< Send only Free-To-Air Channels inside Channel list to XBMC
bool        g_bRadioEnabled        = DEFAULT_RADIO;        ///< Send also Radio channels list to XBMC
bool        g_bHandleMessages      = DEFAULT_HANDLE_MSG;   ///< Send VDR's OSD status messages to XBMC OSD
bool        g_bResolveRTSPHostname = DEFAULT_RESOLVE_RTSP_HOSTNAME; ///< Resolve the server hostname in the rtsp URLs to an IP at the TV Server side (default: false)
bool        g_bReadGenre           = DEFAULT_READ_GENRE;   ///< Read the genre strings from MediaPortal and translate them into XBMC DVB genre id's (only English)
bool        g_bUseRecordingsDir    = DEFAULT_USE_REC_DIR;  ///< Use a normal directory if true for recordings
std::string g_szRecordingsDir      = DEFAULT_REC_DIR;      ///< The path to the recordings directory
std::string g_szTVGroup            = DEFAULT_TVGROUP;      ///< Import only TV channels from this TV Server TV group
std::string g_szRadioGroup         = DEFAULT_RADIOGROUP;   ///< Import only radio channels from this TV Server radio group
bool        g_bDirectTSFileRead    = DEFAULT_DIRECT_TS_FR; ///< Open the Live-TV timeshift buffer directly (skip RTSP streaming)

/* Client member variables */
ADDON_STATUS           m_CurStatus    = ADDON_STATUS_UNKNOWN;
cPVRClientMediaPortal *g_client       = NULL;
bool                   g_bCreated     = false;
int                    g_iClientID    = -1;
std::string            g_szUserPath   = "";
std::string            g_szClientPath = "";
CHelper_libXBMC_addon *XBMC           = NULL;
CHelper_libXBMC_pvr   *PVR            = NULL;

extern "C" {

/***********************************************************
 * Standard AddOn related public library functions
 ***********************************************************/

//-- Create -------------------------------------------------------------------
// Called after loading of the dll, all steps to become Client functional
// must be performed here.
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_Create(void* hdl, void* props)
{
  if (!hdl || !props)
    return ADDON_STATUS_UNKNOWN;

  PVR_PROPERTIES* pvrprops = (PVR_PROPERTIES*)props;

  XBMC = new CHelper_libXBMC_addon;
  if (!XBMC->RegisterMe(hdl))
  {
    delete_null(XBMC);
    return ADDON_STATUS_UNKNOWN;
  }

  PVR = new CHelper_libXBMC_pvr;
  if (!PVR->RegisterMe(hdl))
  {
    delete_null(PVR);
    delete_null(XBMC);
    return ADDON_STATUS_UNKNOWN;
  }

#ifdef TSREADER
  XBMC->Log(LOG_DEBUG, "Creating MediaPortal PVR-Client (TSReader version)");
#else
  XBMC->Log(LOG_DEBUG, "Creating MediaPortal PVR-Client (ffmpeg rtsp version)");
#endif

  m_CurStatus    = ADDON_STATUS_UNKNOWN;
  g_client       = new cPVRClientMediaPortal();
  g_iClientID    = pvrprops->iClientId;
  g_szUserPath   = pvrprops->strUserPath;
  g_szClientPath = pvrprops->strClientPath;

  /* Read setting "host" from settings.xml */
  char buffer[1024];

  if (XBMC->GetSetting("host", &buffer))
  {
    g_szHostname = buffer;
    uri::decode(g_szHostname);
  }
  else
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'host' setting, falling back to '127.0.0.1' as default");
    g_szHostname = DEFAULT_HOST;
  }

  /* Read setting "port" from settings.xml */
  if (!XBMC->GetSetting("port", &g_iPort))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'port' setting, falling back to '9596' as default");
    g_iPort = DEFAULT_PORT;
  }

  /* Read setting "ftaonly" from settings.xml */
  if (!XBMC->GetSetting("ftaonly", &g_bOnlyFTA))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'ftaonly' setting, falling back to 'false' as default");
    g_bOnlyFTA = DEFAULT_FTA_ONLY;
  }

  /* Read setting "useradio" from settings.xml */
  if (!XBMC->GetSetting("useradio", &g_bRadioEnabled))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'useradio' setting, falling back to 'true' as default");
    g_bRadioEnabled = DEFAULT_RADIO;
  }

  /* Read setting "timeout" from settings.xml */
  if (!XBMC->GetSetting("timeout", &g_iConnectTimeout))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'timeout' setting, falling back to %i seconds as default", DEFAULT_TIMEOUT);
    g_iConnectTimeout = DEFAULT_TIMEOUT;
  }

  if (!XBMC->GetSetting("tvgroup", &buffer))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'tvgroup' setting, falling back to '' as default");
  } else {
    g_szTVGroup = buffer;
  }

  if (!XBMC->GetSetting("radiogroup", &buffer))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'tvgroup' setting, falling back to '' as default");
  } else {
    g_szRadioGroup = buffer;
  }

#ifndef TSREADER
  /* Read setting "resolvertsphostname" from settings.xml */
  if (!XBMC->GetSetting("resolvertsphostname", &g_bResolveRTSPHostname))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'resolvertsphostname' setting, falling back to 'true' as default");
    g_bResolveRTSPHostname = DEFAULT_RESOLVE_RTSP_HOSTNAME;
  }
#endif

  /* Read setting "readgenre" from settings.xml */
  if (!XBMC->GetSetting("readgenre", &g_bReadGenre))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'resolvertsphostname' setting, falling back to 'true' as default");
    g_bReadGenre = DEFAULT_READ_GENRE;
  }

  /* Read setting "sleeponrtspurl" from settings.xml */
  if (!XBMC->GetSetting("sleeponrtspurl", &g_iSleepOnRTSPurl))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'sleeponrtspurl' setting, falling back to %i seconds as default", DEFAULT_SLEEP_RTSP_URL);
    g_iSleepOnRTSPurl = DEFAULT_SLEEP_RTSP_URL;
  }

  /* Read setting "userecordingsdir" from settings.xml */
  if (!XBMC->GetSetting("userecordingsdir", &g_bUseRecordingsDir))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'userecordingsdir' setting, falling back to 'false' as default");
    g_bReadGenre = DEFAULT_USE_REC_DIR;
  }

  if (!XBMC->GetSetting("recordingsdir", &buffer))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'recordingsdir' setting, falling back to '%s' as default", DEFAULT_REC_DIR);
  } else {
    g_szRecordingsDir = buffer;
  }
#ifdef TSREADER
  /* Read setting "directtsfileread" from settings.xml */
  if (!XBMC->GetSetting("directtsfileread", &g_bDirectTSFileRead))
  {
    /* If setting is unknown fallback to defaults */
    XBMC->Log(LOG_ERROR, "Couldn't get 'directtsfileread' setting, falling back to 'false' as default");
    g_bDirectTSFileRead = DEFAULT_DIRECT_TS_FR;
  }
#else
  g_bDirectTSFileRead = false;
#endif
  /* Create connection to MediaPortal XBMC TV client */
  if (!g_client->Connect())
  {
    delete_null(g_client);
    delete_null(PVR);
    delete_null(XBMC);
    m_CurStatus = ADDON_STATUS_LOST_CONNECTION;
  }
  else
  {
    m_CurStatus = ADDON_STATUS_OK;
  }

  g_bCreated = true;

  return m_CurStatus;
}

//-- Destroy ------------------------------------------------------------------
// Used during destruction of the client, all steps to do clean and safe Create
// again must be done.
//-----------------------------------------------------------------------------
void ADDON_Destroy()
{
  if ((g_bCreated) && (g_client))
  {
    g_client->Disconnect();
    delete_null(g_client);

    g_bCreated = false;
  }

  if (PVR)
  {
    delete_null(PVR);
  }
  if (XBMC)
  {
    delete_null(XBMC);
  }

  m_CurStatus = ADDON_STATUS_UNKNOWN;
}

//-- GetStatus ----------------------------------------------------------------
// Report the current Add-On Status to XBMC
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_GetStatus()
{
  return m_CurStatus;
}

//-- HasSettings --------------------------------------------------------------
// Report "true", yes this AddOn have settings
//-----------------------------------------------------------------------------
bool ADDON_HasSettings()
{
  return true;
}

unsigned int ADDON_GetSettings(ADDON_StructSetting ***sSet)
{
  return 0;
}

//-- SetSetting ---------------------------------------------------------------
// Called everytime a setting is changed by the user and to inform AddOn about
// new setting and to do required stuff to apply it.
//-----------------------------------------------------------------------------
ADDON_STATUS ADDON_SetSetting(const char *settingName, const void *settingValue)
{
  string str = settingName;

  // SetSetting can occur when the addon is enabled, but TV support still
  // disabled. In that case the addon is not loaded, so we should not try
  // to change its settings.
  if (!g_bCreated)
    return ADDON_STATUS_OK;

  if (str == "host")
  {
    string tmp_sHostname;
    XBMC->Log(LOG_INFO, "Changed Setting 'host' from %s to %s", g_szHostname.c_str(), (const char*) settingValue);
    tmp_sHostname = g_szHostname;
    g_szHostname = (const char*) settingValue;
    if (tmp_sHostname != g_szHostname)
      return ADDON_STATUS_NEED_RESTART;
  }
  else if (str == "port")
  {
    XBMC->Log(LOG_INFO, "Changed Setting 'port' from %u to %u", g_iPort, *(int*) settingValue);
    if (g_iPort != *(int*) settingValue)
    {
      g_iPort = *(int*) settingValue;
      return ADDON_STATUS_NEED_RESTART;
    }
  }
  else if (str == "ftaonly")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'ftaonly' from %u to %u", g_bOnlyFTA, *(bool*) settingValue);
    g_bOnlyFTA = *(bool*) settingValue;
  }
  else if (str == "useradio")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'useradio' from %u to %u", g_bRadioEnabled, *(bool*) settingValue);
    g_bRadioEnabled = *(bool*) settingValue;
  }
  else if (str == "timeout")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'timeout' from %u to %u", g_iConnectTimeout, *(int*) settingValue);
    g_iConnectTimeout = *(int*) settingValue;
  }
  else if (str == "tvgroup")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'tvgroup' from %s to %s", g_szTVGroup.c_str(), (const char*) settingValue);
    g_szTVGroup = (const char*) settingValue;
  }
  else if (str == "radiogroup")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'radiogroup' from %s to %s", g_szRadioGroup.c_str(), (const char*) settingValue);
    g_szRadioGroup = (const char*) settingValue;
  }
#ifndef TSREADER
  else if (str == "resolvertsphostname")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'resolvertsphostname' from %u to %u", g_bResolveRTSPHostname, *(bool*) settingValue);
    g_bResolveRTSPHostname = *(bool*) settingValue;
  }
#endif
  else if (str == "readgenre")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'readgenre' from %u to %u", g_bReadGenre, *(bool*) settingValue);
    g_bReadGenre = *(bool*) settingValue;
  }
  else if (str == "sleeponrtspurl")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'sleeponrtspurl' from %u to %u", g_iSleepOnRTSPurl, *(int*) settingValue);
    g_iSleepOnRTSPurl = *(int*) settingValue;
  }
  else if (str == "userecordingsdir")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'userecordingsdir' from %u to %u", g_bUseRecordingsDir, *(bool*) settingValue);
    g_bUseRecordingsDir = *(bool*) settingValue;
  }
  else if (str == "recordingsdir")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'recordingsdir' from %s to %s", g_szRecordingsDir.c_str(), (const char*) settingValue);
    g_szRecordingsDir = (const char*) settingValue;
  }
#ifdef TSREADER
  else if (str == "directtsfileread")
  {
    XBMC->Log(LOG_INFO, "Changed setting 'directtsfileread' from %u to %u", g_bDirectTSFileRead, *(bool*) settingValue);
    g_bDirectTSFileRead = *(bool*) settingValue;
  }
#endif
  return ADDON_STATUS_OK;
}

void ADDON_Stop()
{
  ADDON_Destroy();
}

void ADDON_FreeSettings()
{

}

/***********************************************************
 * PVR Client AddOn specific public library functions
 ***********************************************************/

//-- GetAddonCapabilities ------------------------------------------------------------
// Tell XBMC our requirements
//-----------------------------------------------------------------------------
PVR_ERROR GetAddonCapabilities(PVR_ADDON_CAPABILITIES *pCapabilities)
{
  XBMC->Log(LOG_DEBUG, "->GetProperties()");

  pCapabilities->bSupportsTimeshift          = false;
  pCapabilities->bSupportsEPG                = true;
  pCapabilities->bSupportsRecordings         = true;
  pCapabilities->bSupportsTimers             = true;
  pCapabilities->bSupportsTV                 = true;
  pCapabilities->bSupportsRadio              = g_bRadioEnabled;
  pCapabilities->bSupportsChannelSettings    = true;
  pCapabilities->bSupportsChannelGroups      = true;
  pCapabilities->bHandlesInputStream         = true;
  pCapabilities->bHandlesDemuxing            = false;
  pCapabilities->bSupportsChannelScan        = false;

  return PVR_ERROR_NO_ERROR;
}

PVR_ERROR GetStreamProperties(PVR_STREAM_PROPERTIES *pProperties)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

//-- GetBackendName -----------------------------------------------------------
// Return the Name of the Backend
//-----------------------------------------------------------------------------
const char * GetBackendName(void)
{
  return g_client->GetBackendName();
}

//-- GetBackendVersion --------------------------------------------------------
// Return the Version of the Backend as String
//-----------------------------------------------------------------------------
const char * GetBackendVersion(void)
{
  return g_client->GetBackendVersion();
}

//-- GetConnectionString ------------------------------------------------------
// Return a String with connection info, if available
//-----------------------------------------------------------------------------
const char * GetConnectionString(void)
{
  return g_client->GetConnectionString();
}

//-- GetDriveSpace ------------------------------------------------------------
// Return the Total and Free Drive space on the PVR Backend
//-----------------------------------------------------------------------------
PVR_ERROR GetDriveSpace(long long *iTotal, long long *iUsed)
{
  return g_client->GetDriveSpace(iTotal, iUsed);
}

PVR_ERROR GetBackendTime(time_t *localTime, int *gmtOffset)
{
  return g_client->GetMPTVTime(localTime, gmtOffset);
}

PVR_ERROR DialogChannelScan()
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR CallMenuHook(const PVR_MENUHOOK &menuhook)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


/*******************************************/
/** PVR EPG Functions                     **/

PVR_ERROR GetEPGForChannel(PVR_HANDLE handle, const PVR_CHANNEL &channel, time_t iStart, time_t iEnd)
{
  return g_client->GetEpg(handle, channel, iStart, iEnd);
}


/*******************************************/
/** PVR Channel Functions                 **/

int GetChannelsAmount()
{
  return g_client->GetNumChannels();
}

PVR_ERROR GetChannels(PVR_HANDLE handle, bool bRadio)
{
  return g_client->GetChannels(handle, bRadio);
}

PVR_ERROR DeleteChannel(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR RenameChannel(const PVR_CHANNEL &channel)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR DialogChannelSettings(const PVR_CHANNEL &channelinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}

PVR_ERROR DialogAddChannel(const PVR_CHANNEL &channelinfo)
{
  return PVR_ERROR_NOT_IMPLEMENTED;
}


/*******************************************/
/** PVR Channel group Functions           **/

int GetChannelGroupsAmount(void)
{
  return g_client->GetChannelGroupsAmount();
}

PVR_ERROR GetChannelGroups(PVR_HANDLE handle, bool bRadio)
{
  return g_client->GetChannelGroups(handle, bRadio);
}

PVR_ERROR GetChannelGroupMembers(PVR_HANDLE handle, const PVR_CHANNEL_GROUP &group)
{
  return g_client->GetChannelGroupMembers(handle, group);
}


/*******************************************/
/** PVR Recording Functions               **/

int GetRecordingsAmount(void)
{
  return g_client->GetNumRecordings();
}

PVR_ERROR GetRecordings(PVR_HANDLE handle)
{
  return g_client->GetRecordings(handle);
}

PVR_ERROR DeleteRecording(const PVR_RECORDING &recording)
{
  return g_client->DeleteRecording(recording);
}

PVR_ERROR RenameRecording(const PVR_RECORDING &recording)
{
  return g_client->RenameRecording(recording);
}


/*******************************************/
/** PVR Timer Functions                   **/

int GetTimersAmount(void)
{
  return g_client->GetNumTimers();
}

PVR_ERROR GetTimers(PVR_HANDLE handle)
{
  return g_client->GetTimers(handle);
}

PVR_ERROR AddTimer(const PVR_TIMER &timer)
{
  return g_client->AddTimer(timer);
}

PVR_ERROR DeleteTimer(const PVR_TIMER &timer, bool bForceDelete)
{
  return g_client->DeleteTimer(timer, bForceDelete);
}

PVR_ERROR UpdateTimer(const PVR_TIMER &timer)
{
  return g_client->UpdateTimer(timer);
}


/*******************************************/
/** PVR Live Stream Functions             **/

bool OpenLiveStream(const PVR_CHANNEL &channelinfo)
{
  return g_client->OpenLiveStream(channelinfo);
}

void CloseLiveStream()
{
  return g_client->CloseLiveStream();
}

int ReadLiveStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  return g_client->ReadLiveStream(pBuffer, iBufferSize);
}

int GetCurrentClientChannel()
{
  return g_client->GetCurrentClientChannel();
}

bool SwitchChannel(const PVR_CHANNEL &channelinfo)
{
  return g_client->SwitchChannel(channelinfo);
}

PVR_ERROR SignalStatus(PVR_SIGNAL_STATUS &signalStatus)
{
  return g_client->SignalStatus(signalStatus);
}

/*******************************************/
/** PVR Recording Stream Functions        **/

bool OpenRecordedStream(const PVR_RECORDING &recording)
{
  return g_client->OpenRecordedStream(recording);
}

void CloseRecordedStream(void)
{
  return g_client->CloseRecordedStream();
}

int ReadRecordedStream(unsigned char *pBuffer, unsigned int iBufferSize)
{
  return g_client->ReadRecordedStream(pBuffer, iBufferSize);
}

const char * GetLiveStreamURL(const PVR_CHANNEL &channel)
{
  return g_client->GetLiveStreamURL(channel);
}

/** UNUSED API FUNCTIONS */
PVR_ERROR MoveChannel(const PVR_CHANNEL &channel) { return PVR_ERROR_NOT_IMPLEMENTED; }
DemuxPacket* DemuxRead(void) { return NULL; }
void DemuxAbort(void) {}
void DemuxReset(void) {}
void DemuxFlush(void) {}

long long SeekRecordedStream(long long iPosition, int iWhence) { return -1; }
long long PositionRecordedStream(void) { return -1; }
long long LengthRecordedStream(void) { return -1; }

long long SeekLiveStream(long long pos, int whence) { return -1; }
long long PositionLiveStream(void) { return -1; }
long long LengthLiveStream(void) { return -1 ; }

} //end extern "C"
