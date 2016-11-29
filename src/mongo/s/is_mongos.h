/*
 *    Copyright (C) 2016 10gen Inc.
 *
 *    This program is free software: you can redistribute it and/or  modify
 *    it under the terms of the GNU Affero General Public License, version 3,
 *    as published by the Free Software Foundation.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU Affero General Public License for more details.
 *
 *    You should have received a copy of the GNU Affero General Public License
 *    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the GNU Affero General Public License in all respects
 *    for all of the code used other than as permitted herein. If you modify
 *    file(s) with this exception, you may extend this exception to your
 *    version of the file(s), but you are not obligated to do so. If you do not
 *    wish to do so, delete this exception statement from your version. If you
 *    delete this exception statement from all source files in the program,
 *    then also delete it in the license file.
 */

#pragma once

namespace mongo
{

	bool isMongos();
	void setMongos( const bool state= true );
}

#if 0
namespace mongo
{

	// TODO(adam.martin): Move to the InlineVariable idiom, if it gets committed.
	namespace mongos_boolean_variable_detail
	{
		template< typename= void >
		struct inline_variable_hack
		{
			static bool mongosState;
		};

		template< typename T > bool inline_variable_hack< T >::mongosState= false;
	}
}

// This function should eventually go away, but needs to be here now because the sorter and
// the version manager must know at runtime which binary it is in.
inline bool
mongo::isMongos()
{
	return mongos_boolean_variable_detail::inline_variable_hack<>::mongosState;
}

// This function should eventually go away, but needs to be here now because the mongos binary and various test drivers must be able to set the mongos state.
// the version manager must know at runtime which binary it is in.
inline void
mongo::setMongos( const bool state= true )
{
	mongos_boolean_variable_detail::inline_variable_hack<>::mongosState= state;
}
#endif
