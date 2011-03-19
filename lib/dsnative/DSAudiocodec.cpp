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
#include "dsaudiocodec.h"
#include "log.h"
#include "SmartPtr.h"
#include "FGFilter.h"

DSAudioCodec::DSAudioCodec(const char *cfname, const GUID guid, const GUID rendererGuid, CMediaType *wvfmt, const char *sfname) 
  :  m_guid(guid), m_renderer_guid(rendererGuid), m_wvfmt(wvfmt), m_hDll(NULL), m_outfmt(NULL), 
     m_frametime(0), m_discontinuity(1), m_pFrameDurationDivision(0), m_currentTime(0),
     m_pFilter(NULL), m_pInputPin(NULL), m_pOutputPin(NULL), m_pOutputQueue(NULL),
     m_pOurInput(NULL), m_pClock(NULL), m_pNullRenderer(NULL),
     m_pMemInputPin(NULL), m_pMemAllocator(NULL), m_pSFilter(NULL),
     m_pAudioRenderer(NULL), m_pAudioRendererInputPin(NULL), m_pGraph(NULL), m_pMC(NULL),
     m_cfname(NULL), m_sfname(NULL),m_newmediatype(1)
{
  int len;

  ASSERT(cfname);
  

  len = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, cfname, -1, NULL, 0);
  if (len > 0)
  {
      m_cfname = new wchar_t[len];
      MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, cfname, -1, m_cfname, len);
  }

  if (sfname)
  {
    len = MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, sfname, -1, NULL, 0);
    if (len > 0)
    {
      m_sfname = new wchar_t[len];
      MultiByteToWideChar(CP_ACP, MB_PRECOMPOSED, sfname, -1, m_sfname, len);
    }
  }
}

DSAudioCodec::~DSAudioCodec()
{
  ReleaseGraph();
  if (m_cfname) 
    delete m_cfname;
  if (m_sfname) 
    delete m_sfname;
  if (m_wvfmt)
    m_wvfmt = NULL;
  if (m_hDll) 
    FreeLibrary(m_hDll);
}

void DSAudioCodec::ReleaseGraph()
{
  if (m_pMC) 
    m_res = m_pMC->Stop();
  else if (m_pFilter)
    m_res = m_pFilter->Stop();

  if (m_pGraph)
  {
    if (m_pFilter)
    {
      SAFE_REMOVE_FILTER(m_pGraph, m_pFilter);
      SAFE_REMOVE_FILTER(m_pGraph, m_pSFilter);
      SAFE_REMOVE_FILTER(m_pGraph, m_pAudioRenderer);
    }
    RemoveFromRot(m_dwRegister);
  }
  else
  {
    if (m_pInputPin) m_res = m_pInputPin->Disconnect();
    if (m_pOutputPin) m_res = m_pOutputPin->Disconnect();
    if (m_pFilter) m_res = m_pFilter->JoinFilterGraph(NULL, NULL);
    if (m_pAudioRenderer) m_res = m_pAudioRenderer->JoinFilterGraph(NULL, NULL);

    if (m_pAudioRendererInputPin) m_res = m_pAudioRendererInputPin->Disconnect();
    if (m_pOurInput) m_res = m_pOurInput->Disconnect();
    
  }

  if (m_pMemInputPin) m_pMemInputPin->Release();
  if (m_pFilter) m_res = m_pFilter->Release();
  if (m_pSFilter) m_res = m_pSFilter->Release();
  if (m_pAudioRenderer) m_res = m_pAudioRenderer->Release();
  }

BOOL DSAudioCodec::LoadLibrary()
{
  HKEY hKey = NULL;

  while ((m_hDll = ::LoadLibraryW(m_cfname)) == NULL)
  {
    /* Try picking path from the registry, if the codecs is registered in the system */
    LONG size;
    wchar_t subkey[61] = L"\\CLSID\\";
    size = (sizeof(subkey) / 2) - 7;

    if (StringFromGUID2(m_guid, subkey + 7, size) != 39)
      break;

    size -= 39;
    wcsncat(subkey, L"\\InprocServer32", size);

    if (RegOpenKeyW(HKEY_CLASSES_ROOT, subkey, &hKey) != ERROR_SUCCESS)
      break;

    if (RegQueryValueW(hKey, NULL, NULL, &size) != ERROR_SUCCESS)
      break;

     delete m_cfname;

     m_cfname = new wchar_t[size];
     if (RegQueryValueW(hKey, NULL, m_cfname, &size) == ERROR_SUCCESS)
       m_hDll = ::LoadLibraryW(m_cfname);

     break;
   }

   if (hKey) 
     RegCloseKey(hKey);

   return (m_hDll != NULL);
}

BOOL DSAudioCodec::CreateFilter()
{
  LPFNGETCLASSOBJECT pDllGetClassObject = (LPFNGETCLASSOBJECT) GetProcAddress(m_hDll, "DllGetClassObject");
  if (!pDllGetClassObject) 
    return FALSE;

  IClassFactory *factory;
  m_res = pDllGetClassObject(m_guid, IID_IClassFactory, (LPVOID *) &factory);
  if (m_res != S_OK) 
    return FALSE;

  IUnknown* object;
  m_res = factory->CreateInstance(NULL, IID_IUnknown, (LPVOID *) &object);
  factory->Release();

  if (m_res != S_OK) 
    return FALSE;

  m_res = object->QueryInterface(IID_IBaseFilter, (LPVOID *) &m_pFilter);
  object->Release();

   //No point to load the audio renderer if the codec failed
   if (m_res == S_OK)
   {
     
     CFGFilterRegistry* pAudioRenderer = new CFGFilterRegistry(m_renderer_guid);
     HRESULT hr = pAudioRenderer->Create(&m_pAudioRenderer);
     if (SUCCEEDED(hr))
     {
       CLog::Log(LOGINFO,"DSAudioCodec::CreateFilter() succeed with audio renderer");
     }
     //CoCreateInstance(m_renderer_guid, NULL, CLSCTX_ALL, IID_IBaseFilter, (void**)&);

#if 0 //this way is not working for a lot of renderer
     Com::SmartPtr<IMoniker> pMoniker;
   Com::SmartPtr<IPropertyBag> pPropertyBag;
   Com::SmartPtr<IBindCtx> pBindCtx;
   VARIANT var;
   CreateBindCtx(0, &pBindCtx);
   if (SUCCEEDED(CoCreateInstance(m_renderer_guid, NULL, CLSCTX_ALL, IID_IBaseFilter, (void**)&m_pAudioRenderer))
     && SUCCEEDED(CreateClassMoniker(m_renderer_guid,&pMoniker))
     && SUCCEEDED(pMoniker->BindToStorage(pBindCtx, 0, IID_IPropertyBag, (void**)&pPropertyBag))
     && SUCCEEDED(pPropertyBag->Read(LPCOLESTR(L"FriendlyName"), &var, NULL)))
   {
     
     m_strRendererName = var.bstrVal;
     
     
   }
#endif
   }
  return (m_res == S_OK);
}

BOOL DSAudioCodec::CheckMediaTypes(IPin *pin)
{
  IEnumMediaTypes *pMedia;
  AM_MEDIA_TYPE *pmt = NULL, *pfnt = NULL;
  HRESULT m_res = pin->EnumMediaTypes(&pMedia);
  pMedia->Reset();

  while((m_res = pMedia->Next(1, &pmt, NULL)) == S_OK)
  {
    if (pmt->formattype == FORMAT_VideoInfo)
    {
      VIDEOINFOHEADER *vih = (VIDEOINFOHEADER *) pmt->pbFormat;
      DeleteMediaType(pmt);
    }
  }
  pMedia->Release();
  return TRUE;
}

dsnerror_t DSAudioCodec::SetOutputType()
{
  unsigned long fetched;
  AM_MEDIA_TYPE *mediaTypes = (AM_MEDIA_TYPE*)alloca(sizeof(AM_MEDIA_TYPE));
  IEnumMediaTypes *ppEnum = NULL;
  m_pOutputPin->EnumMediaTypes(&ppEnum);
  VIDEOINFOHEADER *vih;
  VIDEOINFOHEADER2 *vih2;
  while (ppEnum->Next(1,&mediaTypes,&fetched) == S_OK) 
  {
    if (!fetched) 
      break;
    m_res = m_pOutputPin->QueryAccept(mediaTypes);

    if (m_res == S_OK)
    {
      CopyMediaType(&m_pDestType,mediaTypes);
      return DSN_OK;
    }
  }

}

BOOL DSAudioCodec::SetInputType()
{
  ULONG cbFormat;
  
  CopyMediaType(&m_pOurType,m_wvfmt);
  HRESULT hr = m_pInputPin->QueryAccept(&m_pOurType);
  
  //Sample size of audio input should be the same value as the waveformat->nBlockAlign
  return TRUE;
}

BOOL DSAudioCodec::EnumPins()
{
  IEnumPins *enumpins;
  if (m_pFilter->EnumPins(&enumpins) != S_OK)
    return FALSE;

  enumpins->Reset();

  IPin *pin;
  PIN_INFO pInfo;

  // FIXME: ffdshow has 2 input pins "In" and "In Text"
  // there is not way to check mediatype before connection
  // I think I need a list of pins and then probe for all :(
  while ((m_res = enumpins->Next(1, &pin, NULL)) == S_OK)
  {
    pin->QueryPinInfo(&pInfo);
    /* wprintf(L"Pin: %s - %s\n", pInfo.achName, (pInfo.dir == PINDIR_INPUT) ? L"Input" : L"Output"); */
    if (!m_pInputPin && (pInfo.dir == PINDIR_INPUT))
      m_pInputPin = pin;
    else if (!m_pOutputPin && (pInfo.dir == PINDIR_OUTPUT))
      m_pOutputPin = pin;

    pin->Release();
    m_pFilter->Release();
  }

  enumpins->Release();

  if (!(m_pInputPin && m_pInputPin))
    return FALSE;

  if (m_pInputPin->QueryInterface(IID_IMemInputPin, (LPVOID *) &m_pMemInputPin) != S_OK)
    return FALSE;
  
  
  /* End audio codec */
  /* Begin audio renderer */
  if (m_pAudioRenderer->EnumPins(&enumpins) != S_OK)
    return FALSE;

  enumpins->Reset();



  // FIXME: ffdshow has 2 input pins "In" and "In Text"
  // there is not way to check mediatype before connection
  // I think I need a list of pins and then probe for all :(
  while ((m_res = enumpins->Next(1, &pin, NULL)) == S_OK)
  {
    pin->QueryPinInfo(&pInfo);
    /* wprintf(L"Pin: %s - %s\n", pInfo.achName, (pInfo.dir == PINDIR_INPUT) ? L"Input" : L"Output"); */
    if (!m_pAudioRendererInputPin && (pInfo.dir == PINDIR_INPUT))
      m_pAudioRendererInputPin = pin;
    

    pin->Release();
    m_pAudioRenderer->Release();
  }

  enumpins->Release();

  return TRUE;
}

dsnerror_t DSAudioCodec::SetupAllocator()
{
  DSN_CHECK(m_pMemInputPin->GetAllocator(&m_pMemAllocator), DSN_FAIL_ALLOCATOR);
  ALLOCATOR_PROPERTIES props, props1;
  props.cBuffers = 1;
  //This might fail or be too big for what we need a dynamic allocation would be much better
  props.cbBuffer = m_pOurType.lSampleSize;
  props.cbAlign = 1;
  props.cbPrefix = 0;

  DSN_CHECK(m_pMemAllocator->SetProperties(&props, &props1), DSN_FAIL_ALLOCATOR);
  DSN_CHECK(m_pMemInputPin->NotifyAllocator(m_pMemAllocator, FALSE), DSN_FAIL_ALLOCATOR);
  DSN_CHECK(m_pMemAllocator->Commit(), DSN_FAIL_ALLOCATOR);

  return DSN_OK;
}

HRESULT IsPinConnected(IPin *pPin, BOOL *pResult)
{
  IPin *pTmp = NULL;
  HRESULT hr = pPin->ConnectedTo(&pTmp);
  if (SUCCEEDED(hr))
  {
    *pResult = TRUE;
  }
  else if (hr == VFW_E_NOT_CONNECTED)
  {
    // The pin is not connected. This is not an error for our purposes.
    *pResult = FALSE;
    hr = S_OK;
  }
  
  SAFE_RELEASE(pTmp);
  return hr;
}


// Query whether a pin has a specified direction (input / output)
HRESULT IsPinDirection(IPin *pPin, PIN_DIRECTION dir, BOOL *pResult)
{
  PIN_DIRECTION pinDir;
  HRESULT hr = pPin->QueryDirection(&pinDir);
  if (SUCCEEDED(hr))
    *pResult = (pinDir == dir);
  return hr;
}

HRESULT MatchPin(IPin *pPin, PIN_DIRECTION direction, BOOL bShouldBeConnected, BOOL *pResult)
{
  assert(pResult != NULL);

  BOOL bMatch = FALSE;
  BOOL bIsConnected = FALSE;

  HRESULT hr = IsPinConnected(pPin, &bIsConnected);
  if (SUCCEEDED(hr))
  {
    if (bIsConnected == bShouldBeConnected)
      hr = IsPinDirection(pPin, direction, &bMatch);
  }

  if (SUCCEEDED(hr))
    *pResult = bMatch;
  return hr;
}

// Return the first unconnected input pin or output pin.
HRESULT FindUnconnectedPin(IBaseFilter *pFilter, PIN_DIRECTION PinDir, IPin **ppPin)
{
  IEnumPins *pEnum = NULL;
  IPin *pPin = NULL;
  BOOL bFound = FALSE;

  HRESULT hr = pFilter->EnumPins(&pEnum);
  if (FAILED(hr))
    goto done;

  while (S_OK == pEnum->Next(1, &pPin, NULL))
  {
    hr = MatchPin(pPin, PinDir, FALSE, &bFound);
    if (FAILED(hr))
      goto done;

    if (bFound)
    {
      *ppPin = pPin;
      (*ppPin)->AddRef();
      break;
    }
    SAFE_RELEASE(pPin);
  }

  if (!bFound)
    hr = VFW_E_NOT_FOUND;

done:
  SAFE_RELEASE(pPin);
  SAFE_RELEASE(pEnum);
  return hr;
}

HRESULT ConnectFilters(IGraphBuilder *pGraph, IPin *pOut, IBaseFilter *pDest)
{
  IPin *pIn = NULL;
  // Find an input pin on the downstream filter.
  HRESULT hr = FindUnconnectedPin(pDest, PINDIR_INPUT, &pIn);
  if (SUCCEEDED(hr))
  {
    // Try to connect them.
    hr = pGraph->Connect(pOut, pIn);
    pIn->Release();
  }
  return hr;
}

HRESULT ConnectFilters(IGraphBuilder *pGraph, IBaseFilter *pSrc, IBaseFilter *pDest)
{
  IPin *pOut = NULL;

  // Find an output pin on the first filter.
  HRESULT hr = FindUnconnectedPin(pSrc, PINDIR_OUTPUT, &pOut);
  if (SUCCEEDED(hr))
  {
    hr = ConnectFilters(pGraph, pOut, pDest);
    pOut->Release();
  }
  return hr;
}

dsnerror_t DSAudioCodec::CreateGraph(bool buildgraph)
{
  HRESULT hr = S_OK;

  if (!EnumPins())
    return DSN_FAIL_ENUM;

  if (!SetInputType())
    return DSN_INPUT_NOTACCEPTED;

  m_pSFilter = new CSenderFilter();
  m_pDupFilter = new CTee(L"Tee",NULL,&hr);
  m_pNullRenderer = new CNullRenderer();
  m_pOurInput = (CSenderPin *) m_pSFilter->GetPin(0);
  
  /* setup Source filename if someone wants to known it (i.e. ffdshow) */
  m_pSFilter->Load(m_sfname, NULL);
  m_pSFilter->AddRef();

  if (buildgraph)
  {
    DSN_CHECK(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_ALL, IID_IGraphBuilder, (void **) &m_pGraph), DSN_FAIL_GRAPH);

    DSN_CHECK(DSAudioCodec::AddToRot(m_pGraph, &m_dwRegister), DSN_FAIL_GRAPH);
    DSN_CHECK(m_pGraph->QueryInterface(IID_IMediaControl, (void **) &m_pMC), DSN_FAIL_GRAPH);

    m_pGraph->SetLogFile((DWORD_PTR) GetStdHandle(STD_OUTPUT_HANDLE));
    DSN_CHECK(m_pGraph->AddFilter(m_pFilter, m_sfname), (m_pInputPin = m_pOutputPin = NULL, DSN_FAIL_GRAPH));
    DSN_CHECK(m_pGraph->AddFilter(m_pSFilter, L"DS Sender"), DSN_FAIL_GRAPH);
    DSN_CHECK(m_pGraph->AddFilter(m_pAudioRenderer, L"AudioRenderer"), DSN_FAIL_GRAPH);
    DSN_CHECK(m_pGraph->AddFilter(m_pNullRenderer, L"NULL Renderer"), DSN_FAIL_GRAPH);
    DSN_CHECK(m_pGraph->AddFilter(m_pDupFilter, L"Tee"), DSN_FAIL_GRAPH);

    // Connect our output pin to codec input pin otherwise QueryAccept on the codec output pin will fail
    //DSN_CHECK(ConnectFilters(m_pGraph, m_pSFilter, m_pFilter), DSN_OUTPUT_CONNFAILED);
    hr = m_pGraph->ConnectDirect(m_pOurInput, m_pInputPin, &m_pOurType);
    DSN_CHECK(hr, DSN_INPUT_CONNFAILED);
  }
  else
  {
    m_res = m_pFilter->JoinFilterGraph((IFilterGraph *) m_pSFilter, L"DSNative Graph");
    /* same of above */
    DSN_CHECK(m_pInputPin->ReceiveConnection(m_pOurInput, &m_pOurType), DSN_INPUT_CONNFAILED);
  }

  SetOutputType();
  
  //DSN_CHECK(ConnectFilters(m_pGraph, m_pOutputPin, m_pDupFilter), DSN_OUTPUT_CONNFAILED);
  
  DSN_CHECK(ConnectFilters(m_pGraph, m_pFilter, m_pDupFilter), DSN_OUTPUT_CONNFAILED);
  DSN_CHECK(ConnectFilters(m_pGraph, m_pDupFilter, m_pAudioRenderer), DSN_OUTPUT_CONNFAILED);
  DSN_CHECK(ConnectFilters(m_pGraph, m_pDupFilter, m_pNullRenderer), DSN_OUTPUT_CONNFAILED);
  
  //DSN_CHECK(m_pGraph->ConnectDirect(m_pOutputPin, m_pAudioRendererInputPin, NULL), DSN_OUTPUT_CONNFAILED);
  
  
  return DSN_OK;
}

HRESULT DSAudioCodec::AddToRot(IUnknown *pUnkGraph, DWORD *pdwRegister)
{
  IMoniker *pMoniker = NULL;
  IRunningObjectTable *pROT = NULL;

  if (FAILED(GetRunningObjectTable(0, &pROT)))
    return E_FAIL;

  WCHAR wsz[256];
  StringCchPrintfW(wsz, 256, L"FilterGraph %08x pid %08x (dsnative)", (DWORD_PTR) pUnkGraph, GetCurrentProcessId());
  HRESULT hr = CreateItemMoniker(L"!", wsz, &pMoniker);
  if (SUCCEEDED(hr))
  {
    hr = pROT->Register(ROTFLAGS_REGISTRATIONKEEPSALIVE, pUnkGraph, pMoniker, pdwRegister);
    pMoniker->Release();
  }
  pROT->Release();

  return hr;
}


void DSAudioCodec::RemoveFromRot(DWORD pdwRegister)
{
  IRunningObjectTable *pROT;
  if (SUCCEEDED(GetRunningObjectTable(0, &pROT)))
  {
    pROT->Revoke(pdwRegister);
    pROT->Release();
  }
}

BOOL DSAudioCodec::StartGraph()
{
  SetupAllocator();
  if (m_pMC) 
  {
    m_pMC->Run();
  }
  else
  {
    m_pFilter->Run(0);
    m_pAudioRenderer->Run(0);
  }
  Resync(0); // NewSegment + discontinuity /* e.g. ffdshow will not init byte count */
  return TRUE;
}

dsnerror_t DSAudioCodec::Decode(const BYTE *src, int size, int *usedByte)
{

  IMediaSample* sample = NULL;
  int pts,keyframe = 0;
  BYTE *ptr;
  HRESULT hr = S_OK;
  
  DSN_CHECK(m_pMemAllocator->GetBuffer(&sample, 0, 0, 0), DSN_FAIL_DECODESAMPLE);
  
  if (!m_pClock)
  {
    
    m_pNullRenderer->QueryInterface(IID_IReferenceClock,(void**)&m_pClock);
    Com::SmartPtr<IMediaFilter> pMF;
    hr = m_pGraph->QueryInterface(IID_IMediaFilter,(void**)&pMF);
    pMF->SetSyncSource(m_pClock);
    m_pNullRenderer->SetSyncSource(m_pClock);
    m_pAudioRenderer->SetSyncSource(m_pClock);
    CRefTime time;
    m_pClock->GetTime((REFERENCE_TIME*)&time);
    m_currentTime = time;
  }
  
  
  hr = sample->SetActualDataLength(size);
  DSN_CHECK(sample->GetPointer(&ptr), DSN_FAIL_DECODESAMPLE);
  memcpy(ptr, src, size);

  if (m_newmediatype)
  {
    AM_MEDIA_TYPE MediaType;
    CopyMediaType(&MediaType, &m_pOurType);
    sample->SetMediaType(&MediaType);
    FreeMediaType(MediaType);
    
    m_discontinuity = 1;
    m_newmediatype = 0;
  }
  REFERENCE_TIME dur = 1;//MulDiv(10000000i64, wfmt->wBitsPerSample, wfmt->nSamplesPerSec);
  AM_MEDIA_TYPE* pmt = NULL;
  CMediaType mt;
  if(SUCCEEDED(m_pOutputPin->ConnectionMediaType(&mt)))
  {
    
    WAVEFORMATEX* wfmt = (WAVEFORMATEX*)mt.pbFormat;
    //dur =  MulDiv(size , wfmt->wBitsPerSample, 1000);
    
    dur = wfmt->nAvgBytesPerSec / (wfmt->nChannels * wfmt->nBlockAlign);
    dur = dur*10;

    //dur = MulDiv(size,1000000,m_pFrameDurationDivision);//((double)size * 1000000) / m_pFrameDurationDivision;
    //DeleteMediaType(mt);
    //pmt = NULL;
  }  
  
  
  
  /*if (!m_pFrameDurationDivision)
  {
    
  }
  int pFrameDuration = 1;
  if (m_pFrameDurationDivision > 0)
  {
    pFrameDuration = ((double)size * 1000000) / m_pFrameDurationDivision;
    pFrameDuration = m_pFrameDurationDivision*10;
  }*/
  REFERENCE_TIME start = m_currentTime;
  REFERENCE_TIME stop = m_currentTime + dur;
  m_currentTime = stop;
  DSN_CHECK(sample->SetTime(&start, &stop), DSN_FAIL_DECODESAMPLE);
  DSN_CHECK(sample->SetMediaTime(0, 0), DSN_FAIL_DECODESAMPLE);

  DSN_CHECK(sample->SetSyncPoint(1), DSN_FAIL_DECODESAMPLE);
  DSN_CHECK(sample->SetPreroll(FALSE), DSN_FAIL_DECODESAMPLE);
  DSN_CHECK(sample->SetDiscontinuity(m_discontinuity), DSN_FAIL_DECODESAMPLE);

  m_discontinuity = 0;
  
  //hr = m_pOutputQueue->Receive(sample);
  
  hr = m_pMemInputPin->Receive(sample);
  if (SUCCEEDED(hr))
    *usedByte = size;
  else
    *usedByte = 0;
  sample->Release();

  //*pImageSize = m_pOurOutput->GetPointerSize();
  //*newpts = RT2PTS(m_pOurOutput->GetPTS());
  return DSN_OK;
}

dsnerror_t DSAudioCodec::Resync(REFERENCE_TIME pts)
{
  m_res = m_pInputPin->NewSegment(pts, pts + m_frametime, 1.0);
  //m_pOutputQueue->NewSegment(pts, pts + m_frametime, 1.0);
  m_res = m_pAudioRendererInputPin->NewSegment(pts, pts + m_frametime, 1.0);

  
  m_discontinuity = 1;
  return DSN_OK;
}

BOOL DSAudioCodec::ShowPropertyPage()
{
  if (!m_pFilter) 
    return FALSE;
  ISpecifyPropertyPages *pProp;
  if ((m_res = m_pFilter->QueryInterface(IID_ISpecifyPropertyPages, (LPVOID *) &pProp)) == S_OK)
  {
    // Get the filter's name and IUnknown pointer.
    FILTER_INFO FilterInfo;
    m_res = m_pFilter->QueryFilterInfo(&FilterInfo);
    IUnknown *pFilterUnk;
    m_res = m_pFilter->QueryInterface(IID_IUnknown, (LPVOID *) &pFilterUnk);
    CAUUID caGUID;
    pProp->GetPages(&caGUID);
    pProp->Release();
    
    __try
    {
      m_res = OleCreatePropertyFrame(
      NULL,   // Parent window
      0, 0,   // Reserved
      FilterInfo.achName,     // Caption for the dialog box
      1,      // Number of objects (just the filter)
      &pFilterUnk,  // Array of object pointers.
      caGUID.cElems,    // Number of property pages
      caGUID.pElems,    // Array of property page CLSIDs
      0,      // Locale identifier
      0, NULL     // Reserved
      );
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
    }
    // Clean up.
    pFilterUnk->Release();
    /* FIXME: it crashes (broken example on msdn?) */
    //FilterInfo.pGraph->Release();
    CoTaskMemFree(caGUID.pElems);
  }
  return (!FAILED(m_res));
}


CMediaType DSAudioCodec::GetOutputMediaType()
{
  CMediaType mt;

  m_pAudioRendererInputPin->ConnectionMediaType(&mt);
  return mt;

}