/*
 *      Copyright (C) 2010 Team XBMC
 *      http://www.xbmc.org
 *      Modified version of DShow Native wrapper from Gianluigi Tiesi <sherpya@netfarm.it>
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

#ifndef DSNATIVE_H
#define DSNATIVE_H
#define DS_TEST_M2TS_CONTAINER 0
#define _ATL_EX_CONVERSION_MACROS_ONLY
#define DSN_DLL_VERSION 0.1.1.7
#define DSN_API_VERSION 7

#define DSN_STRINGIFY(s) DSN_TOSTR(s)
#define DSN_TOSTR(s) #s

//#define PTS2RT(x) ((REFERENCE_TIME) llrintf(x * 1E7))
#define PTS2RT(x) ((REFERENCE_TIME) ((x) * 1E7))
#define RT2PTS(x) (((double) x) / 1E7)
#ifndef SAFE_REMOVE_FILTER
#define SAFE_REMOVE_FILTER(graph,filter) if (filter)graph->RemoveFilter(filter);
#endif
#include "streams.h"
#include <dvdmedia.h>
#include "dsnerror.h"
#include "d3d9.h"
#include "IPaintCallback.h"
#include "mfapi.h"

class CDSPacket
{
public:
  CDSPacket(){};
  virtual ~CDSPacket(){};

  BYTE *sample;
  long size;/*Real size of byte*/
  long buffering_size;/*Used for returning buffer*/
  CMediaType media_type;
};

class IDSInfoCallback
{
public:
  virtual HWND GetWindow() = 0;
  virtual IDirect3DDevice9* GetD3DDev() = 0;
  virtual __int64 GetTime() = 0;
  virtual __int64 GetTotalTime() = 0;
  virtual MPEG2VIDEOINFO* GetMPEG2VIDEOINFO() = 0;
  virtual void LogCallback(int loglevel, const char *format, ...) = 0;
  virtual void FrameReady(int number) = 0;
  virtual uint32_t GetOriginalCodecTag() = 0;
  virtual ~IDSInfoCallback() { }
};
struct DSVideoOutputData
{
  union
  {
    struct {
      BYTE* data;
    };
    struct {
      IMFSample* sample;
    };
  };

  enum EOutputFormat {
    FMT_EVR = 0,
    FMT_DS_YV12,
  } format;
};
class DSAudioCodec;
class DSVideoCodec;
class DsAllocator;
extern "C" unsigned int WINAPI DSGetApiVersion(void);
extern "C" const char * WINAPI DSStrError(dsnerror_t error);

/*Video*/
extern "C" DSVideoCodec * WINAPI DSOpenVideoCodec(const char *dll, IDSInfoCallback *pCallback, const GUID guid, BITMAPINFOHEADER* bih, unsigned int outfmt, REFERENCE_TIME frametime, const char *filename, BITMAPINFOHEADER *pDestType,dsnerror_t *err);
extern "C" void WINAPI DSCloseVideoCodec(DSVideoCodec *vcodec);
extern "C" dsnerror_t WINAPI DSVideoDecode(DSVideoCodec *vcodec, BYTE *src, int size, double pts, double *newpts, DSVideoOutputData *pImage, long *pImageSize,int keyframe);
extern "C" dsnerror_t WINAPI DSVideoGetMediaBuffer(DSVideoCodec *vcodec, DsAllocator* ppAlloc);
extern "C" IPaintCallback* WINAPI DSVideoGetEvrCallback(DSVideoCodec *vcodec);
extern "C" dsnerror_t WINAPI DSVideoResync(DSVideoCodec *vcodec, double pts);
/*Audio*/
extern "C" DSAudioCodec * WINAPI DSOpenAudioCodec(const char *dll, const GUID guid, CMediaType* wvfmt, const char *filename, CMediaType* pDestType, dsnerror_t *err);
extern "C" dsnerror_t WINAPI DSAudioGetMediaType(DSAudioCodec *acodec, CMediaType* pType);
extern "C" void WINAPI DSCloseAudioCodec(DSAudioCodec *vcodec);
extern "C" dsnerror_t WINAPI DSAudioDecode(DSAudioCodec *acodec, const BYTE *src, int size, int *usedByte);
extern "C" long WINAPI DSAudioGetSample(DSAudioCodec *acodec,BYTE **dst, int *newMediaType);
extern "C" long WINAPI DSAudioSampleSize(DSAudioCodec *acodec);
extern "C" dsnerror_t WINAPI DSAudioResync(DSAudioCodec *vcodec, double pts);
extern "C" BOOL WINAPI DSShowPropertyPage(DSVideoCodec *codec);

#endif