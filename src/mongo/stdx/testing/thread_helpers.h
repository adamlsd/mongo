/**
 *    Copyright (C) 2019-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include "mongo/stdx/thread.h"

namespace mongo::stdx::testing
{
	class ThreadInformationListener
	{
		private:
			mutable std::mutex mtx;
			std::map< std::thread::id, mongo::ThreadInformation > mapping;

		public:
			~ThreadInformationListener()
			{
				resetThreadInformationHandler();
			}

			ThreadInformationListener()
			{
				registerThreadInformationHandlerType( *this );
			}
		
			void
			report( const std::thread::id id, const mongo::ThreadInformation info )
			{
				const auto lk= std::lock_guard( mtx );
				assert( !mapping.count( id ) );
				mapping[ id ]= info;
				std::cerr << "Mapping installed: " << mapping.count( id ) << std::endl;
				std::cerr << "Identifier: " << id << std::endl;
			}

			void
			retire( std::thread::id id )
			{
				const auto lk= std::lock_guard( mtx );
				std::cerr << "Reaping identifier: " << id << std::endl;
				mapping.erase( id );
			}

			mongo::ThreadInformation
			getMapping( const stdx::thread::id &id ) const
			{
				const auto lk= std::lock_guard( mtx );
				std::cerr << "Identifier: " << id << std::endl;
				return mapping.at( id );
			}

			mongo::ThreadInformation
			getMapping( const stdx::thread &thr ) const
			{
				return getMapping( thr.get_id() );
			}
	};
}
