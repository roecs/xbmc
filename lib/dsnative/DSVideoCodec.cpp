/*
 * DShow Native wrapper
 * Copyright (c) 2010 Gianluigi Tiesi <sherpya@netfarm.it>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
*/

#include "stdafx.h"
#include "dsVideocodec.h"
#include "ExtradataParser.h"
#include "H264Nalu.h"
#pragma comment (lib,"Quartz.lib")

DSVideoCodec::DSVideoCodec(const char *cfname, IDSInfoCallback *pCallback, const GUID guid, BITMAPINFOHEADER *bih, unsigned int outfmt, REFERENCE_TIME frametime, const char *sfname) 
  :  m_pCallback(pCallback), m_guid(guid), m_bih(bih), m_hDll(NULL), m_outfmt(outfmt), 
     m_frametime(frametime), m_vinfo(NULL), m_discontinuity(1), m_pEvr(NULL),
     m_pFilter(NULL), m_pInputPin(NULL), m_pOutputPin(NULL), m_pEvrInputPin(NULL),
     m_pOurInput(NULL), m_pOurOutput(NULL),
     m_pMemInputPin(NULL), m_pMemAllocator(NULL), m_pSFilter(NULL), 
     m_pRFilter(NULL), m_pGraph(NULL), m_pMC(NULL), 
     m_cfname(NULL), m_sfname(NULL),m_currentframeindex(0)
{
  int len;

  ASSERT(cfname);
  ASSERT(bih);

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

DSVideoCodec::~DSVideoCodec()
{
  if (m_pEvr)
    m_pEvr->ProcessMessage(MFVP_MESSAGE_ENDOFSTREAM, 0);
  ReleaseGraph();
  
  if (m_cfname) 
    delete m_cfname;
  if (m_sfname) 
    delete m_sfname;
  if (m_vinfo) 
    delete m_vinfo;
  if (m_hDll) 
    FreeLibrary(m_hDll);
}

void DSVideoCodec::ReleaseGraph()
{
  if (m_pMC) 
    m_res = m_pMC->Stop();
  else if (m_pFilter)
    m_res = m_pFilter->Stop();
  if (m_pEvrFilter)
    m_pEvrFilter->Stop();
  

  if (m_pGraph)
  {
    if (m_pFilter)
    {
      ASSERT(m_pSFilter);
      ASSERT(m_pRFilter);
      SAFE_REMOVE_FILTER(m_pGraph, m_pEvrFilter);
      SAFE_REMOVE_FILTER(m_pGraph, m_pFilter);
      SAFE_REMOVE_FILTER(m_pGraph, m_pSFilter);
      SAFE_REMOVE_FILTER(m_pGraph, m_pRFilter);
    }
    RemoveFromRot(m_dwRegister);
  }
  else
  {
    if (m_pInputPin) m_res = m_pInputPin->Disconnect();
    if (m_pOutputPin) m_res = m_pOutputPin->Disconnect();
    if (m_pFilter) m_res = m_pFilter->JoinFilterGraph(NULL, NULL);

    if (m_pOurInput) m_res = m_pOurOutput->Disconnect();
    if (m_pOurOutput) m_res = m_pOurOutput->Disconnect();
  }

  if (m_pMemInputPin) m_pMemInputPin->Release();
  if (m_pFilter) m_res = m_pFilter->Release();
  if (m_pSFilter) m_res = m_pSFilter->Release();
  if (m_pRFilter) m_res = m_pRFilter->Release();
  }

BOOL DSVideoCodec::LoadLibrary()
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

   if (hKey) RegCloseKey(hKey);
     return (m_hDll != NULL);
}

BOOL DSVideoCodec::CreateFilter()
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

  return (m_res == S_OK);
}

BOOL DSVideoCodec::CreateEvr(HWND window)
{
  HRESULT hr = CoCreateInstance(CLSID_EnhancedVideoRenderer,0, CLSCTX_INPROC_SERVER,IID_IBaseFilter,(void**) &m_pEvrFilter);
  if (FAILED(hr))
    return FALSE;
  IMFGetService *evr_services;
  hr = m_pEvrFilter->QueryInterface(IID_IMFGetService,(void**)&evr_services);
  if (FAILED(hr))
    return FALSE;

  IMFVideoDisplayControl* mfvideodisplaycontrol;
  hr = evr_services->GetService(MR_VIDEO_RENDER_SERVICE,IID_IMFVideoDisplayControl,(void**)&mfvideodisplaycontrol);
  if (FAILED(hr))
    return FALSE;

  evr_services->Release();

  hr = mfvideodisplaycontrol->SetVideoWindow(window);
  //RECT client;
  //GetClientRect(((OsdWin*) Osd::getInstance())->getWindow(), &client);
  //mfvideodisplaycontrol->SetVideoPosition(NULL,&client);

  mfvideodisplaycontrol->Release();


    ///  if (vmrdeinterlacing!=0) vmrfilconfig->SetNumberOfStreams(1);//Enter Mixing Mode //always the case for evr!

  IMFVideoRenderer *mfvideorenderer;
  hr = m_pEvrFilter->QueryInterface(IID_IMFVideoRenderer,(void**)&mfvideorenderer);
  if (FAILED(hr))
    return FALSE;
      
      
  m_pEvr = new DsAllocator(m_pCallback);
  HRESULT hres=mfvideorenderer->InitializeRenderer(NULL,m_pEvr);

  mfvideorenderer->Release();
return TRUE;
}

BOOL DSVideoCodec::CheckMediaTypes(IPin *pin)
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

dsnerror_t DSVideoCodec::SetOutputType()
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

    if (mediaTypes->formattype == FORMAT_VideoInfo)
    {
      m_res = m_pOutputPin->QueryAccept(mediaTypes);
      if (m_res == S_OK)
      {
        CopyMediaType(&m_pDestType, mediaTypes);
        vih = (VIDEOINFOHEADER *) mediaTypes->pbFormat;
        memset(&m_vi, 0, sizeof(m_vi));
        memcpy(&m_vi.bmiHeader, &vih->bmiHeader, sizeof(m_vi.bmiHeader));
  
        return (m_res == S_OK) ? DSN_OK : DSN_OUTPUT_NOTACCEPTED;
      }
    }
    else if (mediaTypes->formattype == FORMAT_VideoInfo2)
    {
      m_res = m_pOutputPin->QueryAccept(mediaTypes);
      if (m_res == S_OK)
      {
        CopyMediaType(&m_pDestType, mediaTypes);
        vih2 = (VIDEOINFOHEADER2 *) mediaTypes->pbFormat;
        memset(&m_vi, 0, sizeof(m_vi));
        memcpy(&m_vi.bmiHeader, &vih2->bmiHeader, sizeof(m_vi.bmiHeader));
        return (m_res == S_OK) ? DSN_OK : DSN_OUTPUT_NOTACCEPTED;
      }

    }
  }

  /*if the previous way didnt work this should*/
  m_pDestType.majortype = MEDIATYPE_Video;
  m_pDestType.bFixedSizeSamples = TRUE;
  m_pDestType.bTemporalCompression = FALSE;
  m_pDestType.pUnk = 0;

  memset(&m_vi, 0, sizeof(m_vi));
  memcpy(&m_vi.bmiHeader, m_bih, sizeof(m_vi.bmiHeader));

  memset(&m_vi2, 0, sizeof(m_vi2));
  memcpy(&m_vi2.bmiHeader, m_bih, sizeof(m_vi2.bmiHeader));

  m_vi.bmiHeader.biSize = m_vi2.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  m_vi.bmiHeader.biCompression = m_vi2.bmiHeader.biCompression = m_outfmt;

  m_vi.bmiHeader.biPlanes = 1;

  /* Check if we support the desidered output format */
  if (!SetOutputFormat(&m_vi.bmiHeader.biBitCount, &m_vi.bmiHeader.biPlanes))
    return DSN_OUTPUT_NOTSUPPORTED;

  m_vi2.bmiHeader.biBitCount = m_vi.bmiHeader.biBitCount;
  m_vi2.bmiHeader.biPlanes = m_vi.bmiHeader.biPlanes;

  RECT rImg = { 0 /* left */, 0 /* top */, m_bih->biWidth /* right */, m_bih->biHeight /* bottom */};
  m_vi.rcSource = m_vi2.rcSource = m_vi.rcTarget = m_vi2.rcTarget = rImg;

  //m_vi2.bmiHeader.biHeight *= -1;

  m_vi.bmiHeader.biSizeImage = m_pDestType.lSampleSize = labs(m_bih->biWidth * m_bih->biHeight * ((m_vi.bmiHeader.biBitCount + 7) / 8));
  m_vi2.bmiHeader.biSizeImage = m_vi.bmiHeader.biSizeImage;

  // try FORMAT_VideoInfo
  m_pDestType.formattype = FORMAT_VideoInfo;
  m_pDestType.cbFormat = sizeof(m_vi);
  m_pDestType.pbFormat = (BYTE *) &m_vi;
  m_res = m_pOutputPin->QueryAccept(&m_pDestType);

  // try FORMAT_VideoInfo2
  if (m_res != S_OK)
  {
    m_pDestType.formattype = FORMAT_VideoInfo2;
    m_pDestType.cbFormat = sizeof(m_vi2);
    m_pDestType.pbFormat = (BYTE *) &m_vi2;
    m_res = m_pOutputPin->QueryAccept(&m_pDestType);
  }
  return (m_res == S_OK) ? DSN_OK : DSN_OUTPUT_NOTACCEPTED;
}

BOOL DSVideoCodec::isAVC(DWORD biCompression)
{
  switch (biCompression)
  {
    case mmioFOURCC('H', '2', '6', '4'):
    case mmioFOURCC('h', '2', '6', '4'):
    case mmioFOURCC('X', '2', '6', '4'):
    case mmioFOURCC('x', '2', '6', '4'):
    case mmioFOURCC('A', 'V', 'C', '1'):
    case mmioFOURCC('a', 'v', 'c', '1'):
    case mmioFOURCC('d', 'a', 'v', 'c'):
    case mmioFOURCC('D', 'A', 'V', 'C'):
    case mmioFOURCC('V', 'S', 'S', 'H'):
      return TRUE;
  }
  return FALSE;
}

// Helper function to get the next number of bits from the buffer
// Supports reading 0 to 64 bits.
UINT64 next_bits(BYTE *buf, int nBits)
{
  ASSERT(nBits >= 0 && nBits <= 64);

  UINT64 bitbuf = 0;

  int bitlen = 0;
  for (; bitlen < nBits; bitlen += 8)
  {
    bitbuf <<= 8;
    bitbuf |= *buf++;
  }
  UINT64 ret = (bitbuf >> (bitlen - nBits)) & ((1ui64 << nBits) - 1);

  return ret;
}

DWORD avc_parse_annexb(BYTE *src, BYTE *dst, int extralen)
{
  BYTE *endpos = src + extralen;
  BYTE *spspos = 0, *ppspos = 0;
  UINT16 spslen = 0, ppslen = 0;

  BYTE *p = src;

  // ISO/IEC 14496-10:2004 Annex B Byte stream format
  // skip any trailing bytes until we find a header
  while(p < (endpos-4) && next_bits(p, 24) != 0x000001 && 
    next_bits(p, 32) != 0x00000001)
  {
    // skip one
    p++;
  }

  // Repeat while:
  //    We're not at the end of the stream
  //    We're at a section start
  //    We still need SPS or PPS
  while(p < (endpos-4) && (next_bits(p, 24) == 0x000001 || next_bits(p, 32) == 0x00000001) && (!spspos || !ppspos))
  {
    // Skip the bytestream nal header
    if (next_bits(p, 32) == 0x000001)
      p++;
    
    p += 3;

    // first byte in the nal unit and their bit-width:
    //    zero bit  (1)
    //    ref_idc   (2)
    //    unit_type (5)
    BYTE ref_idc = *p & 0x60;
    BYTE unit_type = *p & 0x1f;
    // unit types lookup table, figure 7-1, chapter 7.4.1
    if (unit_type == 7 && ref_idc != 0) // Sequence parameter set
    {
      spspos = p;
    }
    else if (unit_type == 8 && ref_idc != 0) 
    { // Picture parameter set
      ppspos = p;
    }

    // go to end of block
    while(1) 
    {
      // either we find another NAL unit block, or the end of the stream
      if((p < (endpos-4) && (next_bits(p, 24) == 0x000001 || next_bits(p, 32) == 0x00000001))
        || (p == endpos)) 
      {
          break;
      } 
      else 
      {
        p++;
      }
    }
    // if a position is set, but doesnt have a length yet, its just been discovered
    // (or something went wrong)
    if(spspos && !spslen) 
    {
      spslen = (UINT16)(p - spspos);
    } 
    else if (ppspos && !ppslen) 
    {
      ppslen = (UINT16)(p - ppspos);
    }
  }

  // if we can't parse the header, we just don't do anything with it
  // Alternative: copy it as-is, without parsing?
  if (!spspos || !spslen || !ppspos || !ppslen)
    return 0;

  // Keep marker for length calcs
  BYTE *dstmarker = dst;

  // The final extradata format is quite simple
  //  A 16-bit size value of the sections, followed by the actual section data

  // copy SPS over
  *dst++ = spslen >> 8;
  *dst++ = spslen & 0xff;
  memcpy(dst, spspos, spslen);
  dst += spslen;

  // and PPS
  *dst++ = ppslen >> 8;
  *dst++ = ppslen & 0xff;
  memcpy(dst, ppspos, ppslen);
  dst += ppslen;

  return (DWORD)(dst - dstmarker);
}
BOOL DSVideoCodec::SetInputType()
{
  ULONG cbFormat;

  m_pOurType.majortype = MEDIATYPE_Video;
  m_pOurType.subtype = MEDIATYPE_Video;
  m_pOurType.bFixedSizeSamples = FALSE;
  m_pOurType.bTemporalCompression = TRUE;
  m_pOurType.lSampleSize = 1;
  m_pOurType.pUnk = NULL;

  /* ffdshow (and others ?) needs AVC1 as fourcc for avc video */
  if (isAVC(m_bih->biCompression))
    m_pOurType.subtype.Data1 = mmioFOURCC('A', 'V', 'C', '1');
  else
    m_pOurType.subtype.Data1 = m_bih->biCompression;

  // probe FORMAT_MPEG2Video
  // this is done before FORMAT_VideoInfo because e.g. coreavc will accept anyway the format
  // but it will decode black frames
  
  int extra = m_bih->biSize - sizeof(BITMAPINFOHEADER);
  cbFormat = FIELD_OFFSET(MPEG2VIDEOINFO, dwSequenceHeader) + extra;

  MPEG2VIDEOINFO *try_mp2vi = m_pCallback->GetMPEG2VIDEOINFO();//(MPEG2VIDEOINFO *) new BYTE[cbFormat];
  if (!try_mp2vi)
  {
    cbFormat = FIELD_OFFSET(MPEG2VIDEOINFO, dwSequenceHeader) + extra - 7;
    try_mp2vi = (MPEG2VIDEOINFO*) new BYTE[cbFormat];
  
    m_pOurType.formattype = FORMAT_MPEG2Video;
  
    m_pOurType.cbFormat = cbFormat;
    try_mp2vi->hdr.bmiHeader.biCompression = m_pOurType.subtype.Data1;
    m_vinfo = (BYTE *) try_mp2vi;
    
    memset(try_mp2vi, 0, cbFormat);
    m_pOurType.pbFormat = m_vinfo;
    try_mp2vi->hdr.rcSource.left = try_mp2vi->hdr.rcSource.top = 0;
    try_mp2vi->hdr.rcSource.right = m_bih->biWidth;
    try_mp2vi->hdr.rcSource.bottom = m_bih->biHeight;
    try_mp2vi->hdr.rcTarget = try_mp2vi->hdr.rcSource;
    try_mp2vi->hdr.dwPictAspectRatioX = m_bih->biWidth;
    try_mp2vi->hdr.dwPictAspectRatioY = m_bih->biHeight;
    memcpy(&try_mp2vi->hdr.bmiHeader, m_bih, sizeof(BITMAPINFOHEADER));
    try_mp2vi->hdr.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    try_mp2vi->hdr.bmiHeader.biCompression = m_pOurType.subtype.Data1;
    try_mp2vi->hdr.AvgTimePerFrame = m_frametime;
  }

  /* From MPC-HC */
  int trying = 0;
tryingotherway:
  if ((extra > 0)&& (trying == 0))
  {
    BYTE *extradata = (BYTE *) m_bih + sizeof(BITMAPINFOHEADER);
    try_mp2vi->dwProfile = extradata[1];
    try_mp2vi->dwLevel = extradata[3];
    //We are not currently setting this and even if we were setting it its not always working

      try_mp2vi->dwFlags = (extradata[4] & 3) + 1;

    try_mp2vi->cbSequenceHeader = 0;

    BYTE* src = (BYTE *) extradata + 5;
    BYTE* dst = (BYTE *) try_mp2vi->dwSequenceHeader;

    BYTE* src_end = (BYTE *) extradata + extra;
    BYTE* dst_end = (BYTE *) try_mp2vi->dwSequenceHeader + extra;

    for (int i = 0; i < 2; i++)
    {
      for (int n = *src++ & 0x1f; n > 0; n--)
      {
        int len = ((src[0] << 8) | src[1]) + 2;
        if(src + len > src_end || dst + len > dst_end) 
        { 
          trying++;
          goto tryingotherway;
        }
        memcpy(dst, src, len);
        src += len;
        dst += len;
        try_mp2vi->cbSequenceHeader += len;
      }
    }
  }
  else if (trying == 1)
  {
    //Usually when it fail the first we can try this way(usually for mpeg-ts)
    try_mp2vi->dwFlags = 4;
    BYTE *extradata = (BYTE*)malloc(extra);
    
    try_mp2vi->cbSequenceHeader = avc_parse_annexb(extradata, (BYTE *)(&try_mp2vi->dwSequenceHeader[0]), extra);
    try_mp2vi->hdr.bmiHeader.biCompression = 0x31637661;
  }

  m_pOurType.formattype = FORMAT_MPEG2Video;
  m_pOurType.pbFormat = m_vinfo;
  m_pOurType.cbFormat = cbFormat;

  if ((m_res = m_pInputPin->QueryAccept(&m_pOurType)) == S_OK)
    return TRUE;

  delete m_vinfo;
  m_vinfo = NULL;

  // probe FORMAT_VideoInfo
        cbFormat = sizeof(VIDEOINFOHEADER) + m_bih->biSize;
  VIDEOINFOHEADER *try_vi = (VIDEOINFOHEADER *) new BYTE[cbFormat];
  m_vinfo = (BYTE *) try_vi;

  memset(try_vi, 0, cbFormat);
  memcpy(&try_vi->bmiHeader, m_bih, m_bih->biSize);

  try_vi->rcSource.left = try_vi->rcSource.top = 0;
  try_vi->rcSource.right = m_bih->biWidth;
  try_vi->rcSource.bottom = m_bih->biHeight;
  try_vi->rcTarget = try_vi->rcSource;
  try_vi->AvgTimePerFrame = m_frametime;

  m_pOurType.formattype = FORMAT_VideoInfo;
  m_pOurType.pbFormat = m_vinfo;
  m_pOurType.cbFormat = cbFormat;

  if ((m_res = m_pInputPin->QueryAccept(&m_pOurType)) == S_OK)
    return TRUE;

  delete m_vinfo;
  m_vinfo = NULL;
  return FALSE;
}

BOOL DSVideoCodec::EnumPins()
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
  
  if (m_pEvrFilter->EnumPins(&enumpins) != S_OK)
    return FALSE;

  enumpins->Reset();

  while ((m_res = enumpins->Next(1, &pin, NULL)) == S_OK)
  {
    pin->QueryPinInfo(&pInfo);
    if (!m_pEvrInputPin && (pInfo.dir == PINDIR_INPUT))
      m_pEvrInputPin = pin;

    pin->Release();
    m_pEvrFilter->Release();
  }

  enumpins->Release();
  return TRUE;
}

dsnerror_t DSVideoCodec::SetupAllocator()
{
  DSN_CHECK(m_pMemInputPin->GetAllocator(&m_pMemAllocator), DSN_FAIL_ALLOCATOR);
  ALLOCATOR_PROPERTIES props, props1;
  props.cBuffers = 1;
  props.cbBuffer = m_pDestType.lSampleSize;
  props.cbAlign = 1;
  props.cbPrefix = 0;
  DSN_CHECK(m_pMemAllocator->SetProperties(&props, &props1), DSN_FAIL_ALLOCATOR);
  DSN_CHECK(m_pMemInputPin->NotifyAllocator(m_pMemAllocator, FALSE), DSN_FAIL_ALLOCATOR);
  DSN_CHECK(m_pMemAllocator->Commit(), DSN_FAIL_ALLOCATOR);
  return DSN_OK;
}
dsnerror_t DSVideoCodec::ResizeAllocatorProperties(int size)
{
  ALLOCATOR_PROPERTIES props, actual;
  DSN_CHECK(m_pMemAllocator->GetProperties(&props),DSN_FAIL_ALLOCATOR);
  props.cbBuffer = size*3/2;

  DSN_CHECK(m_pMemAllocator->Decommit(),DSN_FAIL_ALLOCATOR);
  DSN_CHECK(m_pMemAllocator->SetProperties(&props, &actual),DSN_FAIL_ALLOCATOR);
  DSN_CHECK(m_pMemAllocator->Commit(),DSN_FAIL_ALLOCATOR);
  return DSN_OK;
}
dsnerror_t DSVideoCodec::CreateGraph(bool buildgraph)
{
  if (!EnumPins())
    return DSN_FAIL_ENUM;

  if (!SetInputType())
    return DSN_INPUT_NOTACCEPTED;

  m_pSFilter = new CSenderFilter();
  m_pOurInput = (CSenderPin *) m_pSFilter->GetPin(0);
  /* setup Source filename if someone wants to known it (i.e. ffdshow) */
  m_pSFilter->Load(m_sfname, NULL);
  m_pSFilter->AddRef();

  m_pRFilter = new CRenderFilter();
  m_pOurOutput = (CRenderPin *) m_pRFilter->GetPin(0);
  m_pRFilter->AddRef();
  /*m_pOurOutput*/
  /*The ou */
  if (buildgraph)
  {
    DSN_CHECK(CoCreateInstance(CLSID_FilterGraph, NULL, CLSCTX_INPROC_SERVER, IID_IGraphBuilder, (void **) &m_pGraph), DSN_FAIL_GRAPH);
    DSN_CHECK(DSVideoCodec::AddToRot(m_pGraph, &m_dwRegister), DSN_FAIL_GRAPH);
    DSN_CHECK(m_pGraph->QueryInterface(IID_IMediaControl, (void **) &m_pMC), DSN_FAIL_GRAPH);

    m_pGraph->SetLogFile((DWORD_PTR) GetStdHandle(STD_OUTPUT_HANDLE));
    DSN_CHECK(m_pGraph->AddFilter(m_pFilter, L"Binary Codec"), (m_pInputPin = m_pOutputPin = NULL, DSN_FAIL_GRAPH));
    DSN_CHECK(m_pGraph->AddFilter(m_pSFilter, L"DS Sender"), DSN_FAIL_GRAPH);
    //DSN_CHECK(m_pGraph->AddFilter(m_pRFilter, L"DS Render"), DSN_FAIL_GRAPH);
    m_res = m_pGraph->AddFilter(m_pEvrFilter, L"DS Render");
    // Connect our output pin to codec input pin otherwise QueryAccept on the codec output pin will fail
    DSN_CHECK(m_pGraph->ConnectDirect(m_pOurInput, m_pInputPin, &m_pOurType), DSN_INPUT_CONNFAILED);
  }
  else
  {
    m_res = m_pFilter->JoinFilterGraph((IFilterGraph *) m_pSFilter, L"DSNative Graph");
    /* same of above */
    DSN_CHECK(m_pInputPin->ReceiveConnection(m_pOurInput, &m_pOurType), DSN_INPUT_CONNFAILED);
  }

  SetOutputType();

  
#if 1
  DSN_CHECK(m_pGraph->ConnectDirect(m_pOutputPin, m_pEvrInputPin, NULL), DSN_OUTPUT_CONNFAILED);
#endif
#if 0
  m_res = m_pEvrFilter->JoinFilterGraph((IFilterGraph *) m_pSFilter, L"Evr");
  m_res = m_pOutputPin->Connect(m_pEvrInputPin,NULL);
/*#if 0
  /*if (buildgraph)
    DSN_CHECK(m_pGraph->ConnectDirect(m_pOurOutput, m_pOutputPin, &m_pDestType), DSN_OUTPUT_CONNFAILED);
  else
    DSN_CHECK(m_pOutputPin->ReceiveConnection(m_pOurOutput, &m_pDestType), DSN_OUTPUT_CONNFAILED);*/
#endif
  if (m_pEvr)
  {
    CMediaType mt;
    if (SUCCEEDED(m_pEvrInputPin->ConnectionMediaType(&mt)))
    {
      m_pEvr->SetDSMediaType(mt);
    }
  }
  m_pOurOutput->SetFrameSize(m_vi.bmiHeader.biBitCount * m_vi.bmiHeader.biWidth * (m_vi.bmiHeader.biHeight + 2) / 8);
  return DSN_OK;
}

HRESULT DSVideoCodec::AddToRot(IUnknown *pUnkGraph, DWORD *pdwRegister)
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


void DSVideoCodec::RemoveFromRot(DWORD pdwRegister)
{
  IRunningObjectTable *pROT;
  if (SUCCEEDED(GetRunningObjectTable(0, &pROT)))
  {
    pROT->Revoke(pdwRegister);
    pROT->Release();
  }
}

BOOL DSVideoCodec::StartGraph()
{
  SetupAllocator();
  if (m_pMC) 
    m_pMC->Run();
  else 
    m_pFilter->Run(0);
  Resync(0); // NewSegment + discontinuity /* e.g. ffdshow will not init byte count */
  return TRUE;
}


dsnerror_t DSVideoCodec::Decode(BYTE *src, int size, double pts, double *newpts, DSVideoOutputData *pImage, long *pImageSize, int keyframe)
{
  IMediaSample* sample = NULL;
  //the index is calculated on seek
  REFERENCE_TIME start2 = m_frametime * m_currentframeindex;
  m_currentframeindex++;
  REFERENCE_TIME start =pts;
   
  if (start < 0)
    start = 0;
  REFERENCE_TIME stoptime = start + m_frametime;
  BYTE *ptr;
  
  if (m_pEvr)
  {
    if (!m_pEvr->AcceptMoreData())
      m_pEvr->FreeFirstBuffer();
  }

  DSN_CHECK(m_pMemAllocator->GetBuffer(&sample, 0, 0, 0), DSN_FAIL_DECODESAMPLE);
  HRESULT hr = S_OK;
  //
  std::vector<BYTE> packet;
  
  int64_t startperf = GetPerfCounter();
  //ffmpeg m2ts demuxer set h264 tag, good thing its the only one i seen setting it like this
  if ( m_pCallback->GetOriginalCodecTag() == 0x1b)//if (m_pOurType.subtype == FOURCCMap('1CVA') || m_pOurType.subtype == FOURCCMap('1cva'))
  {
    
    DSN_CHECK(sample->SetActualDataLength(size), DSN_FAIL_DECODESAMPLE);
    DSN_CHECK(sample->GetPointer(&ptr), DSN_FAIL_DECODESAMPLE);
    memcpy(ptr , src , size);
    
#if 0
    //fixing 1cva this has to be starting nalus, more can be packed together
    BYTE* start = src;
    BYTE* end = start + size;

    while(start <= end-4 && *(DWORD*)start != 0x01000000)
      start++;
    
    while(start <= end-4)
    {
      BYTE* next = start+1;
      while(next <= end-4 && *(DWORD*)next != 0x01000000)
        next++;

      if(next >= end-4)
        break;

      int nalusize = next - start;
      int current_pos = start - src;
      
      CH264Nalu Nalu;
      Nalu.SetBuffer (start, nalusize, 0);
      

      
      while (Nalu.ReadNext())
      {
        DWORD  dwNalLength =
          ((Nalu.GetDataLength() >> 24) & 0x000000ff) |
          ((Nalu.GetDataLength() >>  8) & 0x0000ff00) |
          ((Nalu.GetDataLength() <<  8) & 0x00ff0000) |
          ((Nalu.GetDataLength() << 24) & 0xff000000);
        //ptr[current_pos++] = (Nalu.GetDataLength() >> 24) & 0x000000ff;
        //ptr[current_pos++] = (Nalu.GetDataLength() >>  8) & 0x0000ff00;
        //ptr[current_pos++] = (Nalu.GetDataLength() <<  8) & 0x00ff0000;
        //ptr[current_pos++] = (Nalu.GetDataLength() << 24) & 0xff000000;
        memcpy (ptr + current_pos , &dwNalLength, sizeof(dwNalLength));
        //memcpy (ptr + current_pos + sizeof(dwNalLength), Nalu.GetDataBuffer(), Nalu.GetDataLength());
        
      }
      start = next;


    }

    

    int current_pos = start - src;
    int nalsize = size-current_pos-4;
    CH264Nalu Nalu;
    Nalu.SetBuffer (start, nalsize, 0);
    if (Nalu.ReadNext())
    {
      DWORD  dwNalLength =
          ((Nalu.GetDataLength() >> 24) & 0x000000ff) |
          ((Nalu.GetDataLength() >>  8) & 0x0000ff00) |
          ((Nalu.GetDataLength() <<  8) & 0x00ff0000) |
          ((Nalu.GetDataLength() << 24) & 0xff000000);
    memcpy(ptr+current_pos, &dwNalLength, sizeof(dwNalLength));
     memcpy (ptr + current_pos + sizeof(dwNalLength), Nalu.GetDataBuffer(), Nalu.GetDataLength());
    
    }
    else
    {
    DWORD  dwNalLength =
          ((nalsize >> 24) & 0x000000ff) |
          ((nalsize >>  8) & 0x0000ff00) |
          ((nalsize <<  8) & 0x00ff0000) |
          ((nalsize << 24) & 0xff000000);
    memcpy(ptr+current_pos, &dwNalLength, sizeof(dwNalLength));
    
    }
    CStdStringA dbg;
    CH264Nalu nalu;
    nalu.SetBuffer (src, size, 0);
    while (nalu.ReadNext()) {
    
    	switch (nalu.GetType())
	    {
	      case NALU_TYPE_SLICE:
		        dbg.AppendFormat("SliceType: NALU_TYPE_SLICE %i ",nalu.GetLength());
				break;
	      case NALU_TYPE_DPA:
		        dbg.AppendFormat("SliceType: NALU_TYPE_DPA %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_DPB:
		        dbg.AppendFormat("SliceType: NALU_TYPE_DPB %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_DPC:
		        dbg.AppendFormat("SliceType: NALU_TYPE_DPC %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_IDR:
		        dbg.AppendFormat("SliceType: NALU_TYPE_IDR %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_SEI:
		        dbg.AppendFormat("SliceType: NALU_TYPE_SEI %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_SPS:
		        dbg.AppendFormat("SliceType: NALU_TYPE_SPS %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_PPS:
		        dbg.AppendFormat("SliceType: NALU_TYPE_PPS %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_AUD:
		        dbg.AppendFormat("SliceType: NALU_TYPE_AUD %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_EOSEQ:
		        dbg.AppendFormat("SliceType: NALU_TYPE_EOSEQ %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_EOSTREAM:
		        dbg.AppendFormat("SliceType: NALU_TYPE_EOSTREAM %i ",nalu.GetLength());
		        break;
	      case NALU_TYPE_FILL:
		        dbg.AppendFormat("SliceType: NALU_TYPE_FILL %i ",nalu.GetLength());
		        break;
      }
    }
    /*CLog::Log(LOGINFO,"without modif %i",size);
    CStdStringA xtravectstring1;
    for (int i = 0;i < size ;i++)
      xtravectstring1.AppendFormat("%02X ",src[i]);
    CLog::Log(LOGINFO,"%s",xtravectstring1.c_str());*/

    /*CStdStringA xtravectstring;
    for (int i = 0; i < size ; i++)
      xtravectstring.AppendFormat("%02X ",ptr[i]);
    CLog::Log(LOGINFO,"%s",xtravectstring.c_str());*/
    CLog::Log(LOGDEBUG,"DXVADECODERH264:%i",size);
    CLog::Log(LOGDEBUG,"%s",dbg.c_str());
    
#endif
      
      
  }
  else
  {
    if (size > sample->GetSize())
    {
      DSN_CHECK(sample->Release(),DSN_FAIL_DECODESAMPLE);
      dsnerror_t res = ResizeAllocatorProperties(size);
      hr = m_pFilter->Run(0);
    
      DSN_CHECK(m_pMemAllocator->GetBuffer(&sample, 0, 0, 0), DSN_FAIL_DECODESAMPLE);
    }
    DSN_CHECK(sample->SetActualDataLength(size), DSN_FAIL_DECODESAMPLE);
    DSN_CHECK(sample->GetPointer(&ptr), DSN_FAIL_DECODESAMPLE);
    memcpy(ptr, src, size);
  }
  //m2ts due to the requirements of many codecs that need starting nalu at end the fastest is around 36 and 12 without!
  int64_t endperf = GetPerfCounter();
  int64_t resultperf = endperf-startperf;
  if (resultperf < m_perfTest.fastest)
    m_perfTest.fastest = resultperf;
  if (resultperf > m_perfTest.longest)
    m_perfTest.longest = resultperf;

  DSN_CHECK(sample->SetTime(&start, &stoptime), DSN_FAIL_DECODESAMPLE);
  DSN_CHECK(sample->SetSyncPoint(keyframe), DSN_FAIL_DECODESAMPLE);
  DSN_CHECK(sample->SetPreroll(pImage ? 0 : 1), DSN_FAIL_DECODESAMPLE);
  DSN_CHECK(sample->SetDiscontinuity(m_discontinuity), DSN_FAIL_DECODESAMPLE);

  m_discontinuity = 0;
  if (pImage->format == DSVideoOutputData::FMT_DS_YV12)
  {
    m_pOurOutput->SetPointer(pImage->data);
    hr = m_pMemInputPin->Receive(sample);
    if (FAILED(hr))
    {
      char szErr[MAX_ERROR_TEXT_LEN];
      DWORD res = AMGetErrorTextA(hr, szErr, MAX_ERROR_TEXT_LEN);
      if (res == 0)
        StringCchPrintfA(szErr, MAX_ERROR_TEXT_LEN, "Unknown Error: 0x%2x", hr);
      OutputDebugStringA(szErr);
      hr = m_pMemInputPin->ReceiveCanBlock();
      if (hr == S_OK)
      {
        REFERENCE_TIME runStart = PTS2RT(pts);
        hr = m_pSFilter->Run(runStart);
        hr = m_pFilter->Run(runStart);
        hr = m_pEvrFilter->Run(runStart);
      }
      
    }
    //DSN_CHECK(m_pMemInputPin->Receive(sample), DSN_FAIL_RECEIVE);
    sample->Release();
  
    *pImageSize = m_pOurOutput->GetPointerSize();
    *newpts = RT2PTS(m_pOurOutput->GetPTS());
    return DSN_OK;
  }
  else if (pImage->format == DSVideoOutputData::FMT_EVR)
  {
    
    hr = m_pMemInputPin->Receive(sample);
    if (FAILED(hr))
    {
      char szErr[MAX_ERROR_TEXT_LEN];
      DWORD res = AMGetErrorTextA(hr, szErr, MAX_ERROR_TEXT_LEN);
      if (res == 0)
        StringCchPrintfA(szErr, MAX_ERROR_TEXT_LEN, "Unknown Error: 0x%2x", hr);
      OutputDebugStringA(szErr);
      hr = m_pMemInputPin->ReceiveCanBlock();
      if (hr == S_OK)
      {
        REFERENCE_TIME runStart = PTS2RT(pts);
        hr = m_pSFilter->Run(runStart);
        hr = m_pFilter->Run(runStart);
        hr = m_pEvrFilter->Run(runStart);
      }
      
    }
    //DSN_CHECK(m_pMemInputPin->Receive(sample), DSN_FAIL_RECEIVE);
    sample->Release();
  
    *pImageSize = m_pOurOutput->GetPointerSize();
    *newpts = RT2PTS(m_pOurOutput->GetPTS());
    
    return DSN_OK;
  }
  //Verify its not going to dead lock before sending the sample
  //Sounds like no one give a fuck about the msdn documentation
  /*if (m_pMemInputPin->ReceiveCanBlock() != S_OK)*/
  /*at least with the evr we can call imftransform on the mixer to see if the input is full before sending it*/
  if (!m_pEvr->AcceptMoreData())
    return DSN_DATA_QUEUE_FULL;
  
}
IPaintCallback* DSVideoCodec::GetEvrCallback()
{
  return m_pEvr->AcquireCallback();
}

dsnerror_t DSVideoCodec::DSVideoGetMediaBuffer(DsAllocator* pAlloc)
{
  
  DWORD wait = 10;
  
  //pAlloc = m_pEvr->Acquire();
  //m_pEvr->GetNextSurface(&pBuffer,&wait);

  
  
  if (pAlloc)
    return DSN_OK;
  
  return DSN_FAIL_RECEIVE;

}

dsnerror_t DSVideoCodec::Resync(REFERENCE_TIME pts)
{
  //newsegment(start,stop,rate) stop is the duration of the video in a nanosecond time base
  REFERENCE_TIME currenttime = PTS2RT(m_pCallback->GetTime());
  REFERENCE_TIME duration = PTS2RT(m_pCallback->GetTotalTime());
  m_currentframeindex = std::floor((long double)(currenttime / m_frametime));

  m_res = m_pInputPin->NewSegment(currenttime, duration, 1.0);
  m_res = m_pEvrInputPin->NewSegment(currenttime, duration, 1.0);
  m_discontinuity = 1;
  return DSN_OK;
}

class CMyPropertyPageSite : public IPropertyPageSite, CUnknown
{
public:
  CMyPropertyPageSite(void) 
  : CUnknown(NAME("CMyPropertyPageSite"),NULL)
  {
  }
  DECLARE_IUNKNOWN
  STDMETHODIMP NonDelegatingQueryInterface(REFIID riid,void **ppv)
  {
    if (riid==IID_IPropertyPageSite)
      *ppv = (IPropertyPageSite *)this;
  
  return __super::NonDelegatingQueryInterface(riid,ppv);
  }
  virtual HRESULT STDMETHODCALLTYPE OnStatusChange(DWORD dwFlags) { return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE GetLocaleID(LCID *pLocaleID) {*pLocaleID = 0; return S_OK; }
  virtual HRESULT STDMETHODCALLTYPE GetPageContainer(IUnknown **ppUnk){*ppUnk = NULL; return E_NOTIMPL; }
  virtual HRESULT STDMETHODCALLTYPE TranslateAccelerator(MSG *pMsg){pMsg->message = NULL; return E_NOTIMPL;}
};

BOOL DSVideoCodec::ShowPropertyPage()
{
  if (!m_pFilter) 
    return FALSE;
  IPropertyPageSite *pSite = NULL;
  IUnknown *pUnk = NULL;
  IPropertyPage *pPage = NULL;
  RECT rect = { 0, 0, 1, 1, };
  HWND hDlg = NULL, hButton = NULL;
  
  
  ISpecifyPropertyPages *pProp;
  if ((m_res = m_pFilter->QueryInterface(IID_ISpecifyPropertyPages, (LPVOID *) &pProp)) == S_OK)
  {
    // Get the filter's name and IUnknown pointer.
    FILTER_INFO FilterInfo;
    m_res = m_pFilter->QueryFilterInfo(&FilterInfo);
    IUnknown *pFilterUnk;
    m_res = m_pFilter->QueryInterface(IID_IUnknown, (void **) &pFilterUnk);
    CAUUID caGUID;
    pProp->GetPages(&caGUID);
    pProp->Release();
    CoCreateInstance(caGUID.pElems[0], NULL, CLSCTX_INPROC_SERVER, IID_IPropertyPage, (void **)&pPage);
  }
  PROPPAGEINFO pInfo;
  pPage->GetPageInfo(&pInfo);
  
  //create a window to parse button in that page
  HWND hWnd = CreateWindowW(L"", L"", WS_EX_NOACTIVATE ,1 ,1 ,1 ,1 , (HWND) NULL ,(HMENU) NULL , (HINSTANCE) NULL ,NULL);
  
  m_pFilter->QueryInterface(IID_IUnknown, (void **) &pUnk);
  
  pSite = (IPropertyPageSite *)new CMyPropertyPageSite();
  pPage->SetPageSite(pSite);
  pPage->SetObjects(1,&pUnk);
  
  
  pPage->Activate(hWnd,&rect,FALSE);
  struct prop_control
  {
    HWND hwnd;
    int control_id;
    CStdStringW text;
  };

  std::vector<prop_control*> systreeview32;
  std::vector<prop_control*> statictext32;
  std::vector<prop_control*> button32;
  std::vector<prop_control*> edit32;
  
  prop_control* pctrl;
  pctrl = (prop_control*)malloc(sizeof(prop_control*));
  while(hDlg = FindWindowExA(hWnd, hDlg, NULL, NULL))
  {
    
    if(hButton = FindWindowExA(hDlg,NULL,"SysTreeView32",NULL))
    {
      pctrl->hwnd = hButton;
      pctrl->control_id = GetDlgCtrlID(hButton);
      CStdStringW pstringw;
      pstringw.resize(255);
      UINT ctrlsize = GetDlgItemText(hDlg, pctrl->control_id, &pstringw.at(0),255);
      if (ctrlsize>0)
        pctrl->text = pstringw;
      systreeview32.push_back(pctrl);

    }
    if(hButton = FindWindowExA(hDlg,NULL,"Button",NULL))
    {
      pctrl->hwnd = hButton;
      pctrl->control_id = GetDlgCtrlID(hButton);
      CStdStringW pstringw;
      pstringw.resize(255);
      UINT ctrlsize = GetDlgItemText(hDlg, pctrl->control_id, &pstringw.at(0),255);
      if (ctrlsize>0)
        pctrl->text = pstringw;

      button32.push_back(pctrl);
    }
    if(hButton = FindWindowExA(hDlg,NULL,"Static",NULL))
    {
      pctrl->hwnd = hButton;
      pctrl->control_id = GetDlgCtrlID(hButton);
      CStdStringW pstringw;
      pstringw.resize(255);
      UINT ctrlsize = GetDlgItemText(hDlg, pctrl->control_id, &pstringw.at(0),255);
      if (ctrlsize>0)
        pctrl->text = pstringw;
      statictext32.push_back(pctrl);
    }
    if(hButton = FindWindowExA(hDlg,NULL,"Edit",NULL))
    {
      pctrl->hwnd = hButton;
      pctrl->control_id = GetDlgCtrlID(hButton);
      CStdStringW pstringw;
      pstringw.resize(255);
      UINT ctrlsize = GetDlgItemText(hDlg, pctrl->control_id, &pstringw.at(0),255);
      if (ctrlsize>0)
        pctrl->text = pstringw;
      edit32.push_back(pctrl);
      
    }
    /* GUICONTROL_UNKNOWN,
    GUICONTROL_BUTTON,
    GUICONTROL_CHECKMARK,
    GUICONTROL_FADELABEL,
    GUICONTROL_IMAGE,
    GUICONTROL_BORDEREDIMAGE,
    GUICONTROL_LARGE_IMAGE,
    GUICONTROL_LABEL,
    GUICONTROL_LISTGROUP,
    GUICONTROL_PROGRESS,
    GUICONTROL_RADIO,
    GUICONTROL_RSS,
    GUICONTROL_SELECTBUTTON,
    GUICONTROL_SLIDER,
    GUICONTROL_SETTINGS_SLIDER,
    GUICONTROL_SPIN,
    GUICONTROL_SPINEX,
    GUICONTROL_TEXTBOX,
    GUICONTROL_TOGGLEBUTTON,
    GUICONTROL_VIDEO,
    GUICONTROL_MOVER,
    GUICONTROL_RESIZE,
    GUICONTROL_BUTTONBAR,
    GUICONTROL_EDIT,
    GUICONTROL_VISUALISATION,
    GUICONTROL_RENDERADDON,
    GUICONTROL_MULTI_IMAGE,
    GUICONTROL_GROUP,
    GUICONTROL_GROUPLIST,
    GUICONTROL_SCROLLBAR,
    GUICONTROL_LISTLABEL,
    GUICONTROL_MULTISELECT,
    GUICONTAINER_LIST,
    GUICONTAINER_WRAPLIST,
    GUICONTAINER_FIXEDLIST,
    GUICONTAINER_PANEL
    */
    
  }

  pPage->Apply();

  pPage->Deactivate();
  pPage->SetObjects(0,NULL);
  pPage->SetPageSite(NULL);
  pUnk->Release();
  pPage->Release();
  pSite->Release();
  return 1;
#if 0
  ISpecifyPropertyPages *pProp;
  if ((m_res = m_pFilter->QueryInterface(IID_ISpecifyPropertyPages, (LPVOID *) &pProp)) == S_OK)
  {
    // Get the filter's name and IUnknown pointer.
    FILTER_INFO FilterInfo;
    m_res = m_pFilter->QueryFilterInfo(&FilterInfo);
    IUnknown *pFilterUnk;
    m_res = m_pFilter->QueryInterface(IID_IUnknown, (void **) &pFilterUnk);
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
#endif
}

BOOL DSVideoCodec::SetOutputFormat(WORD *biBitCount, WORD *biPlanes)
{
  switch (m_outfmt)
  {
  // YUV
    case mmioFOURCC('Y', 'U', 'Y', '2'):
      m_pDestType.subtype = MEDIASUBTYPE_YUY2;
      *biBitCount = 16;
      *biPlanes = 1;
      return TRUE;
    case mmioFOURCC('U', 'Y', 'V', 'Y'):
      m_pDestType.subtype = MEDIASUBTYPE_UYVY;
      *biBitCount = 16;
      *biPlanes = 1;
      return TRUE;
    case mmioFOURCC('Y', 'V', '1', '2'):
      m_pDestType.subtype = MEDIASUBTYPE_YV12;
      *biBitCount = 12;
      *biPlanes = 3;
      return TRUE;
    case mmioFOURCC('I', 'Y', 'U', 'V'):
      m_pDestType.subtype = MEDIASUBTYPE_IYUV;
      *biBitCount = 12;
      *biPlanes = 3;
      return TRUE;
    case mmioFOURCC('I', '4', '2', '0'):
      /* Missing MEDIASUBTYPE_I420 in headers */
      m_pDestType.subtype = MEDIATYPE_Video;
      m_pDestType.subtype.Data1 = mmioFOURCC('I', '4', '2', '0');
      *biBitCount = 12;
      *biPlanes = 3;
      return TRUE;
    case mmioFOURCC('Y', 'V', 'U', '9'):
      m_pDestType.subtype = MEDIASUBTYPE_YVU9;
      *biBitCount = 9;
      *biPlanes = 1;
      return TRUE;
    case mmioFOURCC('N', 'V', '1', '2'):
      m_pDestType.subtype = MEDIASUBTYPE_NV12;
      *biBitCount = 12;
      *biPlanes = 2;
      return TRUE;
  }

  /* RGB */
  unsigned int bits = m_outfmt & 0xff;
  unsigned int check = m_outfmt ^ bits;

  if ((check == mmioFOURCC(0, 'B', 'G', 'R')) || (check == mmioFOURCC(0, 'R', 'G', 'B')))
  {
    *biBitCount = bits;
    switch (bits)
    {
      case 15: m_pDestType.subtype = MEDIASUBTYPE_RGB555; return TRUE;
      case 16: m_pDestType.subtype = MEDIASUBTYPE_RGB565; return TRUE;
      case 24: m_pDestType.subtype = MEDIASUBTYPE_RGB24; return TRUE;
      case 32: m_pDestType.subtype = MEDIASUBTYPE_RGB32; return TRUE;
    }
  }
  /* fprintf(stderr, "Format not supported 0x%08x\n", m_outfmt); */
  return FALSE;
}

AM_MEDIA_TYPE DSVideoCodec::GetOutputMediaType()
{
  return m_pDestType;
}
