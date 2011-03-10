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

#include "dsnative.h"
#include "dsnpins.h"
#include "DSAudioRenderer.h"
class DSAudioCodec
{
public:
  DSAudioCodec(const char *cfname, const GUID guid, CMediaType *wvfmt, const char *sfname);
  ~DSAudioCodec();
  void ReleaseGraph();
  BOOL LoadLibrary();
  BOOL CreateFilter();
  BOOL CheckMediaTypes(IPin *pin);
  dsnerror_t SetOutputType();
  BOOL SetInputType();
  BOOL EnumPins();
  dsnerror_t SetupAllocator();
  dsnerror_t CreateGraph(bool buildgraph=false);
  HRESULT AddToRot(IUnknown *pUnkGraph, DWORD *pdwRegister);
  void RemoveFromRot(DWORD pdwRegister);
  BOOL StartGraph();
  CMediaType GetOutputMediaType();
  dsnerror_t Decode(const BYTE *src, int size, int *usedByte);
  dsnerror_t Resync(REFERENCE_TIME pts);
  BOOL ShowPropertyPage();
  long HaveCurrentSample(){ if (m_pNullRenderer)return m_pNullRenderer->HaveCurrentSample();else return 0;}
  long GetMediaSample(BYTE **dst, int *newMediaType)
  {
  if (m_pNullRenderer->HaveCurrentSample() > 0)
    return m_pNullRenderer->GetCurrentSample(dst, newMediaType);
  else
    return 0;
  }
private:
  HMODULE m_hDll;
  GUID m_guid;
  wchar_t *m_cfname, *m_sfname;
  unsigned int m_outfmt;
  int m_discontinuity;
  int m_newmediatype;
  int m_mpegts;
  int m_pFrameDurationDivision;
  HRESULT m_res;
  REFERENCE_TIME m_frametime;
  CMediaType *m_wvfmt;
  
  

  IGraphBuilder *m_pGraph;
  DWORD m_dwRegister;
  IMediaControl *m_pMC;
  
  
  //threaded output queue
  COutputQueue *m_pOutputQueue;
  //Our fake source filter
  CSenderFilter *m_pSFilter;
  CSenderPin *m_pOurInput;
  //Current bin codec
  IBaseFilter *m_pFilter;
  IPin *m_pInputPin;
  IPin *m_pOutputPin;
  IBaseFilter *m_pDirectSoundRenderer;
  IPin *m_pDirectSoundRendererInputPin;
  IReferenceClock *m_pClock;
  //CRendererInputPin *m_pNullRendererPin;
  CTee *m_pDupFilter;
  CNullRenderer *m_pNullRenderer;
  //CNullRendererInputPin *m_pNullRendererInputPin;


  IMemInputPin *m_pMemInputPin;
  IMemAllocator *m_pMemAllocator;
  AM_MEDIA_TYPE m_pOurType, m_pDestType;
};