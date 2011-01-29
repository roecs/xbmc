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

#ifndef DSNPINS_H
#define DSNPINS_H
#include "d3d9.h"
#include "mfapi.h"
#include "mfidl.h"
#include "evr.h" //MR_VIDEO_ACCELERATOR_SERVICE definition
#include "Dxva2api.h"
class CSenderFilter;
class CSenderPin;

class CSenderPin : public CBaseOutputPin
{
public:
    CSenderPin::CSenderPin(HRESULT *phr, CSenderFilter *pFilter, CCritSec *pLock);
    HRESULT CheckMediaType(const CMediaType *) { return S_OK; };
    HRESULT DecideBufferSize(IMemAllocator *pAlloc, ALLOCATOR_PROPERTIES *ppropInputRequest) { return S_OK; }

    HRESULT STDMETHODCALLTYPE BeginFlush(void) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndFlush(void) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Notify(IBaseFilter * pSender, Quality q) { return S_OK; }
};

class CSenderFilter: public CBaseFilter, 
                     public IFileSourceFilter, 
                     public IFilterGraph
{
public:

    DECLARE_IUNKNOWN

    CSenderFilter::CSenderFilter();
    CSenderFilter::~CSenderFilter();
    int GetPinCount() { return 1; }
    CBasePin *GetPin(int n) { return m_pin; }

    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv);

    /* IFileSourceFilter */
    HRESULT STDMETHODCALLTYPE Load(LPCOLESTR pszFileName, const AM_MEDIA_TYPE *pmt);
    HRESULT STDMETHODCALLTYPE GetCurFile(LPOLESTR *ppszFileName, AM_MEDIA_TYPE *pmt);

    /* IFilterGraph */
    HRESULT STDMETHODCALLTYPE AddFilter(IBaseFilter *pFilter, LPCWSTR pName) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE RemoveFilter(IBaseFilter *pFilter) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EnumFilters(IEnumFilters **ppEnum) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE FindFilterByName(LPCWSTR pName, IBaseFilter **ppFilter) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE SetDefaultSyncSource(void) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE ConnectDirect(IPin *ppinOut, IPin *ppinIn, const AM_MEDIA_TYPE *pmt) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Reconnect(IPin *ppin) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE Disconnect(IPin *ppin) { return E_NOTIMPL; }

private:
    CSenderPin *m_pin;
    HRESULT m_hr;
    CCritSec m_csFilter;
    LPOLESTR m_pFileName;
};

class CRenderFilter;
class CRenderPin;

class CRenderPin : public CBaseInputPin
{
public:
    CRenderPin::CRenderPin(HRESULT *phr, CRenderFilter *pFilter, CCritSec *pLock);
    ~CRenderPin();

    DECLARE_IUNKNOWN

    // override this to publicise our interfaces
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv);

    HRESULT CheckMediaType(const CMediaType *) { return S_OK; };
    
    HRESULT STDMETHODCALLTYPE Receive(IMediaSample *pSample);
    REFERENCE_TIME GetPTS(void) { return m_reftime; }

    HRESULT STDMETHODCALLTYPE BeginFlush(void) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndFlush(void) { return E_NOTIMPL; }

    void SetPointer(BYTE *ptr) { m_gPtr = ptr; }
    long GetPointerSize() {return m_gPtrSize;}
    void SetFrameSize(long size) { m_fSize = size; }
private:
  REFERENCE_TIME m_reftime;
  BYTE *m_gPtr;
  long m_gPtrSize;
  long m_fSize;
};

class CRenderFilter: public CBaseFilter
{
public:
    CRenderFilter::CRenderFilter();
    CRenderFilter::~CRenderFilter();
    int GetPinCount() { return 1; }
    CBasePin *GetPin(int n) { return m_pin; }

private:
    CRenderPin *m_pin;
    HRESULT m_hr;
    CCritSec m_csFilter;
};

#endif