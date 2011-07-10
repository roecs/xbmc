/*
 *      Copyright (C) 2005-2011 Team XBMC
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

#pragma once

#include <boost/thread/recursive_mutex.hpp>

namespace XbmcThreads
{
  /**
   * This template will take any implementation of the "Lockable" concept
   * and allow it to be used as an "Exitable Lockable."
   *
   * Something that implements the "Lockable concept" simply means that 
   * it has the three methods:
   *
   *   lock();
   *   try_lock();
   *   unlock();
   *
   * "Exitable" specifially means that, no matter how deep the recursion
   * on the mutex/critical section, we can exit from it and then restore
   * the state.
   *
   * This requires us to extend the Lockable so that we can keep track of the
   * number of locks that have been recursively acquired so that we can
   * undo it, and then restore that (See class CSingleExit).
   *
   * All xbmc code expects Lockables to be recursive.
   */
  template<class L> class CountingLockable
  {
  protected:
    L mutex;
    unsigned int count;

  public:
    inline CountingLockable() : count(0) {}

    // boost::thread Lockable concept
    inline void lock() { mutex.lock(); count++; }
    inline bool try_lock() { return mutex.try_lock() ? count++, true : false; }
    inline void unlock() { count--; mutex.unlock(); }

    /**
     * This implements the "exitable" behavior mentioned above.
     */
    inline unsigned int exit() 
    { 
      // it's possibe we don't actually own the lock
      // so we will try it.
      unsigned int ret = 0;
      if (try_lock())
      {
        ret = count - 1;  // The -1 is because we don't want 
        //  to count the try_lock increment.
        while (count > 0) // This will also unlock the try_lock.
          unlock();
      }

      return ret; 
    }

    /**
     * Restore a previous exit to the provided level.
     */
    inline void restore(unsigned int restoreCount)
    {
      for (unsigned int i = 0; i < restoreCount; i++) 
        lock();
    }

    inline unsigned int getCount() { return count; }

    inline L& getLockable() { return mutex; }
  };
}

/**
 * A CCriticalSection is a CountingLockable whose implementation is a boost
 *  recursive_mutex.
 *
 * This is not a typedef because of a number of "class CCriticalSection;" 
 *  forward declarations in the code that break when it's done that way.
 */
class CCriticalSection : public XbmcThreads::CountingLockable<boost::recursive_mutex> {};

