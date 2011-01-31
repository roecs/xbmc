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
#include "DsAudioRenderer.h"
//
// CNullRenderer
//
CNullRendererInputPin::CNullRendererInputPin(HRESULT *phr, CNullRenderer *pFilter, CCritSec *pLock) 
  : CBaseInputPin(NAME("CNullRendererInputPin"), pFilter, pLock, phr, L"Render"),
    m_pRenderer(pFilter)
{
}

CNullRendererInputPin::~CNullRendererInputPin()
{
}

HRESULT CNullRendererInputPin::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
  return CBaseInputPin::NonDelegatingQueryInterface(riid, ppv);
}

CMediaType CNullRendererInputPin::GetOutputMediaType()
{
  CMediaType pType = m_mt;
  return pType;
}

HRESULT CNullRendererInputPin::Receive(IMediaSample *pSample)
{
  HRESULT hr = m_pRenderer->Receive(pSample);
  return hr;
}

CNullRenderer::CNullRenderer()
  : CBaseFilter(NAME("CNullRenderer"), NULL, &m_csFilter, GUID_NULL),
    m_pReferenceClock(NULL)
{
  m_pin = new CNullRendererInputPin(&m_hr, this, &m_csFilter);
  m_pDecodedData = new CDSPacket();
  m_pDecodedData->size = 0;
  m_pDecodedData->buffering_size = 0;
  m_pDecodedData->media_type.InitMediaType();
  m_bNewMediaType = false;
}

CNullRenderer::~CNullRenderer()
{
  delete m_pin;
  delete[] m_pDecodedData->sample;
  delete m_pDecodedData;
  if (m_pReferenceClock)
  {
    CBaseFilter::SetSyncSource(NULL);
    SAFE_RELEASE (m_pReferenceClock);
  }
}
// Checks if there is a sample waiting at the renderer

long CNullRenderer::HaveCurrentSample()
{
    CAutoLock cRendererLock(&m_RendererLock);

    return (m_pDecodedData->buffering_size);
}


// Returns the current sample. We AddRef the
// sample before returning so that should it come due for rendering the
// person who called this method will hold the remaining reference count
// that will stop the sample being added back onto the allocator free list

long CNullRenderer::GetCurrentSample(BYTE **dst, int *newMediaType)
{
    CAutoLock cRendererLock(&m_RendererLock);
    *dst = m_pDecodedData->sample;
    
    m_pDecodedData->buffering_size = 0;
    if (m_bNewMediaType)
    {
      *newMediaType = 1;
      m_bNewMediaType = false;
    }
    return m_pDecodedData->size;
}

HRESULT CNullRenderer::Receive(IMediaSample *pSample)
{
  CAutoLock cSampleLock(&m_RendererLock);
  CAutoLock cRendererLock(&m_csFilter);
  long lDataLength = pSample->GetActualDataLength();
  if (m_pDecodedData->size < lDataLength )
  {
    if (m_pDecodedData->size > 0)
      delete[] m_pDecodedData->sample;
    m_pDecodedData->sample = new BYTE[lDataLength];
    
  }
  
  BYTE* pData = NULL;

  pSample->GetPointer(&pData);
  
  memcpy(m_pDecodedData->sample, pData, lDataLength);

  m_pDecodedData->buffering_size = m_pDecodedData->size = lDataLength;
  AM_MEDIA_TYPE* mt= NULL;
  if (SUCCEEDED(pSample->GetMediaType(&mt)))
  {
    if (mt)
    {
      m_pDecodedData->media_type = *mt;
      m_bNewMediaType = true;
    }
  }

  return S_OK;
}
CMediaType CNullRenderer::GetOutputMediaType()
{
  if (!m_pDecodedData->media_type.cbFormat)
  {
    m_pDecodedData->media_type = m_pin->GetOutputMediaType();
  }
  return m_pDecodedData->media_type;

  
}
STDMETHODIMP CNullRenderer::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
  HRESULT hr = S_OK;
	if (riid == IID_IReferenceClock) 
  {
    
	  if (m_pReferenceClock) 
		  return m_pReferenceClock->NonDelegatingQueryInterface(riid, ppv);


	  m_pReferenceClock = new CBaseReferenceClock (NAME("CNullRenderer Clock"), NULL, &hr);
	  if (! m_pReferenceClock)
		  return E_OUTOFMEMORY;

	  m_pReferenceClock->AddRef();

	  hr = SetSyncSource(m_pReferenceClock);
	  if (FAILED(hr)) 
    {
		  SetSyncSource(NULL);
		  return hr;
	  }
		return m_pReferenceClock->NonDelegatingQueryInterface(riid, ppv);
  }
  return CBaseFilter::NonDelegatingQueryInterface(riid,ppv);
}