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



////////////////////////////////////////////////////////////////////////////////////////////
// TODO: refactor this so as not to need these ffmpeg routines.
// These are not exposed in ffmpeg's API so we dupe them here.
// AVC helper functions for muxers,
//  * Copyright (c) 2006 Baptiste Coudurier <baptiste.coudurier@smartjog.com>
// This is part of FFmpeg
//  * License as published by the Free Software Foundation; either
//  * version 2.1 of the License, or (at your option) any later version.
#define VDA_RB24(x)                          \
  ((((const uint8_t*)(x))[0] << 16) |        \
   (((const uint8_t*)(x))[1] <<  8) |        \
   ((const uint8_t*)(x))[2])

#define VDA_RB32(x)                          \
  ((((const uint8_t*)(x))[0] << 24) |        \
   (((const uint8_t*)(x))[1] << 16) |        \
   (((const uint8_t*)(x))[2] <<  8) |        \
   ((const uint8_t*)(x))[3])

#define VDA_WB32(p, d) { \
  ((uint8_t*)(p))[3] = (d); \
  ((uint8_t*)(p))[2] = (d) >> 8; \
  ((uint8_t*)(p))[1] = (d) >> 16; \
  ((uint8_t*)(p))[0] = (d) >> 24; }

static const uint8_t *avc_find_startcode_internal(const uint8_t *p, const uint8_t *end)
{
  const uint8_t *a = p + 4 - ((intptr_t)p & 3);

  for (end -= 3; p < a && p < end; p++)
  {
    if (p[0] == 0 && p[1] == 0 && p[2] == 1)
      return p;
  }

  for (end -= 3; p < end; p += 4)
  {
    uint32_t x = *(const uint32_t*)p;
    if ((x - 0x01010101) & (~x) & 0x80808080) // generic
    {
      if (p[1] == 0)
      {
        if (p[0] == 0 && p[2] == 1)
          return p;
        if (p[2] == 0 && p[3] == 1)
          return p+1;
      }
      if (p[3] == 0)
      {
        if (p[2] == 0 && p[4] == 1)
          return p+2;
        if (p[4] == 0 && p[5] == 1)
          return p+3;
      }
    }
  }

  for (end += 3; p < end; p++)
  {
    if (p[0] == 0 && p[1] == 0 && p[2] == 1)
      return p;
  }

  return end + 3;
}

const uint8_t *avc_find_startcode(const uint8_t *p, const uint8_t *end)
{
  const uint8_t *out= avc_find_startcode_internal(p, end);
  if (p<out && out<end && !out[-1])
    out--;
  return out;
}

const int avc_parse_nal_units(DllAvFormat *av_format_ctx,
  ByteIOContext *pb, const uint8_t *buf_in, int size)
{
  const uint8_t *p = buf_in;
  const uint8_t *end = p + size;
  const uint8_t *nal_start, *nal_end;

  size = 0;
  nal_start = avc_find_startcode(p, end);
  while (nal_start < end)
  {
    while (!*(nal_start++));
    nal_end = avc_find_startcode(nal_start, end);
    av_format_ctx->put_be32(pb, nal_end - nal_start);
    av_format_ctx->put_buffer(pb, nal_start, nal_end - nal_start);
    size += 4 + nal_end - nal_start;
    nal_start = nal_end;
  }
  return size;
}

const int avc_parse_nal_units_buf(DllAvUtil *av_util_ctx, DllAvFormat *av_format_ctx,
  const uint8_t *buf_in, uint8_t **buf, int *size)
{
  ByteIOContext *pb;
  int ret = av_format_ctx->url_open_dyn_buf(&pb);
  if (ret < 0)
    return ret;

  avc_parse_nal_units(av_format_ctx, pb, buf_in, *size);

  av_util_ctx->av_freep(buf);
  *size = av_format_ctx->url_close_dyn_buf(pb, buf);
  return 0;
}

const int isom_write_avcc(DllAvUtil *av_util_ctx, DllAvFormat *av_format_ctx,
  ByteIOContext *pb, const uint8_t *data, int len)
{
  // extradata from bytestream h264, convert to avcC atom data for bitstream
  if (len > 6)
  {
    /* check for h264 start code */
    if (VDA_RB32(data) == 0x00000001 || VDA_RB24(data) == 0x000001)
    {
      uint8_t *buf=NULL, *end, *start;
      uint32_t sps_size=0, pps_size=0;
      uint8_t *sps=0, *pps=0;

      int ret = avc_parse_nal_units_buf(av_util_ctx, av_format_ctx, data, &buf, &len);
      if (ret < 0)
        return ret;
      start = buf;
      end = buf + len;

      /* look for sps and pps */
      while (buf < end)
      {
        unsigned int size;
        uint8_t nal_type;
        size = VDA_RB32(buf);
        nal_type = buf[4] & 0x1f;
        if (nal_type == 7) /* SPS */
        {
          sps = buf + 4;
          sps_size = size;
        }
        else if (nal_type == 8) /* PPS */
        {
          pps = buf + 4;
          pps_size = size;
        }
        buf += size + 4;
      }
      assert(sps);
      //41 sps
      //9 pps
#if 1
      av_format_ctx->put_be16(pb, sps_size);
      av_format_ctx->put_buffer(pb, sps, sps_size);
      av_format_ctx->put_be16(pb, pps_size);
      av_format_ctx->put_buffer(pb, pps, pps_size);

      av_util_ctx->av_free(start);
#else
      av_format_ctx->put_byte(pb, 1); /* version */
      av_format_ctx->put_byte(pb, sps[1]); /* profile */
      av_format_ctx->put_byte(pb, sps[2]); /* profile compat */
      av_format_ctx->put_byte(pb, sps[3]); /* level */
      av_format_ctx->put_byte(pb, 0xff); /* 6 bits reserved (111111) + 2 bits nal size length - 1 (11) */
      av_format_ctx->put_byte(pb, 0xe1); /* 3 bits reserved (111) + 5 bits number of sps (00001) */

      av_format_ctx->put_be16(pb, sps_size);
      av_format_ctx->put_buffer(pb, sps, sps_size);
      if (pps)
      {
        av_format_ctx->put_byte(pb, 1); /* number of pps */
        av_format_ctx->put_be16(pb, pps_size);
        av_format_ctx->put_buffer(pb, pps, pps_size);
      }
      av_util_ctx->av_free(start);
      
#endif
    }
    else
    {
      av_format_ctx->put_buffer(pb, data, len);
    }
  }
  return 0;
}

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
  original_codec_tag = hints.codec_tag;
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
      unsigned int extrasize = hints.extrasize;
      uint8_t *extradata = (uint8_t*)hints.extradata;
      DllAvUtil* avutil; DllAvFormat* avformat;
      avutil = new DllAvUtil;
      avformat = new DllAvFormat;
      avformat->Load();
      avutil->Load();
      ByteIOContext *pb;
      if (avformat->url_open_dyn_buf(&pb) < 0)
      {
        ASSERT(0);
        return false;
      }
      
      // create a valid avcC atom data from ffmpeg's extradata
      isom_write_avcc(avutil, avformat, pb, extradata, extrasize);
      // unhook from ffmpeg's extradata
      extradata = NULL;
      // extract the avcC atom data into extradata then copy at the end of the video info header
      extrasize = avformat->url_close_dyn_buf(pb, &extradata);
      std::vector<BYTE> xtravect;
      xtravect.resize(extrasize);
      memcpy(&xtravect.at(0), extradata, extrasize);
      /*xtravect.resize(hints.extrasize);
      memcpy(&xtravect.at(0),hints.extradata,hints.extrasize);*/
      CStdString xtravectstring;
      for (std::vector<BYTE>::iterator it= xtravect.begin(); it != xtravect.end(); it++)
      {
        BYTE bstring = *it;
        xtravectstring.AppendFormat("%02X ",bstring);
      }
      CLog::Log(LOGDEBUG,"%s",xtravectstring.c_str());
      bih->biSize = sizeof(*bih) + hints.extrasize;
      memcpy(bih + 1, hints.extradata, hints.extrasize);
#if (DS_TEST_M2TS_CONTAINER)
      //Just in case its a format that require mpeg2videoinfo header
      int len = FIELD_OFFSET(MPEG2VIDEOINFO, dwSequenceHeader) + extrasize;
      m_pMPEG2VIDEOINFO = (MPEG2VIDEOINFO*)new BYTE[len];
      memset(m_pMPEG2VIDEOINFO, 0, len);
      m_pMPEG2VIDEOINFO->hdr.AvgTimePerFrame = frametime;
		  m_pMPEG2VIDEOINFO->hdr.dwPictAspectRatioX = hints.width;
		  m_pMPEG2VIDEOINFO->hdr.dwPictAspectRatioY = hints.height;
		  m_pMPEG2VIDEOINFO->hdr.bmiHeader.biSize = sizeof(m_pMPEG2VIDEOINFO->hdr.bmiHeader);
		  m_pMPEG2VIDEOINFO->hdr.bmiHeader.biWidth = hints.width;
		  m_pMPEG2VIDEOINFO->hdr.bmiHeader.biHeight = hints.height;
      //this is going to be set by dsnative
		  //m_pMPEG2VIDEOINFO->hdr.bmiHeader.biCompression = '1CVA';
      m_pMPEG2VIDEOINFO->dwFlags = 4;
      m_pMPEG2VIDEOINFO->dwProfile = hints.profile;
      m_pMPEG2VIDEOINFO->dwLevel = hints.level;
      m_pMPEG2VIDEOINFO->cbSequenceHeader = extrasize;
      memcpy(&m_pMPEG2VIDEOINFO->dwSequenceHeader[0],extradata,extrasize);
#endif
      
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
  //Only try dxva format if allowed in user settings
  if (g_guiSettings.GetBool("videoplayer.usedxva2"))
  {
    codec = DSOpenVideoCodec("", this, CLSID_FFDShow_DXVA_Video_Decoder, bih, mmioFOURCC('N', 'V', '1', '2'), frametime ,curfile.c_str(), bihout ,&err);
    if (!codec)
      codec = DSOpenVideoCodec("", this, CLSID_MPC_Video_Decoder , bih, mmioFOURCC('N', 'V', '1', '2'), frametime ,curfile.c_str(), bihout ,&err);
  }
  if (!codec)
    codec = DSOpenVideoCodec("", this, CLSID_FFDShow_Video_Decoder , bih, mmioFOURCC('Y', 'V', '1', '2'), frametime ,curfile.c_str(), bihout ,&err);
  if (!codec)
    codec = DSOpenVideoCodec("" , this, CLSID_MPC_Video_Decoder , bih, mmioFOURCC('Y', 'V', '1', '2'), frametime ,curfile.c_str(), bihout ,&err);
  
  
  //CLSID_FFDShow_Video_Decoder
  //CLSID_FFDShow_DXVA_Video_Decoder
  //CLSID_MPC_Video_Decoder
  
  if (!codec)
  {
    CLog::Log(LOGERROR,"DShowNative codec failed:%s",/*m_dllDsNative.*/DSStrError(err));
    return false;
  }
  //DSShowPropertyPage(codec);
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
  //Memory is freed by dsnative. But their might be some missing memory leak that need to be verified
  /*try
  {
    if (bih)
      free(bih);
    if (bihout)
      free(bihout);
  }
  catch (...)
  {
    CLog::Log(LOGERROR,"Failed freeing BITMAPINFOHEADER");
  }*/
  if (codec)
  {
    CLog::Log(LOGNOTICE,"unloading dsnative");
    DSCloseVideoCodec(codec);
    codec = NULL;
  }
  
}

void CDVDVideoCodecDirectshow::SetDropState(bool bDrop)
{
  if (bDrop)
    CLog::Log(LOGDEBUG,"%s we should drop next frame", __FUNCTION__);
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

MPEG2VIDEOINFO* CDVDVideoCodecDirectshow::GetMPEG2VIDEOINFO()
{
  #if (DS_TEST_M2TS_CONTAINER)
  return m_pMPEG2VIDEOINFO;
#else
  return NULL;
#endif
}