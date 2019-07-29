/**
 *    Copyright (C) 2018-present MongoDB, Inc.
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

#include "mongo/stdx/trusted_hasher.h"

#include <absl/container/node_hash_map.h>

namespace mongo ::stdx{
template <typename Key, typename Value, typename Hasher, typename... Args>
class unordered_map;
}
namespace std
{
	template <typename Key, typename Value, typename Hasher, typename... Args>
	void
	swap( unordered_map< Key, Value, Hasher, Args... > &a, unordered_map< Key, Value, Hasher, Args... > &b );
}

namespace mongo::stdx{

template <typename Key, typename Value, typename Hasher = DefaultHasher<Key>, typename... Args>
class unordered_map : absl::node_hash_map<Key, Value, EnsureTrustedHasher<Hasher, Key>, Args...>
{
	private:
		using Parent= absl::node_hash_map<Key, Value, EnsureTrustedHasher<Hasher, Key>, Args...>;

	public:
		Value &
		at( Key k )
		{
			const auto found= this->find( k );
			if( found == this->end() ) throw std::out_of_range("Did not find key in unordered map");
			return found->second;
		}

		const Value &
		at( Key k ) const
		{
			const auto found= this->find( k );
			if( found == this->end() ) throw std::out_of_range("Did not find key in unordered map");
			return found->second;
		}

		using typename Parent::key_type;
		using typename Parent::mapped_type;
		using typename Parent::value_type;
		using typename Parent::size_type;
		using typename Parent::difference_type;
		using typename Parent::hasher;
		using typename Parent::key_equal;
		using typename Parent::allocator_type;
		using typename Parent::reference;
		using typename Parent::const_reference;
		using typename Parent::pointer;
		using typename Parent::const_pointer;
		using typename Parent::iterator;
		using typename Parent::const_iterator;
		using typename Parent::node_type;

		// using typename Parent::insert_return_type; // Absent in abseil
		// using typename Parent::local_iterator; // Absent in abseil
		// using typename Parent::const_local_iterator; // Absent in abseil

		using Parent::Parent;
		using Parent::operator=;
		using Parent::get_allocator;

		using Parent::begin;
		using Parent::cbegin;

		using Parent::end;
		using Parent::cend;

		using Parent::empty;
		using Parent::size;
		using Parent::max_size;

		using Parent::clear;
		using Parent::insert;
		using Parent::insert_or_assign;
		using Parent::emplace;
		using Parent::emplace_hint;
		using Parent::try_emplace;
		using Parent::erase;
		using Parent::swap;
		using Parent::extract;
		using Parent::merge;

		// Do not import Parent::at.  We're patching it's behaviour.
		using Parent::operator[];
		using Parent::count;
		using Parent::find;
		using Parent::contains;
		using Parent::equal_range;

		using Parent::bucket_count;
		// using Parent::max_bucket_count; // Absent in abseil
		// using Parent::bucket_size; // Absent in abseil
		// using Parent::bucket; // Absent in abseil

		using Parent::load_factor;
		using Parent::max_load_factor;
		using Parent::rehash;
		using Parent::reserve;

		using Parent::hash_function;
		using Parent::key_eq;

		friend bool
		operator == ( const unordered_map &lhs, const unordered_map &rhs )
				noexcept( noexcept( static_cast< const Parent & >( lhs ) == static_cast< const Parent & >( rhs ) ) )
		{
			return static_cast< const Parent & >( lhs ) == static_cast< const Parent & >( rhs );
		}

		friend bool
		operator != ( const unordered_map &lhs, const unordered_map &rhs )
				noexcept( noexcept( static_cast< const Parent & >( lhs ) != static_cast< const Parent & >( rhs ) ) )
		{
			return static_cast< const Parent & >( lhs ) != static_cast< const Parent & >( rhs );
		}

	template <typename Key_, typename Value_, typename Hasher_, typename... Args_>
	friend void
	std::swap( unordered_map< Key_, Value_, Hasher_, Args_... > &a, unordered_map< Key_, Value_, Hasher_, Args_... > &b );
};
}

namespace std
{
	template <typename Key, typename Value, typename Hasher, typename... Args>
	void
	swap( unordered_map< Key, Value, Hasher, Args... > &a_, unordered_map< Key, Value, Hasher, Args... > &b_ )
	{
		auto &a= static_cast< typename std::decay_t< decltype( a_ ) >::Parent & >( a_ );
		auto &b= static_cast< typename std::decay_t< decltype( a_ ) >::Parent & >( b_ );

		::std::swap( a, b );
	}
}

