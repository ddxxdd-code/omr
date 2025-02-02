/*******************************************************************************
 * Copyright IBM Corp. and others 2000
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
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0-only WITH Classpath-exception-2.0 OR GPL-2.0-only WITH OpenJDK-assembly-exception-1.0
 *******************************************************************************/

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "codegen/CodeGenPhase.hpp"
#include "codegen/CodeGenerator.hpp"
#include "compile/Compilation.hpp"
#include "env/TRMemory.hpp"
#include "il/Block.hpp"
#include "il/DataTypes.hpp"
#include "il/LabelSymbol.hpp"
#include "il/ResolvedMethodSymbol.hpp"
#include "il/StaticSymbol.hpp"
#include "il/Symbol.hpp"
#include "infra/Assert.hpp"
#include "ras/Debug.hpp"

namespace TR { class ParameterSymbol; }
namespace TR { class Snippet; }
template <class T> class List;

TR::LabelSymbol *
OMR::LabelSymbol::self()
   {
   return static_cast<TR::LabelSymbol*>(this);
   }

template <typename AllocatorType>
TR::LabelSymbol * OMR::LabelSymbol::create(AllocatorType t, TR::CodeGenerator *cg)
   {
   return new (t) TR::LabelSymbol(cg);
   }

template <typename AllocatorType>
TR::LabelSymbol * OMR::LabelSymbol::create(AllocatorType t, TR::CodeGenerator *cg, TR::Block *b)
   {
   return new (t) TR::LabelSymbol(cg, b);
   }

OMR::LabelSymbol::LabelSymbol(TR::CodeGenerator *cg) :
   TR::Symbol(),
   _instruction(NULL),
   _codeLocation(NULL),
   _estimatedCodeLocation(0),
   _snippet(NULL),
   _directlyTargeted(false)
   {
   self()->setIsLabel();

   TR_Debug *debug = cg->comp()->getDebug();
   if (debug)
      debug->newLabelSymbol(self());
   }

OMR::LabelSymbol::LabelSymbol(TR::CodeGenerator *cg, TR::Block *labb) :
   TR::Symbol(),
   _instruction(NULL),
   _codeLocation(NULL),
   _estimatedCodeLocation(0),
   _snippet(NULL),
   _directlyTargeted(false)
   {
   self()->setIsLabel();

   TR_Debug *debug = cg->comp()->getDebug();
   if (debug)
      debug->newLabelSymbol(self());
   }

TR_YesNoMaybe
OMR::LabelSymbol::isTargeted(TR::CodeGenerator *cg)
   {
   if (cg->getCodeGeneratorPhase() <= TR::CodeGenPhase::InstructionSelectionPhase)
      return TR_maybe;
   return _directlyTargeted ? TR_yes : TR_no;
   }

const char *
OMR::LabelSymbol::getName(TR_Debug *debug)
   {
   if (debug)
      return debug->getName(self());
   else
      return "<unknown labelsym>";
   }

void
OMR::LabelSymbol::makeRelativeLabelSymbol(intptr_t offset)
   {
   // Is this assert here purely to ensure that the label size doesn't blow the buffer?
   TR_ASSERT(offset*2 > -9999999 && offset*2 < +9999999, "assertion failure");

   self()->setRelativeLabel();
   _offset = offset;
   char * name = (char*)calloc(10,sizeof(char));  // FIXME: Leaked.
   sprintf(name, "%d", (int)(offset*2));
   self()->setName(name);
   }

intptr_t
OMR::LabelSymbol::getDistance()
   {
   TR_ASSERT(self()->isRelativeLabel(), "Must be a relative label to have a valid offset!");
   return _offset;
   }

TR::LabelSymbol *
generateLabelSymbol(TR::CodeGenerator *cg)
   {
   return TR::LabelSymbol::create(cg->trHeapMemory(), cg);
   }

template <typename AllocatorType>
TR::LabelSymbol *
OMR::LabelSymbol::createRelativeLabel(AllocatorType m, TR::CodeGenerator * cg, intptr_t offset)
   {
   TR::LabelSymbol * rel = new (m) TR::LabelSymbol(cg);
   rel->makeRelativeLabelSymbol(offset);
   return rel;
   }

// Explicit instantiation
template TR::LabelSymbol * OMR::LabelSymbol::create(TR_HeapMemory t, TR::CodeGenerator* cg);
template TR::LabelSymbol * OMR::LabelSymbol::create(TR_HeapMemory t, TR::CodeGenerator* cg, TR::Block* b);
template TR::LabelSymbol * OMR::LabelSymbol::createRelativeLabel(TR_HeapMemory m, TR::CodeGenerator* cg, intptr_t offset);

template TR::LabelSymbol * OMR::LabelSymbol::create(TR_StackMemory t, TR::CodeGenerator* cg);
template TR::LabelSymbol * OMR::LabelSymbol::create(TR_StackMemory t, TR::CodeGenerator* cg, TR::Block* b);
template TR::LabelSymbol * OMR::LabelSymbol::createRelativeLabel(TR_StackMemory m, TR::CodeGenerator* cg, intptr_t offset);
