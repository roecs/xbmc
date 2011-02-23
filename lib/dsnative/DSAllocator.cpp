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

#define INITGUID
#include "stdafx.h"

#include "dsallocator.h"

#include <math.h>

#include <mfidl.h>
#include <Mfapi.h>

#include <evr9.h>
#include "StdString.h"
#include "SmartPtr.h"
#include <map>
#include "utils\TimeUtils.h"
#include "threads\Thread.h"
// === Helper functions
#define CheckHR(exp) {if(FAILED(hr = exp)) return hr;}
//#define PaintInternal() {assert(0);}
//evr
typedef HRESULT (__stdcall *FCT_MFCreateVideoSampleFromSurface)(IUnknown* pUnkSurface, IMFSample** ppSample);
typedef HRESULT (__stdcall *FCT_MFCreateDXSurfaceBuffer)(REFIID riid, IUnknown *punkSurface, BOOL fBottomUpWhenLinear, IMFMediaBuffer **ppBuffer);
  // AVRT.dll
typedef HANDLE  (__stdcall *PTR_AvSetMmThreadCharacteristicsW)(LPCWSTR TaskName, LPDWORD TaskIndex);
typedef BOOL  (__stdcall *PTR_AvSetMmThreadPriority)(HANDLE AvrtHandle, AVRT_PRIORITY Priority);
typedef BOOL  (__stdcall *PTR_AvRevertMmThreadCharacteristics)(HANDLE AvrtHandle);
//HRESULT MFCreateDXSurfaceBuffer(REFIID riid, IUnknown *punkSurface, BOOL fBottomUpWhenLinear, IMFMediaBuffer **ppBuffer);

FCT_MFCreateVideoSampleFromSurface ptrMFCreateVideoSampleFromSurface;
FCT_MFCreateDXSurfaceBuffer ptrMFCreateDXSurfaceBuffer;
PTR_AvSetMmThreadCharacteristicsW       pfAvSetMmThreadCharacteristicsW;
PTR_AvSetMmThreadPriority               pfAvSetMmThreadPriority;
PTR_AvRevertMmThreadCharacteristics     pfAvRevertMmThreadCharacteristics;
// Guid to tag IMFSample with DirectX surface index
static const GUID GUID_SURFACE_INDEX = { 0x30c8e9f6, 0x415, 0x4b81, { 0xa3, 0x15, 0x1, 0xa, 0xc6, 0xa9, 0xda, 0x19 } };

static int64_t GetPerfCounter()
{
  LARGE_INTEGER i64Ticks100ns;
  LARGE_INTEGER llPerfFrequency;

  QueryPerformanceFrequency (&llPerfFrequency);
  if (llPerfFrequency.QuadPart != 0)
  {
    QueryPerformanceCounter (&i64Ticks100ns);
    return llMulDiv(i64Ticks100ns.QuadPart, 10000000, llPerfFrequency.QuadPart, 0);
  }
  else
  {
    // ms to 100ns units
    return timeGetTime() * 10000; 
  }
}

void DebugPrint(const wchar_t *format, ... )
{
  CStdStringW strData;
  strData.reserve(16384);

  va_list va;
  va_start(va, format);
  strData.FormatV(format, va);
  va_end(va);
  
  OutputDebugString(strData.c_str());
  if( strData.Right(1) != L"\n" )
    OutputDebugString(L"\n");

}

#define TRACE_EVR(x) CLog::DebugLog(x)

#pragma pack(push, 1)
template<int texcoords>
struct MYD3DVERTEX
{
  float x, y, z, rhw;
  struct
  {
    float u, v;
  } t[texcoords];
};
template<>
struct MYD3DVERTEX<0> 
{
  float x, y, z, rhw; 
  DWORD Diffuse;
};
#pragma pack(pop)

template<int texcoords>
static void AdjustQuad(MYD3DVERTEX<texcoords>* v, double dx, double dy)
{
  double offset = 0.5;

  for(int i = 0; i < 4; i++)
  {
    v[i].x -= (float) offset;
    v[i].y -= (float) offset;
    
    for(int j = 0; j < max(texcoords-1, 1); j++)
    {
      v[i].t[j].u -= (float) (offset*dx);
      v[i].t[j].v -= (float) (offset*dy);
    }

    if(texcoords > 1)
    {
      v[i].t[texcoords-1].u -= (float) offset;
      v[i].t[texcoords-1].v -= (float) offset;
    }
  }
}

template<int texcoords>
static HRESULT TextureBlt(IDirect3DDevice9* pD3DDev, MYD3DVERTEX<texcoords> v[4], D3DTEXTUREFILTERTYPE filter = D3DTEXF_LINEAR)
{
  if(!pD3DDev)
    return E_POINTER;

  DWORD FVF = 0;
  switch(texcoords)
  {
    case 1: FVF = D3DFVF_TEX1; break;
    case 2: FVF = D3DFVF_TEX2; break;
    case 3: FVF = D3DFVF_TEX3; break;
    case 4: FVF = D3DFVF_TEX4; break;
    case 5: FVF = D3DFVF_TEX5; break;
    case 6: FVF = D3DFVF_TEX6; break;
    case 7: FVF = D3DFVF_TEX7; break;
    case 8: FVF = D3DFVF_TEX8; break;
    default: return E_FAIL;
  }

  HRESULT hr;

  //Those are needed to avoid conflict with the xbmc gui
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_COLOROP, D3DTOP_MODULATE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_COLORARG1, D3DTA_TEXTURE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE );
  hr = pD3DDev->SetTextureStageState( 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE );

  hr = pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  hr = pD3DDev->SetRenderState(D3DRS_LIGHTING, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ZENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE); 
  hr = pD3DDev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE); 
  hr = pD3DDev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA|D3DCOLORWRITEENABLE_BLUE|D3DCOLORWRITEENABLE_GREEN|D3DCOLORWRITEENABLE_RED); 

  for(int i = 0; i < texcoords; i++)
  {
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_MAGFILTER, filter);
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_MINFILTER, filter);
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_MIPFILTER, filter);
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    hr = pD3DDev->SetSamplerState(i, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
  }
  hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | FVF);

  MYD3DVERTEX<texcoords> tmp = v[2]; 
  v[2] = v[3]; 
  v[3] = tmp;
  hr = pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(v[0]));

  for(int i = 0; i < texcoords; i++)
  {
    pD3DDev->SetTexture(i, NULL);
  }

  return S_OK;
}

static HRESULT DrawRect(IDirect3DDevice9* pD3DDev, MYD3DVERTEX<0> v[4])
{
  if(!pD3DDev)
    return E_POINTER;

  HRESULT hr = pD3DDev->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
  hr = pD3DDev->SetRenderState(D3DRS_LIGHTING, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ZENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_STENCILENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
  hr = pD3DDev->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA); 
  hr = pD3DDev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA); 
  //D3DRS_COLORVERTEX 
  hr = pD3DDev->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
  hr = pD3DDev->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);

  hr = pD3DDev->SetRenderState(D3DRS_COLORWRITEENABLE, D3DCOLORWRITEENABLE_ALPHA | D3DCOLORWRITEENABLE_BLUE |
    D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_RED);
  hr = pD3DDev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX0 | D3DFVF_DIFFUSE);

  MYD3DVERTEX<0> tmp = v[2];
  v[2] = v[3];
  v[3] = tmp;
  hr = pD3DDev->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, 2, v, sizeof(v[0]));  

  return S_OK;
}

////////////////////////////////////////////////////////////////////////////////////////////
class CEvrMixerThread : public CThread
{
public:
  CEvrMixerThread(IMFTransform* mixer, IMediaEventSink* sink);
  virtual ~CEvrMixerThread();

  unsigned int        GetReadyCount(void);
  unsigned int        GetFreeCount(void);
  IMFSample*          ReadyListPop(void);
  void                FreeListPush(IMFSample* pBuffer);
  bool                WaitOutput(unsigned int msec);

protected:
  bool                ProcessOutput(void);
  virtual void        Process(void);

  Com::CSyncPtrQueue<IMFSample> m_FreeList;
  Com::CSyncPtrQueue<IMFSample> m_ReadyList;

  Com::SmartPtr<IMFTransform>       m_pMixer;
  Com::SmartPtr<IMediaEventSink>    m_pSink;
  unsigned int                      m_timeout;
  bool                m_format_valid;
  int                 m_width;
  int                 m_height;
  uint64_t            m_timestamp;
  uint64_t            m_PictureNumber;
  uint8_t             m_color_space;
  unsigned int        m_color_range;
  unsigned int        m_color_matrix;
  int                 m_interlace;
  bool                m_framerate_tracking;
  uint64_t            m_framerate_cnt;
  double              m_framerate_timestamp;
  double              m_framerate;
  int                 m_aspectratio_x;
  int                 m_aspectratio_y;
  CEvent              m_ready_event;
  double              m_pSleepTimePerFrame;
};

CEvrMixerThread::CEvrMixerThread(IMFTransform* mixer, IMediaEventSink* sink) :
  CThread(),
  m_pMixer(mixer),
  m_pSink(sink),
  m_timeout(20),
  m_format_valid(false),
  m_framerate_tracking(false),
  m_framerate_cnt(0),
  m_framerate_timestamp(0.0),
  m_framerate(0.0),
  m_pSleepTimePerFrame(0.0)
{
}

CEvrMixerThread::~CEvrMixerThread()
{

  while(m_ReadyList.Count())
    m_ReadyList.Pop()->Release();
  while(m_FreeList.Count())
    m_FreeList.Pop()->Release();
  m_pMixer = NULL;
  m_pSink = NULL;
}

unsigned int CEvrMixerThread::GetReadyCount(void)
{
  return m_ReadyList.Count();
}

unsigned int CEvrMixerThread::GetFreeCount(void)
{
  return m_FreeList.Count();
}

IMFSample* CEvrMixerThread::ReadyListPop(void)
{
  IMFSample *pBuffer = m_ReadyList.Pop();
  return pBuffer;
}

void CEvrMixerThread::FreeListPush(IMFSample* pBuffer)
{
  m_FreeList.Push(pBuffer);
  pBuffer->AddRef();
}

bool CEvrMixerThread::WaitOutput(unsigned int msec)
{
  return m_ready_event.WaitMSec(msec);
}

bool CEvrMixerThread::ProcessOutput(void)
{
  HRESULT     hr = S_OK;
  DWORD       dwStatus = 0;
  LONGLONG    mixerStartTime = 0, mixerEndTime = 0, llMixerLatency = 0;
  MFTIME      systemTime = 0;
  
  
  if (m_FreeList.Count() == 0)
    return false;

  MFT_OUTPUT_DATA_BUFFER dataBuffer;
  ZeroMemory(&dataBuffer, sizeof(dataBuffer));
  IMFSample *pSample = NULL;
  // Get next output buffer from the free list
  pSample = m_FreeList.Pop();
  dataBuffer.pSample = pSample;
  mixerStartTime = GetPerfCounter();
  hr = m_pMixer->ProcessOutput (0 , 1, &dataBuffer, &dwStatus);
  mixerEndTime = GetPerfCounter();
  if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT)
  {
    m_FreeList.Push(pSample);
    return false;
  }
  if (m_pSink)
  {
    llMixerLatency = mixerEndTime - mixerStartTime;
    m_pSink->Notify (EC_PROCESSING_LATENCY, (LONG_PTR)&llMixerLatency, 0);
  }
  if (m_pSleepTimePerFrame == 0)
  {
    LONGLONG sample_duration = 0;
    if (SUCCEEDED(pSample->GetSampleDuration(&sample_duration)))
    {
      sample_duration = sample_duration-llMixerLatency;
      m_pSleepTimePerFrame = DS_TIME_TO_MSEC(sample_duration);
    }
  }
  m_ReadyList.Push(pSample);
}

void CEvrMixerThread::Process(void)
{
  DWORD res;
  // decoder is primed so now calls in DtsProcOutputXXCopy will block
  while (!m_bStop)
  {
    if (SUCCEEDED(m_pMixer->GetOutputStatus(&res)))
    {
      if (res & MFT_OUTPUT_STATUS_SAMPLE_READY)
        break;
    }
    
    Sleep(10);

  }
  CLog::Log(LOGINFO, "%s: CEvrMixerThread Started...", __FUNCTION__);
  while (!m_bStop)
  {

    if (SUCCEEDED(m_pMixer->GetOutputStatus(&res)&&(res & MFT_OUTPUT_STATUS_SAMPLE_READY)))
      ProcessOutput();
   
    Sleep(m_pSleepTimePerFrame);

  }
}

DsAllocator::DsAllocator(IDSInfoCallback *pCallback)
  : m_pCallback(pCallback),
    m_pMixerThread(NULL)
{
  
  
  HMODULE hlib=NULL;
  hlib = LoadLibrary(L"evr.dll");
  ptrMFCreateVideoSampleFromSurface = (FCT_MFCreateVideoSampleFromSurface)GetProcAddress(hlib,"MFCreateVideoSampleFromSurface");
  ptrMFCreateDXSurfaceBuffer = (FCT_MFCreateDXSurfaceBuffer)GetProcAddress(hlib,"MFCreateDXSurfaceBuffer");
  UINT resetToken = 0;
  HRESULT hr = DXVA2CreateDirect3DDeviceManager9(&resetToken, &m_pD3DDevManager);
  hr = m_pD3DDevManager->ResetDevice(m_pCallback->GetD3DDev(), resetToken);
  
  // Load Vista specifics DLLs
  hlib = LoadLibrary (L"AVRT.dll");
  pfAvSetMmThreadCharacteristicsW    = hlib ? (PTR_AvSetMmThreadCharacteristicsW)  GetProcAddress (hlib, "AvSetMmThreadCharacteristicsW") : NULL;
  pfAvSetMmThreadPriority        = hlib ? (PTR_AvSetMmThreadPriority)      GetProcAddress (hlib, "AvSetMmThreadPriority") : NULL;
  pfAvRevertMmThreadCharacteristics  = hlib ? (PTR_AvRevertMmThreadCharacteristics)  GetProcAddress (hlib, "AvRevertMmThreadCharacteristics") : NULL;

  surfallocnotify=NULL;
  refcount=1;
  inevrmode=false;
  m_pMixer=NULL;
  m_pSink=NULL;
  m_pClock=NULL;
  m_pMediaType=NULL;
  endofstream=false;
  ResetSyncOffsets();
  
  m_mutex = new CMutex();
  ResetStats();
  m_nRenderState        = Shutdown;
  //m_fUseInternalTimer      = false;
  m_LastSetOutputRange    = -1;
  m_bPendingRenegotiate    = false;
  m_bPendingMediaFinished    = false;
  m_bWaitingSample      = false;
  m_pCurrentDisplaydSample  = NULL;
  m_nStepCount        = 0;
  m_nUsedBuffer = 0;
  m_iLastSampleTime = 0;
  m_ClockTimeChangeHistoryPos = 0;
  m_ModeratedTimeSpeed = 0;
  m_ModeratedTimeSpeedPrim = 0;
	m_ModeratedTimeSpeedDiff = 0;
  m_OrderedPaint = 0;
  m_PaintTime = 0;
  m_PaintTimeMin = 0;
  m_PaintTimeMax = 0;
  //m_dwVideoAspectRatioMode  = MFVideoARMode_PreservePicture;
  //m_dwVideoRenderPrefs    = (MFVideoRenderPrefs)0;
  //m_BorderColor        = RGB (0,0,0);
  m_bSignaledStarvation    = false;
  m_StarvationClock      = 0;
  //m_pOuterEVR          = NULL;
  m_LastScheduledSampleTime  = -1;
  m_LastScheduledUncorrectedSampleTime = -1;
  m_MaxSampleDuration      = 0;
  m_LastSampleOffset      = 0;
  ZeroMemory(m_VSyncOffsetHistory, sizeof(m_VSyncOffsetHistory));
  m_VSyncOffsetHistoryPos = 0;
  m_bLastSampleOffsetValid  = false;
}

DsAllocator::~DsAllocator() 
{
  CleanupSurfaces();
  SAFE_RELEASE(m_mutex);
}

void DsAllocator::ResetStats()
{
  m_pcFrames      = 0;
  m_nDroppedUpdate  = 0;
  m_pcFramesDrawn    = 0;
  m_piAvg        = 0;
  m_piDev        = 0;
}

void DsAllocator::CleanupSurfaces() 
{
  Lock();
  for (size_t i=0;i<m_pSurfaces.size();i++) 
  {
    SAFE_RELEASE(m_pTextures[i]);
    SAFE_RELEASE(m_pSurfaces[i]);
  }
  Unlock();
}

HRESULT STDMETHODCALLTYPE DsAllocator::InitializeDevice(DWORD_PTR userid,VMR9AllocationInfo* allocinf,DWORD*numbuf)
{
  if (!surfallocnotify)
    return S_FALSE;
  
  CleanupSurfaces();
  Lock();
  m_pSurfaces.resize(*numbuf);
  HRESULT hr= surfallocnotify->AllocateSurfaceHelper(allocinf,numbuf,&m_pSurfaces.at(0));
  vheight=allocinf->dwHeight;
  vwidth=allocinf->dwWidth;
  Unlock();
  return hr;
}

void DsAllocator::LostDevice(IDirect3DDevice9 *d3ddev, IDirect3D9* d3d)
  {
  if (!surfallocnotify) return ;
  CleanupSurfaces();
  Lock();
  HMONITOR hmon=d3d->GetAdapterMonitor(D3DADAPTER_DEFAULT);
  surfallocnotify->ChangeD3DDevice(d3ddev,hmon);
  Unlock();

}

HRESULT STDMETHODCALLTYPE DsAllocator::TerminateDevice(DWORD_PTR userid)
{
  CleanupSurfaces();

  return S_OK;
}
HRESULT STDMETHODCALLTYPE DsAllocator::GetSurface(DWORD_PTR userid,DWORD surfindex,DWORD surfflags, IDirect3DSurface9** surf)
{
  if (surfindex>=m_pSurfaces.size()) return E_FAIL;
  if (surf==NULL) return E_POINTER;

  Lock();
  m_pSurfaces[surfindex]->AddRef();
  *surf=m_pSurfaces[surfindex];
  Unlock();
  return S_OK;
}
HRESULT STDMETHODCALLTYPE DsAllocator::AdviseNotify(IVMRSurfaceAllocatorNotify9* allnoty)
{
  Lock();
  surfallocnotify=allnoty;
  IDirect3D9 *d3d;
  IDirect3DDevice9 *d3ddev = m_pCallback->GetD3DDev();
  //OK lets set the direct3d object from the osd
  //d3ddev=((OsdWin*)Osd::getInstance())->getD3dDev();
  d3ddev->GetDirect3D(&d3d);
  HMONITOR hmon = (d3d->GetAdapterMonitor(D3DADAPTER_DEFAULT));
  HRESULT hr = surfallocnotify->SetD3DDevice(d3ddev,hmon);
  Unlock();
  return S_OK;//hr
}


HRESULT STDMETHODCALLTYPE DsAllocator::StartPresenting(DWORD_PTR userid)
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::StopPresenting(DWORD_PTR userid)
{
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::PresentImage(DWORD_PTR userid,VMR9PresentationInfo* presinf)
{
  //TODO
  return S_OK;

}

HRESULT STDMETHODCALLTYPE DsAllocator::QueryInterface(REFIID refiid,void ** obj)
{
  if (obj==NULL) return E_POINTER;

  if (refiid==IID_IVMRSurfaceAllocator9)
  {
    *obj=static_cast<IVMRSurfaceAllocator9*>(this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IVMRImagePresenter9)
  {
    *obj=static_cast<IVMRImagePresenter9*>(this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IMFVideoDeviceID)
  {
    *obj=static_cast<IMFVideoDeviceID*> (this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IMFTopologyServiceLookupClient )
  {
    *obj=static_cast<IMFTopologyServiceLookupClient*> (this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IQualProp )
  {
    *obj=static_cast<IQualProp*> (this);
    AddRef();
    return S_OK;
  }  else if (refiid==IID_IMFGetService)
  {
    *obj=static_cast<IMFGetService*> (this);
    AddRef();
    return S_OK;
  }
  else if (refiid==IID_IDirect3DDeviceManager9) 
  {
    if (m_pD3DDevManager)
{
      return m_pD3DDevManager->QueryInterface(refiid,obj);
    }
    else
    {
      return E_NOINTERFACE;
    }
  }
  return E_NOINTERFACE;
}



ULONG STDMETHODCALLTYPE  DsAllocator::AddRef()
{
  return InterlockedIncrement(&refcount);
}

ULONG STDMETHODCALLTYPE DsAllocator::Release()
{
  ULONG ref=0;
  ref=InterlockedDecrement(&refcount);
  if (ref==NULL)
  {
    delete this; //Commit suicide
  }
  return ref;
}

HRESULT STDMETHODCALLTYPE  DsAllocator::GetDeviceID(IID *pDid)
{
  if (pDid==NULL)
    return E_POINTER;

  *pDid=__uuidof(IDirect3DDevice9);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::InitServicePointers(IMFTopologyServiceLookup *plooky)
{
  if (!plooky) return E_POINTER;
  
  inevrmode=true;
  /* get all interfaces we need*/

  DWORD dwobjcts=1;
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL,0,MR_VIDEO_MIXER_SERVICE,
    __uuidof(IMFTransform),(void**)&m_pMixer, &dwobjcts);
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL,0,MR_VIDEO_RENDER_SERVICE,
    __uuidof(IMediaEventSink),(void**)&m_pSink, &dwobjcts);
  plooky->LookupService(MF_SERVICE_LOOKUP_GLOBAL, 0, MR_VIDEO_RENDER_SERVICE,
    __uuidof(IMFClock),(void**)&m_pClock,&dwobjcts);
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::ReleaseServicePointers()
{
  Lock();
  inevrmode=false;
  /* TODO Set RenderState , sample type etc.*/

  //((OsdWin*)Osd::getInstance())->setExternalDriving(NULL,0,0);

  
  m_pMixer = NULL;
  m_pSink = NULL;
  m_pClock = NULL;
  m_pMediaType = NULL;
  //if (m_pSink) m_pSink->Release();
  //if (m_pMixer) m_pMixer->Release();
  //if (m_pClock) m_pClock->Release();
  //if (m_pMediaType) m_pMediaType->Release();
  Unlock();
  return S_OK;
}

HRESULT STDMETHODCALLTYPE DsAllocator::GetService(const GUID &guid,const IID &iid,LPVOID *obj)
{
  if (guid==MR_VIDEO_ACCELERATION_SERVICE)
  {
    if (m_pD3DDevManager)
    {
      return m_pD3DDevManager->QueryInterface (__uuidof(IDirect3DDeviceManager9), (void**) obj);
    }
    else
    {
      return E_NOINTERFACE;
    }

  } 
  else if (guid==MR_VIDEO_RENDER_SERVICE)
  {
    return QueryInterface(iid,obj);
  } 
  else
  {
    return E_NOINTERFACE;
  }
}

HRESULT STDMETHODCALLTYPE DsAllocator::ProcessMessage(MFVP_MESSAGE_TYPE mess,ULONG_PTR mess_para)
{
  switch (mess)
  {
    case MFVP_MESSAGE_FLUSH:
      CLog::DebugLog("EVR: MFVP_MESSAGE_FLUSH");
      if (m_pMixerThread)
        m_pMixerThread->StopThread(true);
      
    break;
    case MFVP_MESSAGE_INVALIDATEMEDIATYPE: 
      m_bPendingRenegotiate = true;
      RenegotiateEVRMediaType();
    break;
    case MFVP_MESSAGE_PROCESSINPUTNOTIFY: 
    break;
    case MFVP_MESSAGE_BEGINSTREAMING:
    {
      CLog::DebugLog("EVR Message MFVP_MESSAGE_BEGINSTREAMING received");
      ResetStats();  
    }
    break;
    case MFVP_MESSAGE_ENDSTREAMING:
      CLog::DebugLog("EVR Message MFVP_MESSAGE_ENDSTREAMING received");
      if (m_pMixerThread)
      {
        while(m_BusyList.Count())
          m_pMixerThread->FreeListPush( m_BusyList.Pop() );

        m_pMixerThread->StopThread();
        delete m_pMixerThread;
        m_pMixerThread = NULL;
      }
      break;
    case MFVP_MESSAGE_ENDOFSTREAM: 
    {
     CLog::DebugLog("EVR Message MFVP_MESSAGE_ENDOFSTREAM received");

      m_bPendingMediaFinished = true;
      endofstream=true;
    } 
    break;
    case MFVP_MESSAGE_STEP:
    {
      // Request frame step the param is the number of frame to step
      CLog::Log(LOGINFO, "EVR Message MFVP_MESSAGE_STEP %i", mess_para);
      m_nStepCount = mess_para;
    } 
    break;
    case MFVP_MESSAGE_CANCELSTEP:
    {
      CLog::Log(LOGINFO, "EVR Message MFVP_MESSAGE_CANCELSTEP received");
      //???
    } 
    break;
    default:
      CLog::Log(LOGINFO, "DsAllocator::ProcessMessage unhandled");
  };
  return S_OK;
}
// IMFClockStateSink
STDMETHODIMP DsAllocator::OnClockStart(MFTIME hnsSystemTime,  int64_t llClockStartOffset)
{
  m_nRenderState    = Started;

  CLog::DebugLog("EVR: OnClockStart  hnsSystemTime = %I64d,   llClockStartOffset = %I64d", hnsSystemTime, llClockStartOffset);
  m_ModeratedTimeLast = -1;
  m_ModeratedClockLast = -1;

  return S_OK;
}

STDMETHODIMP DsAllocator::OnClockStop(MFTIME hnsSystemTime)
{
  CLog::DebugLog("EVR: OnClockStop  hnsSystemTime = %I64d", hnsSystemTime);
  m_nRenderState    = Stopped;

  m_ModeratedClockLast = -1;
  m_ModeratedTimeLast = -1;
  return S_OK;
}

STDMETHODIMP DsAllocator::OnClockPause(MFTIME hnsSystemTime)
{
  CLog::DebugLog("EVR: OnClockPause  hnsSystemTime = %I64d", hnsSystemTime);
  if (!m_bSignaledStarvation)
    m_nRenderState    = Paused;

  m_ModeratedTimeLast = -1;
  m_ModeratedClockLast = -1;
  return S_OK;
}

STDMETHODIMP DsAllocator::OnClockRestart(MFTIME hnsSystemTime)
{
  m_nRenderState  = Started;

  m_ModeratedTimeLast = -1;
  m_ModeratedClockLast = -1;
  CLog::DebugLog("EVR: OnClockRestart  hnsSystemTime = %I64d", hnsSystemTime);

  return S_OK;
}


STDMETHODIMP DsAllocator::OnClockSetRate(MFTIME hnsSystemTime, float flRate)
{
  ASSERT (FALSE);
  return E_NOTIMPL;
}

void ModerateFloat(double& Value, double Target, double& ValuePrim, double ChangeSpeed)
{
  double xbiss = (-(ChangeSpeed)*(ValuePrim) - (Value-Target)*(ChangeSpeed*ChangeSpeed)*0.25f);
  ValuePrim += xbiss;
  Value += ValuePrim;
}

int64_t DsAllocator::GetClockTime(int64_t PerformanceCounter)
{
  int64_t       llClockTime;
  MFTIME        nsCurrentTime;
  if (! m_pClock)
    return 0;

  m_pClock->GetCorrelatedTime(0, &llClockTime, &nsCurrentTime);
  DWORD Characteristics = 0;
  m_pClock->GetClockCharacteristics(&Characteristics);
  MFCLOCK_STATE State;
  m_pClock->GetState(0, &State);

  if (!(Characteristics & MFCLOCK_CHARACTERISTICS_FLAG_FREQUENCY_10MHZ))
  {
    MFCLOCK_PROPERTIES Props;
    if (m_pClock->GetProperties(&Props) == S_OK)
      llClockTime = (llClockTime * 10000000) / Props.qwClockFrequency; // Make 10 MHz

  }
  int64_t llPerf = PerformanceCounter;
//  return llClockTime + (llPerf - nsCurrentTime);
  double Target = llClockTime + (llPerf - nsCurrentTime) * m_ModeratedTimeSpeed;

  bool bReset = false;
  if (m_ModeratedTimeLast < 0 || State != m_LastClockState || m_ModeratedClockLast < 0)
  {
    bReset = true;
    m_ModeratedTimeLast = llPerf;
    m_ModeratedClockLast = llClockTime;
  }

  m_LastClockState = State;

  double TimeChange = (double) llPerf - m_ModeratedTimeLast;
  double ClockChange = (double) llClockTime - m_ModeratedClockLast;

  m_ModeratedTimeLast = llPerf;
  m_ModeratedClockLast = llClockTime;

  if (bReset)
  {
    m_ModeratedTimeSpeed = 1.0;
    m_ModeratedTimeSpeedPrim = 0.0;
    ZeroMemory(m_TimeChangeHistory, sizeof(m_TimeChangeHistory));
    ZeroMemory(m_ClockChangeHistory, sizeof(m_ClockChangeHistory));
    m_ClockTimeChangeHistoryPos = 0;
  }
  if (TimeChange)
  {
    int Pos = m_ClockTimeChangeHistoryPos % 100;
    int nHistory = std::min(m_ClockTimeChangeHistoryPos, 100);
    ++m_ClockTimeChangeHistoryPos;
    if (nHistory > 50)
    {
      int iLastPos = (Pos - (nHistory)) % 100;
      if (iLastPos < 0)
        iLastPos += 100;

      double TimeChange = llPerf - m_TimeChangeHistory[iLastPos];
      double ClockChange = llClockTime - m_ClockChangeHistory[iLastPos];

      double ClockSpeedTarget = ClockChange / TimeChange;
      double ChangeSpeed = 0.1;
      if (ClockSpeedTarget > m_ModeratedTimeSpeed)
      {
        if (ClockSpeedTarget / m_ModeratedTimeSpeed > 0.1)
          ChangeSpeed = 0.1;
        else
          ChangeSpeed = 0.01;
      }
      else 
      {
        if (m_ModeratedTimeSpeed / ClockSpeedTarget > 0.1)
          ChangeSpeed = 0.1;
        else
          ChangeSpeed = 0.01;
      }
      ModerateFloat(m_ModeratedTimeSpeed, ClockSpeedTarget, m_ModeratedTimeSpeedPrim, ChangeSpeed);
//      m_ModeratedTimeSpeed = TimeChange / ClockChange;
    }
    m_TimeChangeHistory[Pos] = (double) llPerf;
    m_ClockChangeHistory[Pos] = (double) llClockTime;
  }

  return (int64_t) Target;
}

// IBaseFilter delegate
bool DsAllocator::GetState( DWORD dwMilliSecsTimeout, FILTER_STATE *State, HRESULT &_ReturnValue)
{

  if (m_bSignaledStarvation)
  {
    
    unsigned int nSamples = std::max(m_pSurfaces.size() / 2, 1U);
    if ((m_pMixerThread->GetReadyCount() < nSamples || m_LastSampleOffset < -m_rtTimePerFrame*2) /*&& !g_bNoDuration*/)
    {
      *State = (FILTER_STATE)Paused;
      _ReturnValue = VFW_S_STATE_INTERMEDIATE;
      return true;
    }
    m_bSignaledStarvation = false;
  }
  return false;
}

// IQualProp
STDMETHODIMP DsAllocator::get_FramesDroppedInRenderer(int *pcFrames)
{
  *pcFrames  = m_pcFrames;
  return S_OK;
}
STDMETHODIMP DsAllocator::get_FramesDrawn(int *pcFramesDrawn)
{
  *pcFramesDrawn = m_pcFramesDrawn;
  return S_OK;
}
STDMETHODIMP DsAllocator::get_AvgFrameRate(int *piAvgFrameRate)
{
  *piAvgFrameRate = (int)(m_fAvrFps * 100);
  return S_OK;
}
STDMETHODIMP DsAllocator::get_Jitter(int *iJitter)
{
  *iJitter = (int)((m_fJitterStdDev/10000.0) + 0.5);
  return S_OK;
}
STDMETHODIMP DsAllocator::get_AvgSyncOffset(int *piAvg)
{
  *piAvg = (int)((m_fSyncOffsetAvr/10000.0) + 0.5);
  return S_OK;
}
STDMETHODIMP DsAllocator::get_DevSyncOffset(int *piDev)
{
  *piDev = (int)((m_fSyncOffsetStdDev/10000.0) + 0.5);
  return S_OK;
}


// IMFRateSupport
STDMETHODIMP DsAllocator::GetSlowestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float *pflRate)
{
  // TODO : not finished...
  *pflRate = 0;
  return S_OK;
}
    
STDMETHODIMP DsAllocator::GetFastestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float *pflRate)
{
  HRESULT    hr = S_OK;
  float    fMaxRate = 0.0f;

  CAutoLock lock(this);

  CheckPointer(pflRate, E_POINTER);
  CheckHR(CheckShutdown());
    
  // Get the maximum forward rate.
  fMaxRate = GetMaxRate(fThin);

  // For reverse playback, swap the sign.
  if (eDirection == MFRATE_REVERSE)
    fMaxRate = -fMaxRate;

  *pflRate = fMaxRate;

  return hr;
}
    
STDMETHODIMP DsAllocator::IsRateSupported(BOOL fThin, float flRate, float *pflNearestSupportedRate)
{
    // fRate can be negative for reverse playback.
    // pfNearestSupportedRate can be NULL.

    CAutoLock lock(this);

    HRESULT hr = S_OK;
    float   fMaxRate = 0.0f;
    float   fNearestRate = flRate;   // Default.

  CheckPointer (pflNearestSupportedRate, E_POINTER);
    CheckHR(hr = CheckShutdown());

    // Find the maximum forward rate.
    fMaxRate = GetMaxRate(fThin);

    if (fabsf(flRate) > fMaxRate)
    {
        // The (absolute) requested rate exceeds the maximum rate.
        hr = MF_E_UNSUPPORTED_RATE;

        // The nearest supported rate is fMaxRate.
        fNearestRate = fMaxRate;
        if (flRate < 0)
        {
            // For reverse playback, swap the sign.
            fNearestRate = -fNearestRate;
        }
    }

    // Return the nearest supported rate if the caller requested it.
    if (pflNearestSupportedRate != NULL)
        *pflNearestSupportedRate = fNearestRate;

    return hr;
}


float DsAllocator::GetMaxRate(BOOL bThin)
{
  float   fMaxRate    = FLT_MAX;  // Default.
  UINT32  fpsNumerator  = 0, fpsDenominator = 0;
  UINT    MonitorRateHz  = 0; 

  if (!bThin && (m_pMediaType != NULL))
  {
    // Non-thinned: Use the frame rate and monitor refresh rate.
        
    // Frame rate:
    MFGetAttributeRatio(m_pMediaType, MF_MT_FRAME_RATE, 
      &fpsNumerator, &fpsDenominator);

    // Monitor refresh rate:
    MonitorRateHz = m_RefreshRate; // D3DDISPLAYMODE

    if (fpsDenominator && fpsNumerator && MonitorRateHz)
    {
      // Max Rate = Refresh Rate / Frame Rate
      fMaxRate = (float)MulDiv(
        MonitorRateHz, fpsDenominator, fpsNumerator);
    }
  }
  return fMaxRate;
}

void DsAllocator::CompleteFrameStep(bool bCancel)
{
  if (m_nStepCount > 0)
  {
    if (bCancel || (m_nStepCount == 1)) 
    {
      m_pSink->Notify(EC_STEP_COMPLETE, bCancel ? TRUE : FALSE, 0);
      m_nStepCount = 0;
    }
    else
      m_nStepCount--;
  }
}

void DsAllocator::SetDSMediaType(CMediaType mt)
{
  if (mt.formattype==FORMAT_VideoInfo)
  {
    m_rtTimePerFrame = ((VIDEOINFOHEADER*)mt.pbFormat)->AvgTimePerFrame;
    m_bInterlaced = false;
  }
  else if (mt.formattype==FORMAT_VideoInfo2)
  {
    m_rtTimePerFrame = ((VIDEOINFOHEADER2*)mt.pbFormat)->AvgTimePerFrame;
    m_bInterlaced = (((VIDEOINFOHEADER2*)mt.pbFormat)->dwInterlaceFlags & AMINTERLACE_IsInterlaced) != 0;
  }
  else if (mt.formattype==FORMAT_MPEGVideo)
  {
    m_rtTimePerFrame = ((MPEG1VIDEOINFO*)mt.pbFormat)->hdr.AvgTimePerFrame;
    m_bInterlaced = false;
  }
  else if (mt.formattype==FORMAT_MPEG2Video)
  {
    m_rtTimePerFrame = ((MPEG2VIDEOINFO*)mt.pbFormat)->hdr.AvgTimePerFrame;
    m_bInterlaced = (((MPEG2VIDEOINFO*)mt.pbFormat)->hdr.dwInterlaceFlags & AMINTERLACE_IsInterlaced) != 0;
  }

  if (m_rtTimePerFrame == 0) 
    m_rtTimePerFrame = 417166;
  m_fps = (float)(10000000.0 / m_rtTimePerFrame);
}

HRESULT STDMETHODCALLTYPE DsAllocator::GetCurrentMediaType(IMFVideoMediaType **mtype)
{ 
  OutputDebugString(L"mediatype\n");
  if (!mtype) 
    return E_POINTER;
  Lock();
  if (!m_pMediaType)
  {
    Unlock();
    *mtype=NULL;
    return MF_E_NOT_INITIALIZED;
  }
  HRESULT hr=m_pMediaType->QueryInterface(IID_IMFVideoMediaType,(void**)mtype);
  Unlock();
  return hr;
}

void DsAllocator::RenegotiateEVRMediaType()
{
  if (!m_pMixer)
  {
    OutputDebugString(L"Cannot renegotiate without transform!");
    return ;
  }
  bool gotcha=false;
  DWORD index=0;


  while (!gotcha)
  {
    IMFMediaType *mixtype=NULL;
    HRESULT hr;
    if (hr=m_pMixer->GetOutputAvailableType(0,index++,&mixtype)!=S_OK)
    {
      DebugPrint(L"No more types availiable from EVR %d !",hr);
      break;
    }

    //Type check
    BOOL compressed;
    mixtype->IsCompressedFormat(&compressed); 
    if (compressed)
    {
      mixtype->Release();
      continue;
    }
    UINT32 helper;
    mixtype->GetUINT32(MF_MT_INTERLACE_MODE,&helper);
    if (helper!=MFVideoInterlace_Progressive)
    {
      OutputDebugString(L"Skip media type interlaced!");
      mixtype->Release();
      continue;
    }
    GUID temp;
    mixtype->GetMajorType(&temp);
    if (temp!=MEDIATYPE_Video)
  {
      OutputDebugString(L"Skip media type no video!");
      mixtype->Release();
      continue;
    }
    if(m_pMixer->SetOutputType(0,mixtype,MFT_SET_TYPE_TEST_ONLY)!=S_OK) 
    {
      OutputDebugString(L"Skip media type test failed!");
      mixtype->Release();
      continue;
    }
    //Type is ok!

    gotcha=true;

    Lock();
    //if (m_pMediaType) m_pMediaType->Release();
    m_pMediaType=NULL;

    m_pMediaType=mixtype;
    AllocateEVRSurfaces();
    Unlock();

    hr=m_pMixer->SetOutputType(0,mixtype,0);



    if (hr!=S_OK) 
    {
      Lock();
      //if (m_pMediaType) m_pMediaType->Release();
      m_pMediaType=NULL;
      gotcha=false;

      Unlock();
    }


    DebugPrint(L"Output type set! %d",hr);
  }
  if (!gotcha)
    OutputDebugString(L"No suitable output type!");
  
  
  
  m_pMixerThread->Create();


}

int DsAllocator::GetReadySample()
{ 
  return m_pMixerThread->GetReadyCount();
}

void DsAllocator::AllocateEVRSurfaces()
{
  LARGE_INTEGER temp64;
  m_pMediaType->GetUINT64(MF_MT_FRAME_SIZE, (UINT64*)&temp64);
  vwidth=temp64.HighPart;
  vheight=temp64.LowPart;
  GUID subtype;
  subtype.Data1=D3DFMT_X8R8G8B8;
  m_pMediaType->GetGUID(MF_MT_SUBTYPE,&subtype);
  D3DFORMAT format=(D3DFORMAT)subtype.Data1;
  CLog::DebugLog("Surfaceformat is %d, width %d, height %d",format,vwidth,vheight);
  format=D3DFMT_X8R8G8B8;

  RemoveAllSamples();
  m_pMixerThread = new CEvrMixerThread(m_pMixer, m_pSink);
  //CleanupSurfaces();

  m_pSurfaces.resize(10);
  m_pTextures.resize(10);
  for (int i=0;i<10;i++)
  {
    HRESULT hr;
    hr = m_pCallback->GetD3DDev()->CreateTexture(vwidth, vheight, 1, D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT,&m_pTextures[i],NULL);
    if (SUCCEEDED(hr))
    {
      if (FAILED(m_pTextures[i]->GetSurfaceLevel(0, &m_pSurfaces[i])))
      {
        //assert(0);
      }
      
    }
    else
    {
      m_pSurfaces[i]=NULL;
    }
  }
  
  

  HRESULT hr = S_OK;
  for (size_t i=0;i<m_pSurfaces.size();i++)
  {
    if (m_pSurfaces[i]!=NULL) 
    {
      IMFSample* pMFSample = NULL;
      hr = ptrMFCreateVideoSampleFromSurface(m_pSurfaces[i],&pMFSample);
      pMFSample->SetUINT32 (GUID_SURFACE_INDEX, i);
      
      if (SUCCEEDED (hr))
      {
        pMFSample->SetUINT32 (GUID_SURFACE_INDEX, i);
        m_pMixerThread->FreeListPush(pMFSample);
      }
      ASSERT(SUCCEEDED (hr));
    }
  }

}

void DsAllocator::ResetSyncOffsets()
{
  for (int i=0;i<n_stats;i++)
  {
    sync_offset[i]=0;
    jitter_offset[i]=0;
  }
  framesdrawn=0;
  lastdelframe=0;
  framesdropped=0;
  avg_sync_offset=0;
  dev_sync_offset=0;
  jitter=0;
  sync_pos=0;
  jitter_pos=0;
  avgfps=0;
}

void DsAllocator::CalcSyncOffsets(int sync)
{
  sync_offset[sync_pos]=sync;
  sync_pos=(sync_pos +1)%n_stats;

  double mean_value=0;
  for (int i=0;i<n_stats;i++)
  {
    mean_value+=sync_offset[i];
  }
  mean_value/=(double) n_stats;
  double std_dev=0;
  for (int i=0;i<n_stats;i++)
  {
    double temp_dev=(mean_value-(double)sync_offset[i]);
    std_dev+=temp_dev*temp_dev;
  }
  std_dev/=(double)n_stats;
  avg_sync_offset=mean_value;
  dev_sync_offset=sqrt(std_dev);
}

void DsAllocator::CalcJitter(int jitter)
{
  jitter_offset[jitter_pos]=jitter;
  jitter_pos=(jitter_pos +1)%n_stats;
  

  double mean_value=0;
  for (int i=0;i<n_stats;i++)
  {
    mean_value+=jitter_offset[i];
  }
  mean_value/=(double) n_stats;
  avgfps=1000./mean_value*100.;
  double std_dev=0;
  for (int i=0;i<n_stats;i++)
  {
    double temp_dev=(mean_value-(double)jitter_offset[i]);
    std_dev+=temp_dev*temp_dev;
  }
  std_dev/=(double)n_stats;
  jitter=sqrt(std_dev);
}

IPaintCallback* DsAllocator::AcquireCallback()
{
  
  return static_cast<IPaintCallback*>(this);
  
}


bool DsAllocator::SurfaceReady()
{
  DWORD status;
  if (S_OK == m_pMixer->GetOutputStatus(&status))
  {
    if (status & MFT_OUTPUT_STATUS_SAMPLE_READY)
      return true;
    
  }
  
    return false;
}

bool DsAllocator::WaitOutput(unsigned int msec)
{
  return m_ready_event.WaitMSec(msec);
}

void DsAllocator::Render(const RECT& dst, IDirect3DSurface9* target, int index)
{
  HRESULT hr = S_OK;;
  LONGLONG prestime = 0;
  MFTIME   systime = 0;
  LONGLONG currenttime = 0;
  DWORD waittime = 10;
  if (!m_pCallback || endofstream)
    return;
  LPDIRECT3DDEVICE9 pDevice = m_pCallback->GetD3DDev();
  
  int64_t  llPerf = 0;
  int                         nSamplesLeft = 0;
  CAutoSingleLock lock(m_section);
    

  if ((index < 0 )&& (index > m_pSurfaces.size()))
    return;
  if (m_pSurfaces.at(index))
  {
    Vector v[4];
    v[0] = Vector(dst.left, dst.top, 0);
    v[1] = Vector(dst.right, dst.top, 0);
    v[2] = Vector(dst.left, dst.bottom, 0);
    v[3] = Vector(dst.right, dst.bottom, 0);
    int centerx,centery;
    centerx = (dst.right-dst.left)/2;
    centery = (dst.bottom-dst.top)/2;
    Vector center(centerx, centery, 0);
    XForm xform;
    xform = XForm(Ray(Vector(0, 0, 0), Vector()), Vector(1, 1, 1), false);
    int l = (int)(Vector(dst.right-dst.left, dst.top - dst.bottom, 0).Length()*1.5f)+1;

    for(ptrdiff_t i = 0; i < 4; i++)
    {
      v[i] = xform << (v[i] - center);
      v[i].z = v[i].z / l + 0.5f;
      v[i].x /= v[i].z*2;
      v[i].y /= v[i].z*2;
      v[i] += center;
    }
    
    D3DSURFACE_DESC desc;
    m_pTextures[index]->GetLevelDesc(0,&desc);
    const float dx = 1.0f/(float)desc.Width;
    const float dy = 1.0f/(float)desc.Height;
    const float tx0 = (float) 0;
    const float tx1 = (float) vwidth;
    const float ty0 = (float) 0;
    const float ty1 = (float) vheight;
    float fConstData[][4] = {{dx*0.5f, dy*0.5f, 0, 0}, {dx, dy, 0, 0}, {dx, 0, 0, 0}, {0, dy, 0, 0}};
    hr = pDevice->SetPixelShaderConstantF(0, (float*)fConstData, 4);
/*CWinRenderer::CropSource(vs.SrcRect, vs.DstRect, desc);*/
    MYD3DVERTEX<1> vv[] =
  {
    {v[0].x, v[0].y, v[0].z, 1.0f/v[0].z,  tx0, ty0},
    {v[1].x, v[1].y, v[1].z, 1.0f/v[1].z,  tx1, ty0},
    {v[2].x, v[2].y, v[2].z, 1.0f/v[2].z,  tx0, ty1},
    {v[3].x, v[3].y, v[3].z, 1.0f/v[3].z,  tx1, ty1},
  };

  AdjustQuad(vv, 1.0, 1.0);
  hr = pDevice->SetTexture(0,m_pTextures[index]);
  hr = TextureBlt(pDevice, vv, D3DTEXF_POINT);
    
    //vheight
    //vwidth
    
  if (FAILED(hr))
  {
    hr = pDevice->StretchRect(m_pSurfaces[index], NULL, target, NULL, D3DTEXF_NONE);
    //OutputDebugStringA("DSAllocator Failed to Render Target with EVR");
  }
  //Lock();
  /*if (fullevrsamples.size()==0)
  {
    waittime=0;
    Unlock();
    return;
  }*/
  }
  
}

bool DsAllocator::GetPicture(DVDVideoPicture *pDvdVideoPicture)
{
  IMFSample* pMFSample = m_pMixerThread->ReadyListPop();
  if (!pMFSample)
    return false;
  int nSamplesLeft = 0;
  int surf_index = 0;
  LONGLONG sample_time;
  CAutoSingleLock lock(m_section);
  HRESULT hr = pMFSample->GetUINT32(GUID_SURFACE_INDEX, (UINT32 *)&surf_index);
  pDvdVideoPicture->pts = DVD_NOPTS_VALUE;
  pDvdVideoPicture->dts = DVD_NOPTS_VALUE;
  if (SUCCEEDED(pMFSample->GetSampleTime(&sample_time)))
    pDvdVideoPicture->pts = sample_time / 10;
  
  UINT32 width, height;
  /*if (SUCCEEDED(MFGetAttributeSize(pMFSample, MF_MT_FRAME_SIZE, &width, &height)))
  {
    pDvdVideoPicture->iWidth = width;
    pDvdVideoPicture->iHeight = height;
    pDvdVideoPicture->iDisplayWidth = width;
    pDvdVideoPicture->iDisplayHeight = height;
  }*/
  pDvdVideoPicture->pSurfaceIndex = surf_index;
  
  while( m_BusyList.Count())
    m_pMixerThread->FreeListPush( m_BusyList.Pop() );
  m_BusyList.Push(pMFSample);

  return true;

}

/*Sample stuff*/
bool DsAllocator::AcceptMoreData()
{
  return (m_pMixerThread->GetFreeCount()>0);
}

void DsAllocator::RemoveAllSamples()
{
  if (m_pMixerThread)
  {
    while(m_BusyList.Count())
      m_pMixerThread->FreeListPush( m_BusyList.Pop() );

    m_pMixerThread->StopThread();
    delete m_pMixerThread;
    m_pMixerThread = NULL;
  }
  m_nUsedBuffer = 0;
}

