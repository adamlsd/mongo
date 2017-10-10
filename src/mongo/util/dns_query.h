#pragma once
#include <string>
#include <cstdint>
#include <vector>
#include <array>
#include <stdexcept>
#include <exception>
#include <iostream>

#include <boost/noncopyable.hpp>

#include "mongo/base/relops.h"

namespace mongo
{
	namespace dns
	{
		/**
		 * An `SRVHostEntry` object represents the information received from a DNS lookup of an SRV record.
		 */
		struct SRVHostEntry : mongo::relops::equality::hook, mongo::relops::order::hook
		{
			std::string host;
			std::uint16_t port;

			SRVHostEntry( std::string i_host, const std::uint16_t i_port )
				: host( std::move( i_host ) ), port( i_port ) {}

			inline friend auto
			make_equality_lens( const SRVHostEntry &entry )
			{
				return std::tie( entry.host, entry.port );
			}

			inline friend auto
			make_strict_weak_order_lens( const SRVHostEntry &entry )
			{
				return std::tie( entry.host, entry.port );
			}

			inline friend std::ostream &
			operator << ( std::ostream &os, const SRVHostEntry &entry )
			{
				os << "(" << entry.host.size() << ")";
				std::copy( begin( entry.host ), end( entry.host ),
						std::ostream_iterator< unsigned >( os, "/" ) );
				os << entry.host << ':' << entry.port;
				return os;
			}
		};

		/**
		 * Returns a vector containing SRVHost entries for the specified `service`.
		 * Throws `std::runtime_error` if the DNS lookup fails, for any reason.
		 */
		std::vector< SRVHostEntry > getSRVRecord( const std::string &service );

		/**
		 * Returns a string containing TXT entries for a specified service.
		 * Throws `std::runtime_error` if the DNS lookup fails, for any reason.
		 */
		std::vector< std::string > getTXTRecord( const std::string &service );

		std::string getARecord( const std::string &service );
	}//namespace dns
}//namespace mongo
