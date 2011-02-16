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

#ifndef DSALLOCATOR_H
#define DSALLOCATOR_H

#include <queue>
#include <vector>


using namespace std;

#include <winsock2.h>

#include <d3d9.h>
#include <dshow.h>
#include <vmr9.h>
#include <evr.h>
#include <Dxva2api.h>
#include <evcode.h>
#include <mferror.h>
#include "IPaintCallback.h"
#include "DSNative.h"
#include "CoordGeom.h"
#include "SmartList.h"
#include "..\..\xbmc\cores\dllloader\Win32DllLoader.h"
#include "..\..\xbmc\threads\mutex.h"
#include "threads/Event.h"


#ifndef EXECUTE_ASSERT
#define EXECUTE_ASSERT(_x_) ASSERT(_x_)
#endif
#include "wxutil.h"
#ifndef EC_PROCESSING_LATENCY
#define EC_PROCESSING_LATENCY               0x21
#endif

typedef Com::ComPtrList<IMFSample> VideoSampleList;

//The Allocator and Presenter for VMR9 is also a Presenter for EVR
class DsAllocator 
  : public IVMRSurfaceAllocator9, IVMRImagePresenter9, 
    public IMFVideoDeviceID, IMFTopologyServiceLookupClient,
    public IMFVideoPresenter,IMFGetService, IQualProp, IMFRateSupport,
    public IPaintCallback, CCritSec//callback from xbmc to render on the specified target
{
public:
  CEvent          m_drawingIsDone;
  DsAllocator(IDSInfoCallback *pCallback);
  virtual ~DsAllocator();

  virtual HRESULT STDMETHODCALLTYPE StartPresenting(DWORD_PTR userid);
  virtual HRESULT STDMETHODCALLTYPE StopPresenting(DWORD_PTR userid);
  virtual HRESULT STDMETHODCALLTYPE PresentImage(DWORD_PTR userid,VMR9PresentationInfo* presinf);

  virtual HRESULT STDMETHODCALLTYPE InitializeDevice(DWORD_PTR userid,
    VMR9AllocationInfo* allocinf,DWORD* numbuf);
  virtual HRESULT STDMETHODCALLTYPE TerminateDevice(DWORD_PTR userid); 
  virtual HRESULT STDMETHODCALLTYPE GetSurface(DWORD_PTR userid,DWORD surfindex,DWORD surfflags, IDirect3DSurface9** surf);
  virtual HRESULT STDMETHODCALLTYPE AdviseNotify(IVMRSurfaceAllocatorNotify9* allnoty);
  

  virtual HRESULT STDMETHODCALLTYPE QueryInterface(REFIID refiid,void ** obj);
  virtual ULONG STDMETHODCALLTYPE AddRef();
  virtual ULONG STDMETHODCALLTYPE Release();

  IPaintCallback* AcquireCallback();
  

  void LostDevice(IDirect3DDevice9 *d3ddev, IDirect3D9* d3d);

  /* EVR members */
  virtual HRESULT STDMETHODCALLTYPE GetDeviceID(IID *pDid);

  virtual HRESULT STDMETHODCALLTYPE InitServicePointers(IMFTopologyServiceLookup *plooky);
  virtual HRESULT STDMETHODCALLTYPE ReleaseServicePointers();

  virtual HRESULT STDMETHODCALLTYPE ProcessMessage(MFVP_MESSAGE_TYPE mess,ULONG_PTR mess_para);

  virtual HRESULT STDMETHODCALLTYPE OnClockStart(MFTIME systime,LONGLONG startoffset);
  virtual HRESULT STDMETHODCALLTYPE OnClockStop(MFTIME systime);
  virtual HRESULT STDMETHODCALLTYPE OnClockPause(MFTIME systime);
  virtual HRESULT STDMETHODCALLTYPE OnClockRestart(MFTIME systime);
  virtual HRESULT STDMETHODCALLTYPE OnClockSetRate(MFTIME systime,float rate);
  virtual HRESULT STDMETHODCALLTYPE GetCurrentMediaType(IMFVideoMediaType **mtype);

  virtual HRESULT STDMETHODCALLTYPE GetService(const GUID &guid,const IID &iid,LPVOID *obj);
  /*IBaseFilter*/
  bool   GetState( DWORD dwMilliSecsTimeout, FILTER_STATE *State, HRESULT &_ReturnValue);

  /*IQualProp*/
  virtual HRESULT STDMETHODCALLTYPE get_FramesDrawn(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_AvgFrameRate(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_Jitter(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_AvgSyncOffset(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_DevSyncOffset(int *val);
  virtual HRESULT STDMETHODCALLTYPE get_FramesDroppedInRenderer(int *val);
  
  // IMFRateSupport
  STDMETHODIMP GetSlowestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float *pflRate);
  STDMETHODIMP GetFastestRate(MFRATE_DIRECTION eDirection, BOOL fThin, float *pflRate);
  STDMETHODIMP IsRateSupported(BOOL fThin, float flRate, float *pflNearestSupportedRate);
  
  float GetMaxRate(BOOL bThin);

  HRESULT CheckShutdown() const 
{
    if (m_nRenderState == Shutdown)
        return MF_E_SHUTDOWN;
    else
        return S_OK;
}

  void CompleteFrameStep(bool bCancel);
  void CheckWaitingSampleFromMixer(){if (m_bWaitingSample) m_bWaitingSample = false; }
  bool AcceptMoreData();
  bool SurfaceReady();
  void Lock() { m_mutex->Wait(); }
  void Unlock() { m_mutex->Release();}
  //IPaintCallback
  virtual void Render(const RECT& dst, IDirect3DSurface9* target, int index);
  virtual bool WaitOutput(unsigned int msec);
  virtual bool GetD3DSurfaceFromScheduledSample(int *surface_index);
  virtual int GetReadySample() { return m_ScheduledSamples.GetCount();}
  
  void SetDSMediaType(CMediaType mt);
protected:
  IMFSample *m_gPtr;
  void RenegotiateEVRMediaType();
  void AllocateEVRSurfaces();

  void ResetSyncOffsets();
  void CalcSyncOffsets(int sync);
  void CalcJitter(int jitter);

  double                                GetFrameTime() {if (m_DetectedLock) return m_DetectedFrameTime; return m_rtTimePerFrame / 10000000.0; }
  int64_t                               GetClockTime(int64_t PerformanceCounter);

  int64_t                               m_MaxSampleDuration;
  bool                                  m_bLastSampleOffsetValid;
  int64_t                               m_LastScheduledSampleTime;
  double                                m_LastScheduledSampleTimeFP;
  int64_t                               m_LastScheduledUncorrectedSampleTime;
  double                                m_TimeChangeHistory[100];
  double                                m_ClockChangeHistory[100];
  int                                   m_ClockTimeChangeHistoryPos;
  double                                m_ModeratedTimeSpeed;
  double                                m_ModeratedTimeSpeedPrim;
  double                                m_ModeratedTimeSpeedDiff;
  uint32_t                              m_RefreshRate;
  int                                   m_OrderedPaint;
  uint8_t                               m_VSyncMode;
  int64_t                               m_PaintTime;
  int64_t                               m_PaintTimeMin;
  int64_t                               m_PaintTimeMax;
  long                                  m_nUsedBuffer;
  CCritSec                              m_RefreshRateLock;
  bool                                  m_DetectedLock;
  double                                m_DetectedFrameTime;
  double                                m_DetectedFrameTimeStdDev;
  double                                m_DetectedRefreshTime;
  double                                m_DetectedRefreshTimePrim;
  double                                m_DetectedScanlineTime;
  double                                m_DetectedScanlineTimePrim;
  double                                m_DetectedScanlinesPerFrame;

  double                                m_ldDetectedRefreshRateList[100];
  double                                m_ldDetectedScanlineRateList[100];
  int                                   m_DetectedRefreshRatePos;

  int64_t                               m_LastFrameDuration;
  int64_t                               m_LastSampleTime;
  int                                   m_FrameTimeCorrection;
  int64_t                               m_LastPredictedSync;
  int64_t                               m_VSyncOffsetHistory[5];
  int                                  m_VSyncOffsetHistoryPos;

  Com::SmartSize                        m_ScreenSize;
  Com::SmartRect                        m_pScreenSize;
  vector<IDirect3DSurface9*> m_pSurfaces;
  vector<IDirect3DTexture9*> m_pTextures;
  IDirect3DSurface9* m_pSurfaceRenderTarget;
  IDirect3DTexture9* m_pTextureRenderTarget;
  LONGLONG          m_iLastSampleDuration;
  LONGLONG          m_iLastSampleTime;
  int               m_bInterlaced;
  float             m_fps;
  //CCritSec objCritSec;
  IVMRSurfaceAllocatorNotify9* surfallocnotify;
  void CleanupSurfaces();
  LONG refcount;
  DWORD vheight;
  DWORD vwidth;
  bool inevrmode;
  bool endofstream;

  Com::SmartPtr<IMFTransform> m_pMixer;
  Com::SmartPtr<IMediaEventSink> m_pSink;
  Com::SmartPtr<IMFClock> m_pClock;
  Com::SmartPtr<IMFMediaType> m_pMediaType;

  //IDirect3DDevice9* m_pD3DDevice;
  IDSInfoCallback* m_pCallback;
  IDirect3DDeviceManager9* m_pD3DDevManager;
  static const int n_stats=126;
  int sync_offset[n_stats];
  int jitter_offset[n_stats];
  unsigned int sync_pos;
  unsigned int jitter_pos;
  int framesdrawn;
  int framesdropped;
  int avg_sync_offset;
  int dev_sync_offset;
  int jitter;
  int avgfps;
  LONGLONG lastdelframe;
  CMutex* m_mutex;

  void                     GetMixerThread();
  static DWORD WINAPI      GetMixerThreadStatic(LPVOID lpParam);

  bool                     GetImageFromMixer();
  void                     RenderThread();
  static DWORD WINAPI      PresentThread(LPVOID lpParam);
  void                     StartWorkerThreads();
  void                     StopWorkerThreads();
  HANDLE                   m_hEvtQuit;      // Stop rendering thread event
  bool                     m_bEvtQuit;
  HANDLE                   m_hEvtFlush;    // Discard all buffers
  bool                     m_bEvtFlush;

  void                     RemoveAllSamples();
  HRESULT                  GetFreeSample(IMFSample** ppSample);
  HRESULT                  GetScheduledSample(IMFSample** ppSample, int &_Count);
  void                     MoveToFreeList(IMFSample* pSample, bool bTail);
  void                     MoveToScheduledList(IMFSample* pSample, bool _bSorted);
  void                     FlushSamples();
  void                     FlushSamplesInternal();
  void                     ResetStats();
private:
  typedef enum
  {
    Started = State_Running,
    Stopped = State_Stopped,
    Paused = State_Paused,
    Shutdown = State_Running + 1
  } RENDER_STATE;
  RENDER_STATE             m_nRenderState;
  HANDLE                   m_hThread;
  HANDLE                   m_hGetMixerThread;

  int64_t                  m_rtTimePerFrame;

  int64_t                  m_LastSampleOffset;

  double                   m_ModeratedTime;
  int64_t                  m_ModeratedTimeLast;
  int64_t                  m_ModeratedClockLast;
  int64_t                  m_ModeratedTimer;
  MFCLOCK_STATE            m_LastClockState;

  bool                     m_bSignaledStarvation; 
  int64_t                  m_StarvationClock;

  VideoSampleList          m_FreeSamples;
  VideoSampleList          m_ScheduledSamples;
  VideoSampleList          m_BusySamples;

  //Com::CSyncPtrQueue<IMFSample> m_FreeSamples;
  //Com::CSyncPtrQueue<IMFSample> m_ScheduledSamples;
  CEvent                     m_ready_event;
  CCritSec                   m_section;
  //CCritSec                 m_SampleQueueLock;
  //CCritSec                 m_ImageProcessingLock;
  //CCritSec                 m_DisplaydSampleQueueLock;
  bool                     m_bPendingRenegotiate;
  bool                     m_bPendingMediaFinished;
  bool                     m_bCorrectedFrameTime;
  double                   m_DetectedFrameRate;
  int                      m_DetectedFrameTimePos;
  int64_t                  m_DetectedFrameTimeHistory[60];
  double                   m_DetectedFrameTimeHistoryHistory[500];

  long                     m_LastSetOutputRange;
  std::queue<IMFSample *>  m_pCurrentDisplaydSampleQueue;
  IMFSample *              m_pCurrentDisplaydSample;
  bool                     m_bWaitingSample;
  int                      m_nCurSurface;
  int                      m_nStepCount;
  UINT                     m_pcFrames;
  UINT                     m_nDroppedUpdate;
  UINT                     m_pcFramesDrawn;  // Retrieves the number of frames drawn since streaming started
  UINT                     m_piAvg;
  UINT                     m_piDev;
  
  double                   m_fAvrFps;               // Estimate the real FPS
  double                   m_fJitterStdDev;         // Estimate the Jitter std dev
  double                   m_fJitterMean;
  double                   m_fSyncOffsetStdDev;
  double                   m_fSyncOffsetAvr;
};

#endif
