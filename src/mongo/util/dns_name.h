/**
 *    Copyright (C) 2018 MongoDB Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
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

#include <vector>
#include <iterator>
#include <algorithm>
#include <iostream>
#include <sstream>
#include <tuple>

#include "mongo/base/string_data.h"
#include "mongo/base/error_codes.h"
#include "mongo/util/assert_util.h"

namespace mongo
{
	namespace dns
	{
		class HostName
		{
			public:
				enum Qualification : bool { RelativeName= false, FullyQualified= true };

			private:
				// Hostname components are stored in hierarchy order (reverse from how read by humans in text)
				std::vector< std::string > _nameComponents;
				Qualification fullyQualified;

				auto
				make_equality_lens() const
				{
					return std::tie( fullyQualified, _nameComponents );
				}

				void
				streamQualified( std::ostream &os ) const
				{
					invariant( fullyQualified );
					std::copy( rbegin( _nameComponents ), rend( _nameComponents ),
							std::ostream_iterator< std::string >( os, "." ) );
				}

				void
				streamUnqualified( std::ostream &os ) const
				{
					std::for_each( rbegin( _nameComponents ), rend( _nameComponents ),
							[ first= true, &os ] ( const auto &component ) mutable
							{
								if( !first ) os << '.';
								first= false;
								os << component;
							} );
				}

				// If there are exactly 4 name components, and they are not fully qualified, then they cannot be all numbers.
				void
				checkForValidForm() const
				{
					for( const auto &name: _nameComponents )
					{
						if( !isalpha( name[ 0 ] ) ) uasserted( ErrorCodes::DNSRecordTypeMismatch, "A Domain Name subdomain must start with a letter" );
					}

				}

			public:
				template< typename StringIter >
				HostName( const StringIter first, const StringIter second, const Qualification qualification= RelativeName )
						: _nameComponents( first, second ), fullyQualified( qualification )
				{
					if( _nameComponents.empty() ) uasserted( ErrorCodes::DNSRecordTypeMismatch, "A Domain Name cannot have zero name elements" );
					checkForValidForm();
				}

				explicit
				HostName( StringData dnsName )
				{
					if( dnsName.empty() ) uasserted( ErrorCodes::DNSRecordTypeMismatch, "A Domain Name cannot have zero characters" );

					if( dnsName[ 0 ] == '.' ) uasserted( ErrorCodes::DNSRecordTypeMismatch, "A Domain Name cannot start with a '.' character." );

					enum ParserState { NonPeriod= 1, Period= 2 };
					ParserState parserState= NonPeriod;

					std::string name;
					for( const char ch: dnsName )
					{
						if( ch == '.' )
						{
							if( parserState == Period ) uasserted( ErrorCodes::DNSRecordTypeMismatch, "A Domain Name cannot have two adjacent '.' characters" );
							parserState= Period;
							_nameComponents.push_back( std::move( name ) );
							name.clear();
							continue;
						}
						parserState= NonPeriod;

						name.push_back( ch );
					}

					if( parserState == Period ) fullyQualified= FullyQualified;
					else 
					{
						fullyQualified= RelativeName;
						_nameComponents.push_back( std::move( name ) );
					}

					if( _nameComponents.empty() ) uasserted( ErrorCodes::DNSRecordTypeMismatch, "A Domain Name cannot have zero name elements" );

					checkForValidForm();

					// Reverse all the names, once we've parsed them all in.
					std::reverse( begin( _nameComponents ), end( _nameComponents ) );
				}

				bool isFQDN() const { return fullyQualified; }

				void
				forceQualification( const Qualification qualification= FullyQualified )
				{
					fullyQualified= qualification;
				}

				std::string
				canonicalName() const
				{
					std::ostringstream oss;
					oss << *this;
					return oss.str();
				}

				std::string
				sslName() const
				{
					std::ostringstream oss;
					streamUnqualified( oss );
					return oss.str();
				}

				bool
				contains( const HostName &candidate ) const
				{
					return ( fullyQualified == candidate.fullyQualified )
							&& ( _nameComponents.size() < candidate._nameComponents.size() )
							&& std::equal( begin( _nameComponents ), end( _nameComponents ), begin( candidate._nameComponents ) );
				}

				HostName
				resolvedIn( const HostName &rhs ) const
				{
					using std::begin;  using std::end;

					if( this->fullyQualified ) uasserted( ErrorCodes::DNSRecordTypeMismatch, "A fully qualified Domain Name cannot be resolved within another domain name." );
					HostName result= rhs;
					result._nameComponents.insert( end( result._nameComponents ), begin( this->_nameComponents ), end( this->_nameComponents ) );

					return result;
				}

				const std::vector< std::string > &nameComponents() const { return this->_nameComponents; }

				friend bool
				operator == ( const HostName &lhs, const HostName &rhs )
				{
					return lhs.make_equality_lens() == rhs.make_equality_lens();
				}

				friend bool
				operator != ( const HostName &lhs, const HostName &rhs )
				{
					return !( lhs == rhs );
				}

				friend std::ostream &
				operator << ( std::ostream &os, const HostName &hostName )
				{
					if( hostName.fullyQualified )
					{
						hostName.streamQualified( os );
					}
					else
					{
						hostName.streamUnqualified( os );
					}

					return os;
				}
		};
	}//namespace dns
}//namespace mongo
