/*
 *      Copyright (C) 2010 Team XBMC
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

#include "Win32DirectShowAudioRenderer.h"
#include "guilib/AudioContext.h"
#include "Application.h"
#include "utils/log.h"
#include "utils/TimeUtils.h"
#include "../../lib/dsnative/SmartPtr.h"
#include "Dshow.h" //ICreateDevEnum
#define BUFFER CHUNKLEN * 20
#define CHUNKLEN 512

#define BeginEnumSysDev(clsid, pMoniker) \
{Com::SmartPtr<ICreateDevEnum> pDevEnum4$##clsid; \
  pDevEnum4$##clsid.CoCreateInstance(CLSID_SystemDeviceEnum); \
  Com::SmartPtr<IEnumMoniker> pClassEnum4$##clsid; \
  if(SUCCEEDED(pDevEnum4$##clsid->CreateClassEnumerator(clsid, &pClassEnum4$##clsid, 0)) \
  && pClassEnum4$##clsid) \
{ \
  for(Com::SmartPtr<IMoniker> pMoniker; pClassEnum4$##clsid->Next(1, &pMoniker, 0) == S_OK; pMoniker = NULL) \
{ \

#define EndEnumSysDev }}}

void CWin32DirectShowAudioRenderer::DoWork()
{
}

//////////////////////////////////////////////////////////////////////
// Construction/Destruction
//////////////////////////////////////////////////////////////////////
//***********************************************************************************************
CWin32DirectShowAudioRenderer::CWin32DirectShowAudioRenderer()
{
}
bool CWin32DirectShowAudioRenderer::Initialize(IAudioCallback* pCallback, const CStdString& device, int iChannels, enum PCMChannels *channelMap, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool bResample, bool bIsMusic, bool bPassthrough)
{
  CLog::Log(LOGERROR,"Creating a Null Audio Renderer, Check your audio settings as this should not happen");
  if (iChannels == 0)
    iChannels = 2;

  bool bAudioOnAllSpeakers(false);
  g_audioContext.SetupSpeakerConfig(iChannels, bAudioOnAllSpeakers, bIsMusic);
  g_audioContext.SetActiveDevice(CAudioContext::DIRECTSOUND_DEVICE);

  m_timePerPacket = 1.0f / (float)(iChannels*(uiBitsPerSample/8) * uiSamplesPerSec);
  m_packetsSent = 0;
  m_paused = 0;
  m_lastUpdate = CTimeUtils::GetTimeMS();
  return true;
}

//***********************************************************************************************
CWin32DirectShowAudioRenderer::~CWin32DirectShowAudioRenderer()
{
  Deinitialize();
}

void CWin32DirectShowAudioRenderer::EnumerateAudioSinks(AudioSinkList& vAudioSinks, bool passthrough)
{
  Com::SmartPtr<IPropertyBag> propBag = NULL;
  BeginEnumSysDev(CLSID_AudioRendererCategory, pMoniker)
  {
    if (SUCCEEDED(pMoniker->BindToStorage(NULL, NULL, IID_IPropertyBag, (void**) &propBag)))
    {
      _variant_t var;

      CStdStringW filterName;
      CStdStringW filterGuid;
      CStdStringA filterNameA;
      CStdStringA filterGuidA;

      if (SUCCEEDED(propBag->Read(L"FriendlyName", &var, 0)))
        filterName = CStdStringW(var.bstrVal);

      var.Clear();

      if (SUCCEEDED(propBag->Read(L"CLSID", &var, 0)))
        filterGuid = CStdStringW(var.bstrVal);
      g_charsetConverter.fromW(filterGuid, filterGuidA, "UTF-8");
      g_charsetConverter.fromW(filterName, filterNameA, "UTF-8");
      vAudioSinks.push_back(AudioSink(CStdString("directshow: ").append(filterNameA.c_str()), CStdString("directshow:").append(filterGuidA.c_str())));
      //AddFilter(pRenderers, filterGuid, filterName);
      propBag = NULL;
    }
    else
      return;
  }
  EndEnumSysDev;
  
  
}

//***********************************************************************************************
bool CWin32DirectShowAudioRenderer::Deinitialize()
{
  g_audioContext.SetActiveDevice(CAudioContext::DEFAULT_DEVICE);
  return true;
}

void CWin32DirectShowAudioRenderer::Flush()
{
  m_lastUpdate = CTimeUtils::GetTimeMS();
  m_packetsSent = 0;
  Pause();
}

//***********************************************************************************************
bool CWin32DirectShowAudioRenderer::Pause()
{
  m_paused = true;
  return true;
}

//***********************************************************************************************
bool CWin32DirectShowAudioRenderer::Resume()
{
  m_paused = false;
  return true;
}

//***********************************************************************************************
bool CWin32DirectShowAudioRenderer::Stop()
{
  Flush();
  return true;
}

//***********************************************************************************************
long CWin32DirectShowAudioRenderer::GetCurrentVolume() const
{
  return m_nCurrentVolume;
}

//***********************************************************************************************
void CWin32DirectShowAudioRenderer::Mute(bool bMute)
{
}

//***********************************************************************************************
bool CWin32DirectShowAudioRenderer::SetCurrentVolume(long nVolume)
{
  m_nCurrentVolume = nVolume;
  return true;
}


//***********************************************************************************************
unsigned int CWin32DirectShowAudioRenderer::GetSpace()
{
  Update();

  if(BUFFER > m_packetsSent)
    return (int)BUFFER - m_packetsSent;
  else
    return 0;
}

//***********************************************************************************************
unsigned int CWin32DirectShowAudioRenderer::AddPackets(const void* data, unsigned int len)
{
  if (m_paused || GetSpace() == 0)
    return 0;

  int add = ( len / GetChunkLen() ) * GetChunkLen();
  m_packetsSent += add;

  return add;
}

//***********************************************************************************************
float CWin32DirectShowAudioRenderer::GetDelay()
{
  Update();

  return m_timePerPacket * (float)m_packetsSent;
}

float CWin32DirectShowAudioRenderer::GetCacheTime()
{
  return GetDelay();
}

//***********************************************************************************************
unsigned int CWin32DirectShowAudioRenderer::GetChunkLen()
{
  return (int)CHUNKLEN;
}
//***********************************************************************************************
int CWin32DirectShowAudioRenderer::SetPlaySpeed(int iSpeed)
{
  return 0;
}

void CWin32DirectShowAudioRenderer::RegisterAudioCallback(IAudioCallback *pCallback)
{
}

void CWin32DirectShowAudioRenderer::UnRegisterAudioCallback()
{
}

void CWin32DirectShowAudioRenderer::WaitCompletion()
{
  while(m_packetsSent > 0)
    Update();
}

void CWin32DirectShowAudioRenderer::SwitchChannels(int iAudioStream, bool bAudioOnAllSpeakers)
{
    return ;
}

void CWin32DirectShowAudioRenderer::Update()
{
  long currentTime = CTimeUtils::GetTimeMS();
  long deltaTime = (currentTime - m_lastUpdate);

  if (m_paused)
  {
    m_lastUpdate += deltaTime;
    return;
  }

  double d = (double)deltaTime / 1000.0f;

  if (currentTime != m_lastUpdate)
  {
    double i = (d / (double)m_timePerPacket);
    m_packetsSent -= (long)i;
    if (m_packetsSent < 0)
      m_packetsSent = 0;
    m_lastUpdate = currentTime;
  }
}
