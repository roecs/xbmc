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

#include "DVDVideoCodecDirectshow.h"
#include "DVDClock.h"
#include "DVDStreamInfo.h"
#include "utils/log.h"
#include "application.h"
#include "dsnative/DSCodecTag.h"
#include "dsnative/dumpuids.h"
#include "dsnative/ExtraDataParser.h"
#include "WindowingFactory.h"
#include "Mfobjects.h"
#include "Settings/AdvancedSettings.h"
#include "Charsetconverter.h"

#include "DllAvFormat.h"
#include "DllAvCodec.h"
#pragma comment(lib, "mfplat.lib")

CDVDVideoCodecDirectshow::CDVDVideoCodecDirectshow() : CDVDVideoCodec()
{
  codec = NULL;
  m_pCurrentData = NULL;
  m_requireResync = true;
  m_wait_timeout = 1;
  current_surface_index = -1;
  number_of_frame_ready = 0;
}

CDVDVideoCodecDirectshow::~CDVDVideoCodecDirectshow()
{
  Dispose();
}


bool CDVDVideoCodecDirectshow::Open(CDVDStreamInfo &hints, CDVDCodecOptions &options)
{ 
  CStdString ffdshow = CStdString("C:\\Program Files (x86)\\ffdshow\\ffdshow.ax");//Not used right now
 
  dsnerror_t err;
  bih = (BITMAPINFOHEADER*)calloc(sizeof(*bih) + hints.extrasize,1);
  bih->biSize= sizeof(*bih) + hints.extrasize;
  bih->biWidth= hints.width;
  bih->biHeight= hints.height;
  if (hints.bitspercodedsample > 0)
    bih->biBitCount = hints.bitspercodedsample;
  else
    bih->biBitCount = 12;
  bih->biSizeImage = bih->biWidth * bih->biHeight * bih->biBitCount/8;
  DllAvFormat dllAvFormat;
  dllAvFormat.Load();
  
  bih->biCompression = dllAvFormat.av_codec_get_tag(mp_bmp_taglists, hints.codec);
  int level = hints.level;
  int profile = hints.profile;

  int64_t frametime = MulDiv(DVD_TIME_BASE*10, hints.fpsscale, hints.fpsrate);
  
  m_wait_timeout = ( DVD_TIME_BASE / (hints.fpsrate / hints.fpsscale) ) / 2000;
  CStdString curfile = g_application.CurrentFile();
  CLog::Log(LOGNOTICE,"Loading directshow filter for file %s",curfile.c_str());
//Correcting some mpeg-ts extradata for some video codecs which can't handle NAL_AUD in 2 first parameter
  if (hints.extrasize > 0 && hints.profile > 0 && hints.level > 0)
  {
    try
    {
      CExtradataParser* pextra = new CExtradataParser((BYTE*)hints.extradata,hints.extrasize,hints.profile,hints.level);
      std::vector<BYTE> xtravect = pextra->getextradata();
      /*xtravect.resize(hints.extrasize);
      memcpy(&xtravect.at(0),hints.extradata,hints.extrasize);*/
      CStdString xtravectstring;
      for (std::vector<BYTE>::iterator it= xtravect.begin(); it != xtravect.end(); it++)
      {
        BYTE bstring = *it;
        xtravectstring.AppendFormat("%hhx ",bstring);
      }
      bih->biSize = sizeof(*bih) + xtravect.size();
      CLog::Log(LOGDEBUG,"%s",xtravectstring.c_str());
    
  
      memcpy(bih + 1, &xtravect.at(0), xtravect.size());
    }
    catch (...)
    {
    }
  }
  bihout = (BITMAPINFOHEADER*)calloc(sizeof(*bihout), 1);
  //first argument is the path of the filter but is not required if the filter is registered
  CStdStringW codecGuidW;
  //g_charsetConverter.subtitleCharsetToW(g_advancedSettings.m_videoDefaultDirectshowCodec,codecGuidW);
  GUID codecguid = GUID_NULL;
  //CLSIDFromString((LPCOLESTR)codecGuidW.c_str(),&codecguid);
  codec = DSOpenVideoCodec("", this, CLSID_MPC_Video_Decoder , bih, mmioFOURCC('N', 'V', '1', '2'), frametime ,curfile.c_str(), 0/*is mpegts*/,bihout ,&err);
  if (!codec)
    codec = DSOpenVideoCodec("", this, CLSID_FFDShow_DXVA_Video_Decoder , bih, mmioFOURCC('N', 'V', '1', '2'), frametime ,curfile.c_str(), 0/*is mpegts*/,bihout ,&err);
  if (!codec)
    codec = DSOpenVideoCodec("", this, CLSID_FFDShow_Video_Decoder , bih, mmioFOURCC('Y', 'V', '1', '2'), frametime ,curfile.c_str(), 0/*is mpegts*/,bihout ,&err);
  if (!codec)
    codec = DSOpenVideoCodec("" , this, CLSID_MPC_Video_Decoder , bih, mmioFOURCC('Y', 'V', '1', '2'), frametime ,curfile.c_str(), 0/*is mpegts*/,bihout ,&err);
  
  
  //CLSID_FFDShow_Video_Decoder
  //CLSID_FFDShow_DXVA_Video_Decoder
  //CLSID_MPC_Video_Decoder
  //int pageres = DSShowPropertyPage(codec);
  if (!codec)
  {
    CLog::Log(LOGERROR,"DShowNative codec failed:%s",/*m_dllDsNative.*/DSStrError(err));
    return false;
  }
  CLog::Log(LOGNOTICE,"Loading directshow filter Completed");
  
  //memset(&m_pCurrentData,0, sizeof(DSVideoOutputData));
  m_pCurrentData = new DSVideoOutputData;
  if (1)//todo add isevr
  {
    int picsize = bihout->biBitCount*bihout->biWidth*(bihout->biHeight+2)/8;
    HRESULT hr = MFCreateSample(&m_pCurrentData->sample);
    m_pCurrentData->data = new BYTE[picsize];//(BYTE*)malloc(bihout->biBitCount*bihout->biWidth*(bihout->biHeight+2)/8);
    m_pCurrentData->format = DSVideoOutputData::FMT_EVR;
  }
  else
  {
    m_pCurrentData->data = (BYTE*)malloc(bihout->biBitCount*bihout->biWidth*(bihout->biHeight+2)/8);
    m_pCurrentData->format = DSVideoOutputData::FMT_DS_YV12;
  }
  

  return true;
}

void CDVDVideoCodecDirectshow::Dispose()
{
  if (codec)
  {
    CLog::Log(LOGNOTICE,"unloading dsnative");
    /*m_dllDsNative.*/DSCloseVideoCodec(codec);
    codec = NULL;
  }
  /*if (m_pCurrentData)
  {
    free(m_pCurrentData);
    m_pCurrentData = NULL;
  }*/
}

void CDVDVideoCodecDirectshow::SetDropState(bool bDrop)
{
}

int CDVDVideoCodecDirectshow::Decode(BYTE* pData, int iSize, double dts, double pts, int flags)
{
  int err;
  long imageSize;
  int keyframe = flags & 1;
  double newpts;
  if (m_requireResync)
  {
    double currenttime = g_application.GetTime()*10000;
    DSVideoResync(codec, currenttime);
    m_requireResync = false;
  }
  __int64 rt = 0;
  if (pts != DVD_NOPTS_VALUE)
  {
    rt = pts * 10;
  }
  else if (dts != DVD_NOPTS_VALUE)
  {
    rt = dts * 10;
  }

  if (!pData)
  {
    if (number_of_frame_ready == 1)
      return VC_PICTURE | VC_BUFFER;
    if (number_of_frame_ready > 2)
      return VC_PICTURE;
    else
      return VC_BUFFER;
  }
  
  err = DSVideoDecode(codec, pData, iSize, rt, &newpts, m_pCurrentData, &imageSize, keyframe);
  
  if (err == 14)//DSN_DATA_QUEUE_FULL
  {
    CLog::Log(LOGDEBUG, "%s: m_pInputThread->AddInput full.", __FUNCTION__);
    Sleep(10);
  }
  else if (err != 0)
  {
      CLog::Log(LOGERROR,"DShowNative codec failed:%s", DSStrError((dsnerror_t) err));
      return VC_ERROR;
  }
  m_pts = pts;
  m_dts = dts;  
  IPaintCallback* pAlloc = DSVideoGetEvrCallback(codec);
  int rtn = 0;
  if (pAlloc->GetReadySample() > 2)
    return VC_PICTURE;
    
  if (pAlloc->GetReadySample())
    rtn = VC_PICTURE;
  return rtn | VC_BUFFER;
  


  return VC_PICTURE | VC_BUFFER;
}

void CDVDVideoCodecDirectshow::FrameReady(int number)
{
  number_of_frame_ready = number;
}

void CDVDVideoCodecDirectshow::Reset()
{
  
  //happen on seek, changing file, flushing(...???)
  //The seek will only be updated after the codec started back so we need to wait before the resync
  m_requireResync = true;
}

bool CDVDVideoCodecDirectshow::GetPicture(DVDVideoPicture* pDvdVideoPicture)
{
  pDvdVideoPicture->pts = DVD_NOPTS_VALUE;
  
  pDvdVideoPicture->iWidth = pDvdVideoPicture->iDisplayWidth = bihout->biWidth;
  pDvdVideoPicture->iHeight = pDvdVideoPicture->iDisplayHeight = bihout->biHeight;
  if (m_dts<0)
  {
    pDvdVideoPicture->pts = m_pts;
    pDvdVideoPicture->dts = m_pts;
  }
  else
  {
    pDvdVideoPicture->pts = m_dts;
    pDvdVideoPicture->dts = m_dts;
  }
  IPaintCallback* pAlloc = NULL;
  pAlloc = DSVideoGetEvrCallback(codec);
  if (pAlloc)
  {
    pDvdVideoPicture->format = DVDVideoPicture::FMT_DSHOW;
    bool ret = pAlloc->GetPicture(pDvdVideoPicture);
    
    pDvdVideoPicture->pAlloc = pAlloc;
    //pDvdVideoPicture->pSurfaceIndex = current_surface_index;
    return ret;
  }
    return false;
    
  
  
  
  if (!m_pCurrentData)
    return false;
  
  int w = pDvdVideoPicture->iWidth / 2;
  int h = pDvdVideoPicture->iHeight / 2;
  int size = w * h;
  long planesize = bihout->biBitCount * bihout->biWidth * (bihout->biHeight + 2) / 8;
  pDvdVideoPicture->format = DVDVideoPicture::FMT_YUV420P;
  pDvdVideoPicture->data[0] = m_pCurrentData->data;
  pDvdVideoPicture->data[2] = pDvdVideoPicture->data[0] + (pDvdVideoPicture->iWidth * pDvdVideoPicture->iHeight);
  pDvdVideoPicture->data[1] = pDvdVideoPicture->data[2] + size;
  pDvdVideoPicture->data[3] = NULL;
  pDvdVideoPicture->iLineSize[0] = pDvdVideoPicture->iWidth;
  pDvdVideoPicture->iLineSize[1] = w;
  pDvdVideoPicture->iLineSize[2] = w;
  pDvdVideoPicture->iLineSize[3] = 0;
  pDvdVideoPicture->iFlags = DVP_FLAG_ALLOCATED;
  //pDvdVideoPicture->iDuration  = videoinfoheader might have that info
  return true;
}

bool CDVDVideoCodecDirectshow::GetUserData(DVDVideoUserData* pDvdVideoUserData)
{
  /*if (pDvdVideoUserData && m_pInfo && m_pInfo->user_data && m_pInfo->user_data_len > 0)
  {
    pDvdVideoUserData->data = (BYTE*)m_pInfo->user_data;
    pDvdVideoUserData->size = m_pInfo->user_data_len;
    return true;
  }*/
  return false;
}

