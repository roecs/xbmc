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
  long refcount = pSample->Release();

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


//
// Constructor
//
CTee::CTee(TCHAR *pName, LPUNKNOWN pUnk, HRESULT *phr) :
  m_OutputPinsList(NAME("Tee Output Pins list")),
  m_lCanSeek(TRUE),
  m_pAllocator(NULL),
  m_NumOutputPins(0),
  m_NextOutputPinNumber(0),
  m_Input(NAME("Input Pin"), this, phr, L"Input"),
  CBaseFilter(NAME("Tee filter"), pUnk, this, CLSID_DSoundRender)
{
  ASSERT(phr);

  // Create a single output pin at this time
  InitOutputPinsList();

  CTeeOutputPin *pOutputPin = CreateNextOutputPin(this);

  if (pOutputPin != NULL )
  {
    m_NumOutputPins++;
    m_OutputPinsList.AddTail(pOutputPin);
  }
}


//
// Destructor
//
CTee::~CTee()
{
  InitOutputPinsList();
}


//
// GetPinCount
//
int CTee::GetPinCount()
{
  return (1 + m_NumOutputPins);
}


//
// GetPin
//
CBasePin *CTee::GetPin(int n)
{
  if (n < 0)
    return NULL ;

  // Pin zero is the one and only input pin
  if (n == 0)
    return &m_Input;

  // return the output pin at position(n - 1) (zero based)
  return GetPinNFromList(n - 1);
}


//
// InitOutputPinsList
//
void CTee::InitOutputPinsList()
{
  POSITION pos = m_OutputPinsList.GetHeadPosition();

  while(pos)
  {
    CTeeOutputPin *pOutputPin = m_OutputPinsList.GetNext(pos);
    ASSERT(pOutputPin->m_pOutputQueue == NULL);
    pOutputPin->Release();
  }

  m_NumOutputPins = 0;
  m_OutputPinsList.RemoveAll();

} // InitOutputPinsList


//
// CreateNextOutputPin
//
CTeeOutputPin *CTee::CreateNextOutputPin(CTee *pTee)
{
  WCHAR szbuf[20];       // Temporary scratch buffer
  m_NextOutputPinNumber++;   // Next number to use for pin
  HRESULT hr = NOERROR;

  (void)StringCchPrintfW(szbuf, NUMELMS(szbuf), L"Output%d\0", m_NextOutputPinNumber);

  CTeeOutputPin *pPin = new CTeeOutputPin(NAME("Tee Output"), pTee,
                      &hr, szbuf,
                      m_NextOutputPinNumber);

  if (FAILED(hr) || pPin == NULL) 
  {
    delete pPin;
    return NULL;
  }

  pPin->AddRef();
  return pPin;

} // CreateNextOutputPin


//
// DeleteOutputPin
//
void CTee::DeleteOutputPin(CTeeOutputPin *pPin)
{
  ASSERT(pPin);
  if (!pPin)
    return;
    
  POSITION pos = m_OutputPinsList.GetHeadPosition();

  while(pos) 
  {
    POSITION posold = pos;     // Remember this position
    CTeeOutputPin *pOutputPin = m_OutputPinsList.GetNext(pos);

    if (pOutputPin == pPin) 
    {
      // If this pin holds the seek interface release it
      if (pPin->m_bHoldsSeek) 
      {
        InterlockedExchange(&m_lCanSeek, FALSE);
        pPin->m_bHoldsSeek = FALSE;
        pPin->m_pPosition->Release();
      }

      m_OutputPinsList.Remove(posold);
      ASSERT(pOutputPin->m_pOutputQueue == NULL);
      delete pPin;

      m_NumOutputPins--;
      IncrementPinVersion();
      break;
    }
  }

} // DeleteOutputPin


//
// GetNumFreePins
//
int CTee::GetNumFreePins()
{
  int n = 0;
  POSITION pos = m_OutputPinsList.GetHeadPosition();

  while(pos) 
  {
    CTeeOutputPin *pOutputPin = m_OutputPinsList.GetNext(pos);

    if (pOutputPin && pOutputPin->m_Connected == NULL)
      n++;
  }

  return n;

} // GetNumFreePins


//
// GetPinNFromList
//
CTeeOutputPin *CTee::GetPinNFromList(int n)
{
  // Validate the position being asked for
  if (n >= m_NumOutputPins)
    return NULL;

  // Get the head of the list
  POSITION pos = m_OutputPinsList.GetHeadPosition();

  n++;     // Make the number 1 based

  CTeeOutputPin *pOutputPin=0;
  while(n) 
  {
    pOutputPin = m_OutputPinsList.GetNext(pos);
    n--;
  }

  return pOutputPin;

} // GetPinNFromList


//
// Stop
//
// Overriden to handle no input connections
//
STDMETHODIMP CTee::Stop()
{
  CBaseFilter::Stop();
  m_State = State_Stopped;

  return NOERROR;
}


//
// Pause
//
// Overriden to handle no input connections
//
STDMETHODIMP CTee::Pause()
{
  CAutoLock cObjectLock(m_pLock);
  HRESULT hr = CBaseFilter::Pause();

  if (m_Input.IsConnected() == FALSE) 
  {
    m_Input.EndOfStream();
  }

  return hr;
}


//
// Run
//
// Overriden to handle no input connections
//
STDMETHODIMP CTee::Run(REFERENCE_TIME tStart)
{
  CAutoLock cObjectLock(m_pLock);
  HRESULT hr = CBaseFilter::Run(tStart);

  if (m_Input.IsConnected() == FALSE) 
  {
    m_Input.EndOfStream();
  }

  return hr;
}

//
// CTeeInputPin constructor
//
CTeeInputPin::CTeeInputPin(TCHAR   *pName,
               CTee  *pTee,
               HRESULT *phr,
               LPCWSTR pPinName) :
  CBaseInputPin(pName, pTee, pTee, phr, pPinName),
  m_pTee(pTee),
  m_bInsideCheckMediaType(FALSE)
{
  ASSERT(pTee);
}


#ifdef DEBUG
//
// CTeeInputPin destructor
//
CTeeInputPin::~CTeeInputPin()
{
  DbgLog((LOG_TRACE,2,TEXT("CTeeInputPin destructor")));
  ASSERT(m_pTee->m_pAllocator == NULL);
}
#endif


#ifdef DEBUG
//
// DisplayMediaType -- (DEBUG ONLY)
//
void DisplayMediaType(TCHAR *pDescription, const CMediaType *pmt)
{
  ASSERT(pmt);
  if (!pmt)
    return;
    
  // Dump the GUID types and a short description

  DbgLog((LOG_TRACE,2,TEXT("")));
  DbgLog((LOG_TRACE,2,TEXT("%s"),pDescription));
  DbgLog((LOG_TRACE,2,TEXT("")));
  DbgLog((LOG_TRACE,2,TEXT("Media Type Description")));
  DbgLog((LOG_TRACE,2,TEXT("Major type %s"),GuidNames[*pmt->Type()]));
  DbgLog((LOG_TRACE,2,TEXT("Subtype %s"),GuidNames[*pmt->Subtype()]));
  DbgLog((LOG_TRACE,2,TEXT("Subtype description %s"),GetSubtypeName(pmt->Subtype())));
  DbgLog((LOG_TRACE,2,TEXT("Format size %d"),pmt->cbFormat));

  // Dump the generic media types */

  DbgLog((LOG_TRACE,2,TEXT("Fixed size sample %d"),pmt->IsFixedSize()));
  DbgLog((LOG_TRACE,2,TEXT("Temporal compression %d"),pmt->IsTemporalCompressed()));
  DbgLog((LOG_TRACE,2,TEXT("Sample size %d"),pmt->GetSampleSize()));


} // DisplayMediaType
#endif


//
// CheckMediaType
//
HRESULT CTeeInputPin::CheckMediaType(const CMediaType *pmt)
{
  CAutoLock lock_it(m_pLock);

  // If we are already inside checkmedia type for this pin, return NOERROR
  // It is possble to hookup two of the tee filters and some other filter
  // like the video effects sample to get into this situation. If we don't
  // detect this situation, we will carry on looping till we blow the stack

  if (m_bInsideCheckMediaType == TRUE)
    return NOERROR;

  m_bInsideCheckMediaType = TRUE;
  HRESULT hr = NOERROR;

#ifdef DEBUG
  // Display the type of the media for debugging perposes
  DisplayMediaType(TEXT("Input Pin Checking"), pmt);
#endif

  // The media types that we can support are entirely dependent on the
  // downstream connections. If we have downstream connections, we should
  // check with them - walk through the list calling each output pin

  int n = m_pTee->m_NumOutputPins;
  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();

  while(n) 
  {
    CTeeOutputPin *pOutputPin = m_pTee->m_OutputPinsList.GetNext(pos);

    if (pOutputPin != NULL) 
    {
      if (pOutputPin->m_Connected != NULL) 
      {
        // The pin is connected, check its peer
        hr = pOutputPin->m_Connected->QueryAccept(pmt);
        if (hr != NOERROR) 
        {
          m_bInsideCheckMediaType = FALSE;
          return VFW_E_TYPE_NOT_ACCEPTED;
        }
      }
    } 
    else 
    {
      // We should have as many pins as the count says we have
      ASSERT(FALSE);
    }

    n--;
  }

  // Either all the downstream pins have accepted or there are none.
  m_bInsideCheckMediaType = FALSE;
  return NOERROR;

} // CheckMediaType


//
// SetMediaType
//
HRESULT CTeeInputPin::SetMediaType(const CMediaType *pmt)
{
  CAutoLock lock_it(m_pLock);
  HRESULT hr = NOERROR;

  // Make sure that the base class likes it
  hr = CBaseInputPin::SetMediaType(pmt);
  if (FAILED(hr))
    return hr;

  ASSERT(m_Connected != NULL);
  return NOERROR;

} // SetMediaType


//
// BreakConnect
//
HRESULT CTeeInputPin::BreakConnect()
{
  // Release any allocator that we are holding
  if (m_pTee->m_pAllocator)
  {
    m_pTee->m_pAllocator->Release();
    m_pTee->m_pAllocator = NULL;
  }

  return NOERROR;

} // BreakConnect


//
// NotifyAllocator
//
STDMETHODIMP
CTeeInputPin::NotifyAllocator(IMemAllocator *pAllocator, BOOL bReadOnly)
{
  CheckPointer(pAllocator,E_FAIL);
  CAutoLock lock_it(m_pLock);

  // Free the old allocator if any
  if (m_pTee->m_pAllocator)
    m_pTee->m_pAllocator->Release();

  // Store away the new allocator
  pAllocator->AddRef();
  m_pTee->m_pAllocator = pAllocator;

  // Notify the base class about the allocator
  return CBaseInputPin::NotifyAllocator(pAllocator,bReadOnly);

} // NotifyAllocator


//
// EndOfStream
//
HRESULT CTeeInputPin::EndOfStream()
{
  CAutoLock lock_it(m_pLock);
  ASSERT(m_pTee->m_NumOutputPins);
  HRESULT hr = NOERROR;

  // Walk through the output pins list, sending the message downstream

  int n = m_pTee->m_NumOutputPins;
  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();

  while(n) 
  {
    CTeeOutputPin *pOutputPin = m_pTee->m_OutputPinsList.GetNext(pos);
    if (pOutputPin != NULL) 
    {
      hr = pOutputPin->DeliverEndOfStream();
      if (FAILED(hr))
        return hr;
    } 
    else 
    {
      // We should have as many pins as the count says we have
      ASSERT(FALSE);
    }
    n--;
  }
  return(NOERROR);

} // EndOfStream


//
// BeginFlush
//
HRESULT CTeeInputPin::BeginFlush()
{
  CAutoLock lock_it(m_pLock);
  ASSERT(m_pTee->m_NumOutputPins);
  HRESULT hr = NOERROR;

  // Walk through the output pins list, sending the message downstream

  int n = m_pTee->m_NumOutputPins;
  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();

  while(n) 
  {
    CTeeOutputPin *pOutputPin = m_pTee->m_OutputPinsList.GetNext(pos);
    if (pOutputPin != NULL) 
    {
      hr = pOutputPin->DeliverBeginFlush();
      if (FAILED(hr))
        return hr;
    } 
    else 
    {
      // We should have as many pins as the count says we have
      ASSERT(FALSE);
    }
    n--;
  }

  return CBaseInputPin::BeginFlush();

} // BeginFlush


//
// EndFlush
//
HRESULT CTeeInputPin::EndFlush()
{
  CAutoLock lock_it(m_pLock);
  ASSERT(m_pTee->m_NumOutputPins);
  HRESULT hr = NOERROR;

  // Walk through the output pins list, sending the message downstream

  int n = m_pTee->m_NumOutputPins;
  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();

  while(n)
  {
    CTeeOutputPin *pOutputPin = m_pTee->m_OutputPinsList.GetNext(pos);
    if(pOutputPin != NULL)
    {
      hr = pOutputPin->DeliverEndFlush();
      if(FAILED(hr))
        return hr;
    }
    else
    {
      // We should have as many pins as the count says we have
      ASSERT(FALSE);
    }
    n--;
  }

  return CBaseInputPin::EndFlush();

} // EndFlush


//
// NewSegment
//
          
HRESULT CTeeInputPin::NewSegment(REFERENCE_TIME tStart,
                 REFERENCE_TIME tStop,
                 double dRate)
{
  CAutoLock lock_it(m_pLock);
  ASSERT(m_pTee->m_NumOutputPins);
  HRESULT hr = NOERROR;

  // Walk through the output pins list, sending the message downstream

  int n = m_pTee->m_NumOutputPins;
  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();

  while(n)
  {
    CTeeOutputPin *pOutputPin = m_pTee->m_OutputPinsList.GetNext(pos);
    if(pOutputPin != NULL)
    {
      hr = pOutputPin->DeliverNewSegment(tStart, tStop, dRate);
      if(FAILED(hr))
        return hr;
    }
    else
    {
      // We should have as many pins as the count says we have
      ASSERT(FALSE);
    }
    n--;
  }

  return CBaseInputPin::NewSegment(tStart, tStop, dRate);

} // NewSegment


//
// Receive
//
HRESULT CTeeInputPin::Receive(IMediaSample *pSample)
{
  ASSERT(pSample);
  CAutoLock lock_it(m_pLock);

  // Check that all is well with the base class
  HRESULT hr = NOERROR;
  hr = CBaseInputPin::Receive(pSample);
  if(hr != NOERROR)
    return hr;

  // Walk through the output pins list, delivering to each in turn

  int n = m_pTee->m_NumOutputPins;
  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();

  while(n)
  {
    CTeeOutputPin *pOutputPin = m_pTee->m_OutputPinsList.GetNext(pos);
    if(pOutputPin != NULL)
    {
      hr = pOutputPin->Deliver(pSample);
      if(FAILED(hr))
        ASSERT(0);
    }
    else
    {
      // We should have as many pins as the count says we have
      ASSERT(FALSE);
    }
    n--;
  }

  return NOERROR;

} // Receive


//
// Completed a connection to a pin
//
HRESULT CTeeInputPin::CompleteConnect(IPin *pReceivePin)
{
  ASSERT(pReceivePin);
  
  HRESULT hr = CBaseInputPin::CompleteConnect(pReceivePin);
  if(FAILED(hr))
  {
    return hr;
  }

  // Force any output pins to use our type

  int n = m_pTee->m_NumOutputPins;
  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();

  while(n)
  {
    CTeeOutputPin *pOutputPin = m_pTee->m_OutputPinsList.GetNext(pos);
    if(pOutputPin != NULL)
    {
      // Check with downstream pin
      if(pOutputPin->m_Connected != NULL)
      {
        if(m_mt != pOutputPin->m_mt)
          m_pTee->ReconnectPin(pOutputPin, &m_mt);
      }
    }
    else
    {
      // We should have as many pins as the count says we have
      ASSERT(FALSE);
    }
    n--;
  }

  return S_OK;
}


//
// CTeeOutputPin constructor
//
CTeeOutputPin::CTeeOutputPin(TCHAR *pName,
               CTee *pTee,
               HRESULT *phr,
               LPCWSTR pPinName,
               int PinNumber) :
  CBaseOutputPin(pName, pTee, pTee, phr, pPinName) ,
  m_pOutputQueue(NULL),
  m_bHoldsSeek(FALSE),
  m_pPosition(NULL),
  m_pTee(pTee),
  m_cOurRef(0),
  m_bInsideCheckMediaType(FALSE)
{
  ASSERT(pTee);
}



#ifdef DEBUG
//
// CTeeOutputPin destructor
//
CTeeOutputPin::~CTeeOutputPin()
{
  ASSERT(m_pOutputQueue == NULL);
}
#endif


//
// NonDelegatingQueryInterface
//
// This function is overwritten to expose IMediaPosition and IMediaSelection
// Note that only one output stream can be allowed to expose this to avoid
// conflicts, the other pins will just return E_NOINTERFACE and therefore
// appear as non seekable streams. We have a LONG value that if exchanged to
// produce a TRUE means that we have the honor. If it exchanges to FALSE then
// someone is already in. If we do get it and error occurs then we reset it
// to TRUE so someone else can get it.
//
STDMETHODIMP
CTeeOutputPin::NonDelegatingQueryInterface(REFIID riid, void **ppv)
{
  CheckPointer(ppv,E_POINTER);
  ASSERT(ppv);

  *ppv = NULL;
  HRESULT hr = NOERROR;

  // See what interface the caller is interested in.
  if(riid == IID_IMediaPosition || riid == IID_IMediaSeeking)
  {
    if(m_pPosition)
    {
      if(m_bHoldsSeek == FALSE)
        return E_NOINTERFACE;
      return m_pPosition->QueryInterface(riid, ppv);
    }
  }
  else
  {
    return CBaseOutputPin::NonDelegatingQueryInterface(riid, ppv);
  }

  CAutoLock lock_it(m_pLock);
  ASSERT(m_pPosition == NULL);
  IUnknown *pMediaPosition = NULL;

  // Try to create a seeking implementation
  if(InterlockedExchange(&m_pTee->m_lCanSeek, FALSE) == FALSE)
    return E_NOINTERFACE;

  // Create implementation of this dynamically as sometimes we may never
  // try and seek. The helper object implements IMediaPosition and also
  // the IMediaSelection control interface and simply takes the calls
  // normally from the downstream filter and passes them upstream

  hr = CreatePosPassThru(GetOwner(),
               FALSE,
               (IPin *)&m_pTee->m_Input,
               &pMediaPosition);

  if(pMediaPosition == NULL)
  {
    InterlockedExchange(&m_pTee->m_lCanSeek, TRUE);
    return E_OUTOFMEMORY;
  }

  if(FAILED(hr))
  {
    InterlockedExchange(&m_pTee->m_lCanSeek, TRUE);
    pMediaPosition->Release();
    return hr;
  }

  m_pPosition = pMediaPosition;
  m_bHoldsSeek = TRUE;
  return NonDelegatingQueryInterface(riid, ppv);

} // NonDelegatingQueryInterface


//
// NonDelegatingAddRef
//
// We need override this method so that we can do proper reference counting
// on our output pin. The base class CBasePin does not do any reference
// counting on the pin in RETAIL.
//
// Please refer to the comments for the NonDelegatingRelease method for more
// info on why we need to do this.
//
STDMETHODIMP_(ULONG) CTeeOutputPin::NonDelegatingAddRef()
{
  CAutoLock lock_it(m_pLock);

#ifdef DEBUG
  // Update the debug only variable maintained by the base class
  m_cRef++;
  ASSERT(m_cRef > 0);
#endif

  // Now update our reference count
  m_cOurRef++;
  ASSERT(m_cOurRef > 0);
  return m_cOurRef;

} // NonDelegatingAddRef


//
// NonDelegatingRelease
//
// CTeeOutputPin overrides this class so that we can take the pin out of our
// output pins list and delete it when its reference count drops to 1 and there
// is atleast two free pins.
//
// Note that CreateNextOutputPin holds a reference count on the pin so that
// when the count drops to 1, we know that no one else has the pin.
//
// Moreover, the pin that we are about to delete must be a free pin(or else
// the reference would not have dropped to 1, and we must have atleast one
// other free pin(as the filter always wants to have one more free pin)
//
// Also, since CBasePin::NonDelegatingAddRef passes the call to the owning
// filter, we will have to call Release on the owning filter as well.
//
// Also, note that we maintain our own reference count m_cOurRef as the m_cRef
// variable maintained by CBasePin is debug only.
//
STDMETHODIMP_(ULONG) CTeeOutputPin::NonDelegatingRelease()
{
  CAutoLock lock_it(m_pLock);

#ifdef DEBUG
  // Update the debug only variable in CBasePin
  m_cRef--;
  ASSERT(m_cRef >= 0);
#endif

  // Now update our reference count
  m_cOurRef--;
  ASSERT(m_cOurRef >= 0);

  // if the reference count on the object has gone to one, remove
  // the pin from our output pins list and physically delete it
  // provided there are atealst two free pins in the list(including
  // this one)

  // Also, when the ref count drops to 0, it really means that our
  // filter that is holding one ref count has released it so we
  // should delete the pin as well.

  if(m_cOurRef <= 1)
  {
    int n = 2;           // default forces pin deletion
    if(m_cOurRef == 1)
    {
      // Walk the list of pins, looking for count of free pins
      n = m_pTee->GetNumFreePins();
    }

    // If there are two free pins, delete this one.
    // NOTE: normall
    if(n >= 2)
    {
      m_cOurRef = 0;
#ifdef DEBUG
      m_cRef = 0;
#endif
      m_pTee->DeleteOutputPin(this);
      return(ULONG) 0;
    }
  }

  return(ULONG) m_cOurRef;

} // NonDelegatingRelease


//
// DecideBufferSize
//
// This has to be present to override the PURE virtual class base function
//
HRESULT CTeeOutputPin::DecideBufferSize(IMemAllocator *pMemAllocator,
                    ALLOCATOR_PROPERTIES * ppropInputRequest)
{
  return NOERROR;

} // DecideBufferSize


//
// DecideAllocator
//
HRESULT CTeeOutputPin::DecideAllocator(IMemInputPin *pPin, IMemAllocator **ppAlloc)
{
  CheckPointer(pPin,E_POINTER);
  CheckPointer(ppAlloc,E_POINTER);
  ASSERT(m_pTee->m_pAllocator != NULL);

  *ppAlloc = NULL;

  // Tell the pin about our allocator, set by the input pin.
  HRESULT hr = NOERROR;
  hr = pPin->NotifyAllocator(m_pTee->m_pAllocator,TRUE);
  if(FAILED(hr))
    return hr;

  // Return the allocator
  *ppAlloc = m_pTee->m_pAllocator;
  m_pTee->m_pAllocator->AddRef();
  return NOERROR;

} // DecideAllocator


//
// CheckMediaType
//
HRESULT CTeeOutputPin::CheckMediaType(const CMediaType *pmt)
{
  CAutoLock lock_it(m_pLock);

  // If we are already inside checkmedia type for this pin, return NOERROR
  // It is possble to hookup two of the tee filters and some other filter
  // like the video effects sample to get into this situation. If we
  // do not detect this, we will loop till we blow the stack

  if(m_bInsideCheckMediaType == TRUE)
    return NOERROR;

  m_bInsideCheckMediaType = TRUE;
  HRESULT hr = NOERROR;

#ifdef DEBUG
  // Display the type of the media for debugging purposes
  DisplayMediaType(TEXT("Output Pin Checking"), pmt);
#endif

  // The input needs to have been conneced first
  if(m_pTee->m_Input.m_Connected == NULL)
  {
    m_bInsideCheckMediaType = FALSE;
    return VFW_E_NOT_CONNECTED;
  }

  // Make sure that our input pin peer is happy with this
  hr = m_pTee->m_Input.m_Connected->QueryAccept(pmt);
  if(hr != NOERROR)
  {
    m_bInsideCheckMediaType = FALSE;
    return VFW_E_TYPE_NOT_ACCEPTED;
  }

  // Check the format with the other outpin pins

  int n = m_pTee->m_NumOutputPins;
  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();

  while(n)
  {
    CTeeOutputPin *pOutputPin = m_pTee->m_OutputPinsList.GetNext(pos);
    if(pOutputPin != NULL && pOutputPin != this)
    {
      if(pOutputPin->m_Connected != NULL)
      {
        // The pin is connected, check its peer
        hr = pOutputPin->m_Connected->QueryAccept(pmt);
        if(hr != NOERROR)
        {
          m_bInsideCheckMediaType = FALSE;
          return VFW_E_TYPE_NOT_ACCEPTED;
        }
      }
    }
    n--;
  }

  m_bInsideCheckMediaType = FALSE;
  return NOERROR;

} // CheckMediaType


//
// EnumMediaTypes
//
STDMETHODIMP CTeeOutputPin::EnumMediaTypes(IEnumMediaTypes **ppEnum)
{
  CAutoLock lock_it(m_pLock);
  ASSERT(ppEnum);

  // Make sure that we are connected
  if(m_pTee->m_Input.m_Connected == NULL)
    return VFW_E_NOT_CONNECTED;

  // We will simply return the enumerator of our input pin's peer
  return m_pTee->m_Input.m_Connected->EnumMediaTypes(ppEnum);

} // EnumMediaTypes


//
// SetMediaType
//
HRESULT CTeeOutputPin::SetMediaType(const CMediaType *pmt)
{
  CAutoLock lock_it(m_pLock);

#ifdef DEBUG
  // Display the format of the media for debugging purposes
  DisplayMediaType(TEXT("Output pin type agreed"), pmt);
#endif

  // Make sure that we have an input connected
  if(m_pTee->m_Input.m_Connected == NULL)
    return VFW_E_NOT_CONNECTED;

  // Make sure that the base class likes it
  HRESULT hr = NOERROR;
  hr = CBaseOutputPin::SetMediaType(pmt);
  if(FAILED(hr))
    return hr;

  return NOERROR;

} // SetMediaType


//
// CompleteConnect
//
HRESULT CTeeOutputPin::CompleteConnect(IPin *pReceivePin)
{
  CAutoLock lock_it(m_pLock);
  ASSERT(m_Connected == pReceivePin);
  HRESULT hr = NOERROR;

  hr = CBaseOutputPin::CompleteConnect(pReceivePin);
  if(FAILED(hr))
    return hr;

  // If the type is not the same as that stored for the input
  // pin then force the input pins peer to be reconnected

  if(m_mt != m_pTee->m_Input.m_mt)
  {
    hr = m_pTee->ReconnectPin(m_pTee->m_Input.m_Connected, &m_mt);
    if(FAILED(hr))
    {
      return hr;
    }
  }

  // Since this pin has been connected up, create another output pin. We
  // will do this only if there are no unconnected pins on us. However
  // CompleteConnect will get called for the same pin during reconnection

  int n = m_pTee->GetNumFreePins();
  ASSERT(n <= 1);
  //Don't even know why it would get to 1000 but in case its the limit
  if(n == 1 || m_pTee->m_NumOutputPins == 1000)
    return NOERROR;

  // No unconnected pins left so spawn a new one

  CTeeOutputPin *pOutputPin = m_pTee->CreateNextOutputPin(m_pTee);
  if(pOutputPin != NULL)
  {
    m_pTee->m_NumOutputPins++;
    m_pTee->m_OutputPinsList.AddTail(pOutputPin);
    m_pTee->IncrementPinVersion();
  }

  // At this point we should be able to send some
  // notification that we have sprung a new pin

  return NOERROR;

} // CompleteConnect


//
// Active
//
// This is called when we start running or go paused. We create the
// output queue object to send data to our associated peer pin
//
HRESULT CTeeOutputPin::Active()
{
  CAutoLock lock_it(m_pLock);
  HRESULT hr = NOERROR;

  // Make sure that the pin is connected
  if(m_Connected == NULL)
    return NOERROR;

  // Create the output queue if we have to
  if(m_pOutputQueue == NULL)
  {
    m_pOutputQueue = new COutputQueue(m_Connected, &hr, TRUE, FALSE);
    if(m_pOutputQueue == NULL)
      return E_OUTOFMEMORY;

    // Make sure that the constructor did not return any error
    if(FAILED(hr))
    {
      delete m_pOutputQueue;
      m_pOutputQueue = NULL;
      return hr;
    }
  }

  // Pass the call on to the base class
  CBaseOutputPin::Active();
  return NOERROR;

} // Active


//
// Inactive
//
// This is called when we stop streaming
// We delete the output queue at this time
//
HRESULT CTeeOutputPin::Inactive()
{
  CAutoLock lock_it(m_pLock);

  // Delete the output queus associated with the pin.
  if(m_pOutputQueue)
  {
    delete m_pOutputQueue;
    m_pOutputQueue = NULL;
  }

  CBaseOutputPin::Inactive();
  return NOERROR;

} // Inactive


//
// Deliver
//
HRESULT CTeeOutputPin::Deliver(IMediaSample *pMediaSample)
{
  CheckPointer(pMediaSample,E_POINTER);

  // Make sure that we have an output queue
  if(m_pOutputQueue == NULL)
    return NOERROR;

  pMediaSample->AddRef();
  return m_pOutputQueue->Receive(pMediaSample);

} // Deliver


//
// DeliverEndOfStream
//
HRESULT CTeeOutputPin::DeliverEndOfStream()
{
  // Make sure that we have an output queue
  if(m_pOutputQueue == NULL)
    return NOERROR;

  m_pOutputQueue->EOS();
  return NOERROR;

} // DeliverEndOfStream


//
// DeliverBeginFlush
//
HRESULT CTeeOutputPin::DeliverBeginFlush()
{
  // Make sure that we have an output queue
  if(m_pOutputQueue == NULL)
    return NOERROR;

  m_pOutputQueue->BeginFlush();
  return NOERROR;

} // DeliverBeginFlush


//
// DeliverEndFlush
//
HRESULT CTeeOutputPin::DeliverEndFlush()
{
  // Make sure that we have an output queue
  if(m_pOutputQueue == NULL)
    return NOERROR;

  m_pOutputQueue->EndFlush();
  return NOERROR;

} // DeliverEndFlish

//
// DeliverNewSegment
//
HRESULT CTeeOutputPin::DeliverNewSegment(REFERENCE_TIME tStart, 
                     REFERENCE_TIME tStop,  
                     double dRate)
{
  // Make sure that we have an output queue
  if(m_pOutputQueue == NULL)
    return NOERROR;

  m_pOutputQueue->NewSegment(tStart, tStop, dRate);
  return NOERROR;

} // DeliverNewSegment


//
// Notify
//
STDMETHODIMP CTeeOutputPin::Notify(IBaseFilter *pSender, Quality q)
{
  // We pass the message on, which means that we find the quality sink
  // for our input pin and send it there

  POSITION pos = m_pTee->m_OutputPinsList.GetHeadPosition();
  CTeeOutputPin *pFirstOutput = m_pTee->m_OutputPinsList.GetNext(pos);

  if(this == pFirstOutput)
  {
    if(m_pTee->m_Input.m_pQSink!=NULL)
    {
      return m_pTee->m_Input.m_pQSink->Notify(m_pTee, q);
    }
    else
    {
      // No sink set, so pass it upstream
      HRESULT hr;
      IQualityControl * pIQC;

      hr = VFW_E_NOT_FOUND;
      if(m_pTee->m_Input.m_Connected)
      {
        m_pTee->m_Input.m_Connected->QueryInterface(IID_IQualityControl,(void**)&pIQC);

        if(pIQC!=NULL)
        {
          hr = pIQC->Notify(m_pTee, q);
          pIQC->Release();
        }
      }
      return hr;
    }
  }

  // Quality management is too hard to do
  return NOERROR;

} // Notify
