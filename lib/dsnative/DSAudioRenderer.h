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

class CNullRenderer;
class CNullRendererInputPin;

class CNullRendererInputPin : public CBaseInputPin
{

public:
    CNullRendererInputPin::CNullRendererInputPin(HRESULT *phr, CNullRenderer *pFilter, CCritSec *pLock);
    ~CNullRendererInputPin();

    DECLARE_IUNKNOWN

    // override this to publicise our interfaces
    STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv);

    HRESULT CheckMediaType(const CMediaType *) { return S_OK; };
    CMediaType GetOutputMediaType();
    HRESULT STDMETHODCALLTYPE Receive(IMediaSample *pSample);
    HRESULT STDMETHODCALLTYPE BeginFlush(void) { return E_NOTIMPL; }
    HRESULT STDMETHODCALLTYPE EndFlush(void) { return E_NOTIMPL; }

protected:
  CNullRenderer *m_pRenderer;

};

class CNullRenderer: public CBaseFilter
{
public:
  CNullRenderer::CNullRenderer();
  CNullRenderer::~CNullRenderer();
  int GetPinCount() { return 1; }
  CBasePin *GetPin(int n) { return m_pin; }
  CMediaType GetOutputMediaType();
  DECLARE_IUNKNOWN
	STDMETHODIMP NonDelegatingQueryInterface(REFIID riid, void **ppv);
  long HaveCurrentSample();
  long GetCurrentSample(BYTE **dst, int *newMediaType);
  HRESULT Receive(IMediaSample *pSample);
private:
    CNullRendererInputPin *m_pin;
    CBaseReferenceClock*	m_pReferenceClock;
    CMediaType m_pMediaType;                       // Media type of connection
    HRESULT m_hr;
    CCritSec m_csFilter;
    CCritSec m_RendererLock;            // Controls access to internals
    CDSPacket *m_pDecodedData;       // Decoded data
    bool m_bNewMediaType;
};