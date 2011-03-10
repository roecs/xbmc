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

#ifndef WIN32DIRECTSHOWAUDIORENDERER_H
#define WIN32DIRECTSHOWAUDIORENDERER_H

#if _MSC_VER > 1000
#pragma once
#endif // _MSC_VER > 1000

#include "IAudioRenderer.h"
#include "cores/IAudioCallback.h"

extern void RegisterAudioCallback(IAudioCallback* pCallback);
extern void UnRegisterAudioCallback();

class CWin32DirectShowAudioRenderer : public IAudioRenderer
{
public:
  virtual void UnRegisterAudioCallback();
  virtual void RegisterAudioCallback(IAudioCallback* pCallback);
  virtual unsigned int GetChunkLen();
  virtual float GetDelay();
  virtual float GetCacheTime();
  CWin32DirectShowAudioRenderer();
  virtual bool Initialize(IAudioCallback* pCallback, const CStdString& device, int iChannels, enum PCMChannels *channelMap, unsigned int uiSamplesPerSec, unsigned int uiBitsPerSample, bool bResample, bool bIsMusic=false, bool bPassthrough = false);
  virtual ~CWin32DirectShowAudioRenderer();

  virtual unsigned int AddPackets(const void* data, unsigned int len);
  virtual unsigned int GetSpace();
  virtual bool Deinitialize();
  virtual bool Pause();
  virtual bool Stop();
  virtual bool Resume();

  virtual long GetCurrentVolume() const;
  virtual void Mute(bool bMute);
  virtual bool SetCurrentVolume(long nVolume);
  virtual int SetPlaySpeed(int iSpeed);
  virtual void WaitCompletion();
  virtual void DoWork();
  virtual void SwitchChannels(int iAudioStream, bool bAudioOnAllSpeakers);

  virtual void Flush();
  static void EnumerateAudioSinks(AudioSinkList& vAudioSinks, bool passthrough);
private:
  long m_nCurrentVolume;

  float m_timePerPacket;
  int m_packetsSent;
  bool m_paused;
  long m_lastUpdate;

  void Update();
};

#endif
