/*******************************************************************************
 * Copyright IBM Corp. and others 2015
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

#ifndef SCANNER_HPP
#define SCANNER_HPP

#include "ddr/error.hpp"
#include "ddr/std/string.hpp"

#include "omrport.h"

#include <set>
#include <vector>

class ClassUDT;
class EnumUDT;
class NamespaceUDT;
class Symbol_IR;
class Type;
class TypedefUDT;
class UnionUDT;

using std::set;
using std::string;
using std::vector;

class Scanner
{
public:
	virtual DDR_RC startScan(OMRPortLibrary *portLibrary, Symbol_IR *ir,
			vector<string> *debugFiles, const char *excludesFilePath) = 0;

protected:
	set<string> _excludedFiles;
	set<string> _excludedTypes;

	bool checkExcludedType(const string &name) const;
	bool checkExcludedFile(const string &name) const;
	DDR_RC loadExcludesFile(OMRPortLibrary *portLibrary, const char *file);
};

#endif /* SCANNER_HPP */
