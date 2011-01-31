#pragma once

/*
 *      Copyright (C) 2005-2008 Team XBMC
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

#include "DVDAudioCodec.h"
//todoo move this file
#include "DVDCodecs/Video/DllDSNative.h"
#include "DSNative/dumpuids.h"
 
const enum PCMChannels standardChannelMasks[][8] = 
{
  {PCM_FRONT_CENTER},//1
  {PCM_FRONT_LEFT, PCM_FRONT_RIGHT},//2
  {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_LOW_FREQUENCY},//3
  {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_BACK_LEFT, PCM_BACK_RIGHT},//4
  {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_LOW_FREQUENCY, PCM_BACK_LEFT, PCM_BACK_RIGHT},//5
  {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_LOW_FREQUENCY, PCM_BACK_LEFT, PCM_BACK_RIGHT},//6
  {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_LOW_FREQUENCY, PCM_BACK_CENTER, PCM_BACK_LEFT, PCM_BACK_RIGHT},//7
  {PCM_FRONT_LEFT, PCM_FRONT_RIGHT, PCM_FRONT_CENTER, PCM_LOW_FREQUENCY, PCM_BACK_LEFT, PCM_BACK_RIGHT, PCM_SIDE_LEFT, PCM_SIDE_RIGHT}//8
};


class CDVDAudioCodecDirectshow 
  : public CDVDAudioCodec
{
public:
  CDVDAudioCodecDirectshow();
  virtual ~CDVDAudioCodecDirectshow();
  virtual bool Open(CDVDStreamInfo &hints, CDVDCodecOptions &options);
  virtual void Dispose();
  virtual int Decode(BYTE* pData, int iSize);
  virtual int GetData(BYTE** dst);
  virtual void Reset();
  virtual enum PCMChannels *GetChannelMap();
  virtual int GetChannels();
  virtual int GetSampleRate();
  virtual int GetBitsPerSample();
  virtual int GetBufferSize();
  virtual const char* GetName() { return "DirectShow"; }
protected:
  DSAudioCodec *codec;
  int m_pCurrentDataSize;

  std::vector<BYTE> m_decodedData;
  int  m_iDecodedDataSize;

  std::vector<BYTE> m_inputBuffer;
  int m_iInputBufferSize;

  enum PCMChannels m_channelMap[PCM_MAX_CH + 1];
  int64_t          m_layout;
  int              m_channels;

  int m_iBuffered;
  WAVEFORMATEX *m_pWaveOut;//WAVEFORMATEX for the renderer
  WAVEFORMATEXTENSIBLE *m_pWaveOutExt; //WAVEFORMATEXTENSIBLE for the renderer
  CMediaType *wfmtTypeOut;//wave header for the outputpin
};

class CDSAudioSettings
{
public:
  CDSAudioSettings()
  {
    m_pWaveFormat = NULL;
  };
  ~CDSAudioSettings();
  void SetWaveFormat(WAVEFORMATEXTENSIBLE *wave) 
  { 
    m_pWaveFormat = wave;
  }
  WAVEFORMATEXTENSIBLE *GetWaveFormat()
  {
    return m_pWaveFormat;
  }
protected:
  WAVEFORMATEXTENSIBLE *m_pWaveFormat;
};

extern CDSAudioSettings g_dsAudioSettings;