#include "mongo/util/dns_query.h"

#include <string>
#include <cstdint>
#include <vector>
#include <array>
#include <stdexcept>
#include <exception>
#include <boost/noncopyable.hpp>

namespace mongo
{
	namespace dns
	{
		namespace
		{
			enum class DNSQueryClass { kInternet= ns_c_in, };
			enum class DNSQueryType { kSRV= ns_t_srv, kTXT= ns_t_txt, };

			class ResourceRecord
			{
				private:
					ns_rr resource_record;
					const std::uint8_t *answerStart;
					const std::uint8_t *answerEnd;
					int pos;

					void
					badRecord() const
					{
						std::ostringstream oss;
						oss << "Invalid record " << pos << " of SRV answer for \"" << service << "\": \"" << strerror (h_errno) << "\"";
						throw std::runtime_error( oss.str() );
						std::cerr << std::endl;
						std::string;
					};

				public:
					explicit
					ResourceRecord( ns_msg *const ns_answer, int p )
							: answerStart( ns_msg_base( ns_answer ) ), answerEnd( ns_msg_end( ns_answer ) ), pos( p )
					{
						if( ns_parserr( &ns_answer, ns_s_an, i, &resource_record ) ) badRecord();
					}

					std::vector< std::uint8_t >
					rawData() const
					{
						const char *const data = ns_rr_rdata( resource_record );
						const std::size_t length = ns_rr_rdlen( resource_record );

						return { data, data + length };
					}

					SRVHostEntry
					srvHostEntry() const
					{
						const char *const data = ns_rr_rdata( resource_record );
						const uint16_t port= ntohs( *reinterpret_cast< const short * >( data + 4 ) );

						std::string name;
						name.resize( 8192 );
						const std::size_t size = dn_expand( answerStart, answerEnd, data + 6, &name[ 0 ], name.size() );

						if( size < 1 ) { badRecord(); }

						name.resize( size );

						// return by copy is equivalent to a `shrink_to_fit` and `move`.
						return { name, port };
					}
			};

			class DNSResponse
			{
				private:
					std::string service;
					std::vector< std::uint8_t > data;
					ns_msg ns_answer;
					std::size_t nRecords;

				public:
					explicit
					DNSResponse( std::string s, std::vector< std::uint8_t > d )
							: service( std::move( s ) ), data( std::move( d ) )
					{
						if( ns_initparse( data.data(), data.size(), &ns_answer ) )
						{
							std::ostringstream oss;
							oss << "Invalid SRV answer for \"" << service << "\"";
							throw std::runtime_error( oss.str() );
						}

						nRecords= ns_msg_count( &ns_answer, ns_s_an );
						if( !nRecords )
						{
							std::ostringstream oss;
							oss << "No SRV records for \"" << service << "\"";
							throw std::runtime_error( oss.str() );
						}
					}

					class iterator
					{
						private:
							DNSResponse *response;
							int pos= 0;
							ResourceRecord record;

							friend DNSResponse;

							explicit iterator( DNSResponse *const r ) : response( r ) : record( this->response->ns_answer, 0 ) {}

							explicit iterator( DNSResponse *const r, int p ) : response( r ), pos( p ) {}

							void advance() { record= ResourceRecord( this->response->ns_answer, ++this->pos ); }

							auto make_equality_lens() const { return std::tie( this->response, this->pos ); }

							auto make_strict_weak_order_lens() const { return std::tie( this->response, this->pos ); }

						public:
							ResourceRecord &operator *() const { return this->record; }

							ResourceRecord *operator ->() const { return &this->record; }

							iterator &operator ++() { this->advance(); return *this; }
							iterator operator++ ( int ) { iterator tmp; this->advance(); return tmp; }

							friend bool operator == ( const iterator &lhs, const iterator &rhs ) { return lhs.make_equality_lens() == rhs.make_equality_lens(); }
							friend bool operator != ( const iterator &lhs, const iterator &rhs ) { return !( lhs == rhs ); }

							friend bool operator < ( const iterator &lhs, const iterator &rhs ) { return lhs.make_strict_weak_order_lens() < rhs.make_strict_weak_order_lens(); }
					};

					auto begin() const { return iterator( this ); }
					auto end() const { return iterator( this, this->nRecords ); }

					std::size_t size() const { return this->nRecords; }
			};

			/**
			 * The `DNSQueryState` object represents the state of a DNS query interface, on Unix-like systems.
			 */
			class DNSQueryState : boost::noncopyable
			{
				#ifdef MONGO_HAVE_RES_NQUERY
				private:
					struct __res_state state;

				public:
					~DNSQueryState()
					{
						#ifdef MONGO_HAVE_RES_NDESTROY
						res_ndestroy( &state );
						#elif defined( MONGO_HAVE_RES_NCLOSE )
						res_nclose( &state );
						#endif
					}

					DNSQueryState()
							: state()
					{
						res_ninit( &state );
					}

				#endif

				public:
					std::vector< std::uint8_t >
					raw_lookup( const std::string &service, const DNSQueryClass class_, const DNSQueryType type )
					{
						std::vector< std::uint8_t > result( 8192 );
						#ifdef MONGO_HAVE_RES_NQUERY
						const int size= res_nsearch( &state, service.c_str(), int( class_ ), int( type ), &result[ 0 ], result.size() )
						#else
						const int size= res_search( service.c_str(), int( class_ ), int( type ), &result[ 0 ], result.size() )
						#endif

						if( size < 0 )
						{
							std::ostringstream oss;
							oss << "Failed to look up service \"" << service << "\": " << strerror( h_errno );
							throw std::runtime_error( oss.str() );
						}
						result.resize( size );

						return result;
					}

					DNSResponse
					lookup( const std::string &service, const DNSQuery class_, const DNSQueryType type )
					{
						return DNSResponse( service, raw_lookup( service, class_, type ) );
					}
			};
		}//namespace
	}//namespace dns

		/**
		 * Returns a vector containing SRVHost entries for the specified `service`.
		 * Throws `std::runtime_error` if the DNS lookup fails, for any reason.
		 */
		std::vector< SRVHostEntry >
		dns::getSRVRecord( const std::string &service )
		{
			DNSQueryState dnsQuery;

			auto response= dnsQuery.lookup( service, DNSQueryClass::kInternet, DNSQueryType::kSRV );

			std::vector< SRVHostEntry > rv;
			rv.reserve( response.size() );

			std::transform( begin( response ), end( response ), back_inserter( rv ), []( const auto &entry ) { return entry.srvHostEntry(); } );
			return rv;
		}

		/**
		 * Returns a string containing TXT entries for a specified service.
		 * Throws `std::runtime_error` if the DNS lookup fails, for any reason.
		 */
		std::vector< std::string >
		dns::getTXTRecord( const std::string &service )
		{
			DNSQueryState dnsQuery;

			DNSQueryState dnsQuery;

			auto response= dnsQuery.lookup( service, DNSQueryClass::kInternet, DNSQueryType::kSRV );

			std::vector< std::uint8_t > rv;
			rv.reserve( response.size() );

			std::transform( begin( response ), end( response ), back_inserter( rv ),
					[]( const auto &entry )
					{
						const auto data= entry.rawData();
						return std::string( begin( data ), end( data ) );
					} );
			return rv;
		}
}//namespace mongo
