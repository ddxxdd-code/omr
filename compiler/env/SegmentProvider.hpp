/*******************************************************************************
 * Copyright (c) 2000, 2017 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at http://eclipse.org/legal/epl-2.0
 * or the Apache License, Version 2.0 which accompanies this distribution
 * and is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following Secondary
 * Licenses when the conditions for such availability set forth in the
 * Eclipse Public License, v. 2.0 are satisfied: GNU General Public License,
 * version 2 with the GNU Classpath Exception [1] and GNU General Public
 * License, version 2 with the OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

#ifndef TR_SEGMENT_PROVIDER
#define TR_SEGMENT_PROVIDER

#pragma once

#include <stddef.h>
#include <new>

class regionLog;

namespace TR {

class MemorySegment;

class SegmentProvider
   {
public:
   virtual TR::MemorySegment& request(size_t requiredSize) = 0;
   virtual void release(TR::MemorySegment& segment) throw() = 0;
   size_t defaultSegmentSize() { return _defaultSegmentSize; }
   virtual size_t bytesAllocated() const throw() = 0;
   // new counters involved in segment provider
   virtual size_t regionBytesInUse() const throw() = 0;
   virtual size_t regionRealBytesInUse() const throw() = 0;
   void setCollectRegionLog();   // called to set a segment provider to enable region log collection
   uint32_t recordEvent() = 0;   // called on creation and destructor of region
   bool collectRegions() = 0;    // called in constructor of region to check if region should be allocated
   // head and tail for the double linked list for regionlogs.
   regionLog *_regionLogListHead = NULL;
   regionLog *_regionLogListTail = NULL;


protected:
   explicit SegmentProvider(size_t defaultSegmentSize) :
      _defaultSegmentSize(defaultSegmentSize)
      {
      }

   SegmentProvider(const SegmentProvider &other):
      _defaultSegmentSize(other._defaultSegmentSize)
      {
      }

   /*
    * Require knowledge of the concrete class in order to destroy SegmentProviders
    */
   virtual ~SegmentProvider() throw();

   size_t const _defaultSegmentSize;
   };

}

#endif // TR_SEGMENT_PROVIDER
