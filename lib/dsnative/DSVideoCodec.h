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

#include "dsnative.h"
#include "dsnpins.h"
#include "dsallocator.h"
class DSVideoCodec
{
public:
  DSVideoCodec(const char *cfname, IDSInfoCallback *pCallback, const GUID guid, BITMAPINFOHEADER *bih, unsigned int outfmt, REFERENCE_TIME frametime, const char *sfname);
  ~DSVideoCodec();
  void ReleaseGraph();
  BOOL LoadLibrary();
  BOOL CreateFilter();
  BOOL CreateEvr(HWND window);
  BOOL CheckMediaTypes(IPin *pin);
  dsnerror_t SetOutputType();
  BOOL isAVC(DWORD biCompression);
  BOOL SetInputType();
  BOOL EnumPins();
  dsnerror_t SetupAllocator();
  dsnerror_t ResizeAllocatorProperties(int size);
  dsnerror_t CreateGraph(bool buildgraph=false);
  HRESULT AddToRot(IUnknown *pUnkGraph, DWORD *pdwRegister);
  void RemoveFromRot(DWORD pdwRegister);
  BOOL StartGraph();
  AM_MEDIA_TYPE GetOutputMediaType();
  dsnerror_t Decode(const BYTE *src, int size, double pts, double *newpts, DSVideoOutputData *pImage, long *pImageSize, int keyframe);
  dsnerror_t DSVideoGetMediaBuffer(DsAllocator* pAlloc);
  IPaintCallback* GetEvrCallback();
  dsnerror_t Resync(REFERENCE_TIME pts);
  BOOL ShowPropertyPage();
  BOOL SetOutputFormat(WORD *biBitCount, WORD *biPlanes);
private:
  HMODULE m_hDll;
  GUID m_guid;
  wchar_t *m_cfname, *m_sfname;
  unsigned int m_outfmt;
  int m_discontinuity;
  HRESULT m_res;
  __int64        m_currentframeindex;
  REFERENCE_TIME m_frametime;
  BITMAPINFOHEADER *m_bih;
  IBaseFilter     *m_pFilter;

  //evr stuff
  IBaseFilter     *m_pEvrFilter;
  DsAllocator     *m_pEvr;
  IPin            *m_pEvrInputPin;
  IDSInfoCallback *m_pCallback;

  IGraphBuilder *m_pGraph;
  DWORD m_dwRegister;
  IMediaControl *m_pMC;

  CSenderFilter *m_pSFilter;
  CRenderFilter *m_pRFilter;
  CRenderPin *m_pOurOutput;
  CSenderPin *m_pOurInput;

  IPin *m_pInputPin;
  IPin *m_pOutputPin;

  IMemInputPin *m_pMemInputPin;
  IMemAllocator *m_pMemAllocator;
  AM_MEDIA_TYPE m_pOurType, m_pDestType;
  BYTE *m_vinfo;
  VIDEOINFOHEADER m_vi;
  VIDEOINFOHEADER2 m_vi2;
};