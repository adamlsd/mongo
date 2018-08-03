/**
 *    Copyright 2018 MongoDB, Inc.
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

#ifndef _MSC_VER

#include "third_party/function2-3.0.0/function2.hpp"

namespace mongo
{
	using ::fu2::unique_function;
}//namespace mongo

#else

#include <functional>

namespace mongo
{
	template< typename Function >
	class unique_function;

	template< typename RetType, typename ... Args >
	class unique_function< RetType ( Args... ) >
	{
		private:
			struct Impl
			{
				virtual ~Impl()= default;
				virtual RetType call( Args &&... )= 0;
			};

			template< typename Functor >
			static std::unique_ptr< Impl >
			makeImpl( Functor functor )
			{
				class SpecificImpl : public Impl
				{
					private:
						Functor f;

					public:
						explicit SpecificImpl( Functor f ) : f( std::move( f ) ) {}

						RetType call( Args &&... args ) override { return f( args... ); }
				};

				return std::make_unique< SpecificImpl >( std::move( functor ) );
			}

			std::unique_ptr< Impl > impl;

		public:
			unique_function()= default;

			template< typename Functor >
			unique_function( Functor functor )
				: impl( makeImpl( std::move( functor ) ) ) {}

			RetType
			operator() ( Args &&... args ) const
			{
				if( !this->impl.get() ) throw std::bad_function_call();
				return this->impl->call( std::forward< Args >( args )... );
			}

			explicit operator bool () const { return this->impl.get(); }
	};
} //namespace mongo

#endif
