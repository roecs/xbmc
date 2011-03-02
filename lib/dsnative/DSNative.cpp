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

#include "stdafx.h"
#include "dsnative.h"
#include "DSAudiocodec.h"
#include "DSVideoCodec.h"


extern "C" DSVideoCodec * WINAPI DSOpenVideoCodec(const char *dll, IDSInfoCallback *pCallback, const GUID guid, BITMAPINFOHEADER* bih,
                                                  unsigned int outfmt, REFERENCE_TIME frametime, const char *filename, BITMAPINFOHEADER *pDestType,dsnerror_t *err)
{
    DSVideoCodec *vcodec = new DSVideoCodec(dll, pCallback, guid, bih, outfmt, frametime, filename);
    dsnerror_t res = DSN_OK;

    if (!vcodec->LoadLibrary())
        res = DSN_LOADLIBRARY;
    else if (!vcodec->CreateFilter())
        res = DNS_FAIL_FILTER;
    else if (!vcodec->CreateEvr(pCallback->GetWindow()))
        res = DNS_FAIL_FILTER;
    else if ((res = vcodec->CreateGraph(true)) == DSN_OK)
    {
        vcodec->StartGraph();
        if (err) 
          *err = DSN_OK;
        AM_MEDIA_TYPE pType = vcodec->GetOutputMediaType();
        if (pType.formattype == FORMAT_VideoInfo)
        {
          VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *) pType.pbFormat;
          *pDestType = vih->bmiHeader;
        }
        else if (pType.formattype == FORMAT_VideoInfo2)
        {
          VIDEOINFOHEADER2 *vih2 = (VIDEOINFOHEADER2 *) pType.pbFormat;
          *pDestType = vih2->bmiHeader;
        }
        
        return vcodec;
    }
    delete vcodec;
    if (*err) 
      *err = res;
    return NULL;
}

extern "C" void WINAPI DSCloseVideoCodec(DSVideoCodec *vcodec)
{
    delete vcodec;
}

extern "C" dsnerror_t WINAPI DSVideoDecode(DSVideoCodec *vcodec, BYTE *src, int size, double pts, double *newpts, DSVideoOutputData *pImage, long *pImageSize, int keyframe)
{
    return vcodec->Decode(src, size, pts, newpts, pImage, pImageSize, keyframe);
}
extern "C" IPaintCallback* WINAPI DSVideoGetEvrCallback(DSVideoCodec *vcodec)
{
  return vcodec->GetEvrCallback();
}
extern "C" dsnerror_t WINAPI DSVideoGetMediaBuffer(DSVideoCodec *vcodec, DsAllocator* ppAlloc)
{
  dsnerror_t err;
  DsAllocator* pAlloc = NULL;

  
  err = vcodec->DSVideoGetMediaBuffer(pAlloc);
  //ppAlloc = pAlloc->Acquire();

  return err;
}

extern "C" dsnerror_t WINAPI DSVideoResync(DSVideoCodec *vcodec, double pts)
{
    return vcodec->Resync(PTS2RT(pts));
}

extern "C" DSAudioCodec * WINAPI DSOpenAudioCodec(const char *dll, const GUID guid, CMediaType* wvfmt,
                                                  const char *filename, CMediaType* pDestType,dsnerror_t *err)
{
    DSAudioCodec *acodec = new DSAudioCodec(dll, guid, wvfmt, filename);
    dsnerror_t res = DSN_OK;

    if (!acodec->LoadLibrary())
        res = DSN_LOADLIBRARY;
    else if (!acodec->CreateFilter())
        res = DNS_FAIL_FILTER;
    else if ((res = acodec->CreateGraph(true)) == DSN_OK)
    {
        acodec->StartGraph();
        if (err) 
          *err = DSN_OK;
        //AM_MEDIA_TYPE pType = acodec->GetOutputMediaType();
        //CopyMediaType(pDestType,&pType);
        *pDestType = acodec->GetOutputMediaType();
        
        return acodec;
    }
    delete acodec;
    if (*err) 
      *err = res;
    return NULL;
}
extern "C" dsnerror_t WINAPI DSAudioGetMediaType(DSAudioCodec *acodec, CMediaType* pType)
{
  if (acodec)
  {
    *pType = acodec->GetOutputMediaType();
    return dsnerror_t::DSN_OK;
  }
  else
  {
    return dsnerror_t::DNS_FAIL_FILTER;
  }

}
extern "C" void WINAPI DSCloseAudioCodec(DSAudioCodec *acodec)
{
    delete acodec;
}

extern "C" dsnerror_t WINAPI DSAudioDecode(DSAudioCodec *acodec, const BYTE *src, int size, int *usedByte)
{
  return acodec->Decode(src, size, usedByte);
}

extern "C" long WINAPI DSAudioGetSample(DSAudioCodec *acodec, BYTE **dst, int *newMediaType)
{
  return acodec->GetMediaSample(dst, newMediaType);
}

extern "C" long WINAPI DSAudioSampleSize(DSAudioCodec *acodec)
{
  return acodec->HaveCurrentSample();
}

extern "C" dsnerror_t WINAPI DSAudioResync(DSAudioCodec *acodec, double pts)
{
    return acodec->Resync(PTS2RT(pts));
}

extern "C" BOOL WINAPI DSShowPropertyPage(DSVideoCodec *codec)
{
    return codec->ShowPropertyPage();
}

extern "C" unsigned int WINAPI DSGetApiVersion(void)
{
    return DSN_API_VERSION;
}


BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    switch (reason)
    {
        case DLL_PROCESS_ATTACH:
        {
            DisableThreadLibraryCalls(hModule);
#ifdef _DEBUG
        	DbgInitialise(hModule);
	        DbgSetModuleLevel(LOG_TRACE,5);
	        DbgSetModuleLevel(LOG_MEMORY,5);
	        DbgSetModuleLevel(LOG_ERROR,5);
	        DbgSetModuleLevel(LOG_TIMING,5);
	        DbgSetModuleLevel(LOG_LOCKING,5);
#endif
			CoInitialize(NULL);
			break;
        }
        case DLL_THREAD_ATTACH:
        case DLL_THREAD_DETACH:
            return TRUE;
        case DLL_PROCESS_DETACH:
			CoUninitialize();
            break;
    }
    return TRUE;
}
