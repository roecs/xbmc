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

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#endif

#include "DynamicDll.h"
#include "utils/log.h"
#include "Dshow.h"//IMediaSample is easier to handle

#include "lib/dsnative/dsnative.h"
#ifdef DEBUG
#pragma comment (lib, "libs/dshownative/Debug/dshownative.lib")
#else
#pragma comment (lib, "libs/dshownative/Release/dshownative.lib")
#endif


class DllD3X9Interface
{
public:
  virtual ~DllD3X9Interface() {}
  virtual HRESULT D3DXCompileShader( LPCSTR pSrcData, UINT srcDataLen, const D3DXMACRO *pDefines, LPD3DXINCLUDE pInclude, LPCSTR pFunctionName, LPCSTR pProfile, DWORD Flags, LPD3DXBUFFER *ppShader, LPD3DXBUFFER *ppErrorMsgs, LPD3DXCONSTANTTABLE *ppConstantTable)=0;
  virtual HRESULT D3DXDisassembleShader(const DWORD *pShader, BOOL EnableColorCode, LPCSTR pComments, LPD3DXBUFFER *ppDisassembly)=0;
};

class DllD3X9 : public DllDynamic, DllD3X9Interface
{
public:
  DllD3X9() : DllDynamic(){}
  DEFINE_METHOD10(HRESULT, D3DXCompileShader, (LPCSTR p1, UINT p2, const D3DXMACRO *p3, LPD3DXINCLUDE p4, LPCSTR p5, LPCSTR p6, DWORD p7, LPD3DXBUFFER *p8, LPD3DXBUFFER *p9, LPD3DXCONSTANTTABLE *p10))
  DEFINE_METHOD4(HRESULT, D3DXDisassembleShader, (const DWORD *p1, BOOL p2, LPCSTR p3, LPD3DXBUFFER *p4))
  BEGIN_METHOD_RESOLVE()
    RESOLVE_METHOD(D3DXCompileShader)
    RESOLVE_METHOD(D3DXDisassembleShader)
  END_METHOD_RESOLVE()
};

class DllDSNativeInterface
{
public:
  virtual ~DllDSNativeInterface() {}
  virtual DSVideoCodec * DSOpenVideoCodec(const char *dll, const GUID guid, BITMAPINFOHEADER* bih, unsigned int outfmt, float fps, const char *filename, int mpegts, dsnerror_t *err)=0;
  virtual void DSCloseVideoCodec(DSVideoCodec *vcodec)=0;
  virtual dsnerror_t DSVideoDecode(DSVideoCodec *vcodec, const BYTE *src, int size, double pts, double *newpts, BYTE *pImage, long *pImageSize,int keyframe)=0;
  virtual dsnerror_t DSVideoResync(DSVideoCodec *vcodec, double pts)=0;
  virtual BOOL DSShowPropertyPage(DSVideoCodec *codec)=0;
  virtual unsigned int DSGetApiVersion(void)=0;
  virtual const char * DSStrError(dsnerror_t error)=0;
};



class DllDSNative : public DllDynamic, DllDSNativeInterface
{
  DECLARE_DLL_WRAPPER(DllDSNative, DLL_PATH_DSHOWNATIVE)
  DEFINE_METHOD8(DSVideoCodec *, DSOpenVideoCodec, (const char * p1, const GUID p2, BITMAPINFOHEADER* p3, unsigned int p4, float p5, const char * p6, int p7, dsnerror_t * p8))
  DEFINE_METHOD1(void, DSCloseVideoCodec,(DSVideoCodec * p1))
  DEFINE_METHOD8(dsnerror_t, DSVideoDecode,(DSVideoCodec * p1, const BYTE * p2, int p3, double p4, double *p5, BYTE *p6, long *p7, int p8))
  DEFINE_METHOD2(dsnerror_t, DSVideoResync,(DSVideoCodec * p1, double p2))
  DEFINE_METHOD1(BOOL, DSShowPropertyPage,(DSVideoCodec * p1))
  DEFINE_METHOD0(unsigned int, DSGetApiVersion)
  DEFINE_METHOD1(const char *, DSStrError,(dsnerror_t p1))
  BEGIN_METHOD_RESOLVE()
    RESOLVE_METHOD(DSOpenVideoCodec)
    RESOLVE_METHOD(DSCloseVideoCodec)
    RESOLVE_METHOD(DSVideoDecode)
    RESOLVE_METHOD(DSVideoResync)
    RESOLVE_METHOD(DSShowPropertyPage)
    RESOLVE_METHOD(DSGetApiVersion)
    RESOLVE_METHOD(DSStrError)
  END_METHOD_RESOLVE()
};
