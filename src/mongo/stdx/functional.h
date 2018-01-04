/**
 *    Copyright (C) 2014 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <bitset>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

#include <boost/functional/hash.hpp>
#include <boost/optional.hpp>
#include <boost/mpl/contains.hpp>
#include <boost/mpl/list.hpp>

#include "mongo/stdx/thread.h"

namespace mongo
{
    namespace stdx
    {
        using ::std::bind;                             // NOLINT
        using ::std::cref;                             // NOLINT
        using ::std::function;                         // NOLINT
        using ::std::ref;                              // NOLINT
        namespace placeholders= ::std::placeholders;  // NOLINT

        namespace hash_detail
        {
            #if !defined( _WIN32 )
            namespace hash_namespace= ::std; //NOLINT

            #else
            namespace hash_namespace= ::boost; //NOLINT
            #endif

            using required_types= std::tuple< bool, char, signed char, unsigned char, char16_t, char32_t, wchar_t, short, unsigned short, int, unsigned int, long, long long, unsigned long,
					unsigned long long, float, double, long double, ::std::string, ::std::u16string, ::std::u32string, ::std::wstring, ::std::error_code, ::std::vector< bool >, ::mongo::stdx::thread::id,
					::std::type_index >; 

            template< typename T, typename Container >
            struct is_part_of;

            template< typename T >
            struct is_part_of< T, std::tuple<> > : std::false_type {};

            template< typename T, typename U >
            struct is_part_of< T, std::tuple< U > > : std::false_type {};

            template< typename T >
            struct is_part_of< T, std::tuple< T > > : std::true_type {};

            template< typename T, typename ... Args >
            struct is_part_of< T, std::tuple< T, Args... > > : std::true_type {};

            template< typename T, typename U, typename ... Args >
            struct is_part_of< T, std::tuple< U, Args... > > : is_part_of< T, std::tuple< Args... > > {};

			template< typename T, bool required= is_part_of< T, required_types >::value || std::is_enum< T >::value >
			struct hash_selector_special;

			// For special types requird by the standard, including enums, use the library definition.
			template< typename T >
			struct hash_selector_special< T, true >
			{
				using type= hash_namespace::hash< T >;
			};

			// For anything that isn't an enum, and isn't one of the standard-required specialized types, reach into the std namespace.
			template< typename T >
			struct hash_selector_special< T, false >
			{
				using type= ::std::hash< T >;
			};

			template< typename T >
			struct hash_selector;

			// Use the library hasher if it is a raw pointer.
			template< typename T >
			struct hash_selector< T * >
			{
				using type= hash_namespace::hash< T * >;
			};

			// Use the library hasher if it is an unique_ptr
			template< typename T >
			struct hash_selector< std::unique_ptr< T > >
			{
				using type= hash_namespace::hash< std::unique_ptr< T > >;
			};

			// Use the library hasher if it is a shared_ptr
			template< typename T >
			struct hash_selector< std::shared_ptr< T > >
			{
				using type= hash_namespace::hash< std::shared_ptr< T > >;
			};

			// Use the boost hasher for the boost optional
			template< typename T >
			struct hash_selector< boost::optional< T > >
			{
				using type= boost::hash< boost::optional< T > >;
			};

			// Use the library hasher if it is a std::bitset
			template< std::size_t sz >
			struct hash_selector< std::bitset< sz > >
			{
				using type= hash_namespace::hash< std::bitset< sz > >;
			};

			// For everything else, use the specialized selector system.
			template< typename T >
			struct hash_selector : hash_selector_special< T > {};
        }  // namespace hash_detail

		template< typename T >
		using hash= typename hash_detail::hash_selector< T >::type;
    }  // namespace stdx
}  // namespace mongo
