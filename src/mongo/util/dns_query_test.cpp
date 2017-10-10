#include "mongo/util/dns_query.h"

#include "mongo/unittest/unittest.h"

namespace
{
	TEST( MongoDnsQuery, basic )
	{
		const auto entry= mongo::dns::getARecord( "google-public-dns-a.google.com." );
		std::cout << "Entry: " << entry << std::endl;

		ASSERT_EQ( entry, "8.8.8.8" );
	}

	TEST( MongoDnsQuery, SRVHostEntryLens )
	{
		using mongo::dns::SRVHostEntry;
		SRVHostEntry a{ "Host", 1234 };
		SRVHostEntry b{ "Host", 1234 };
		SRVHostEntry c{ "Host2", 1234 };
		SRVHostEntry d{ "Host", 1233 };
		SRVHostEntry e{ "Host2", 1233 };

		ASSERT_EQ( a, b );
		ASSERT_EQ( b, a );

		ASSERT_NE( a, c );
		ASSERT_NE( c, a );

		ASSERT_NE( a, d );
		ASSERT_NE( d, a );

		ASSERT_NE( a, e );
		ASSERT_NE( e, a );
	}

	TEST( MongoDnsQuery, srv )
	{
		auto entries= mongo::dns::getSRVRecord( "_mongodb._tcp.test1.test.build.10gen.cc." );
		std::sort( begin( entries ), end( entries ) );

		for( const auto &entry: entries )
		{
			std::cout << "Entry: " << entry << std::endl;
		}

		const std::vector< mongo::dns::SRVHostEntry > witness=
			{
				{ "localhost.build.10gen.cc.", 27017 },
				{ "localhost.build.10gen.cc.", 27018 },
			};

		for( std::size_t i= 0; i < witness.size() && i < entries.size(); ++i )
		{
			std::cout << "Witness: " << witness.at( i ) << std::endl;
			std::cout << "Result:  " << entries.at( i ) << std::endl;
			//ASSERT_EQ( witness.at( i ), entries.at( i ) );
		}

		ASSERT_TRUE( std::equal( begin( witness ), end( witness ), begin( entries ), end( entries ) ) );
		ASSERT_TRUE( witness.size() == entries.size() );
	}

#if 0
	TEST( MongoDnsQuery, txt )
	{
		auto entries= mongo::dns::getTXTRecord( "test5.test.build.10gen.cc" );
		std::sort( begin( entries ), end( entries ) );

		for( const auto &entry: entries )
		{
			std::cout << "Entry: " << entry << std::endl;
		}

		const std::vector< std::string > witness= { "connectTimeoutMS=300000&socketTimeoutMS=300000" };

		ASSERT_TRUE( std::equal( begin( witness ), end( witness ), begin( entries ), end( entries ) ) );
		ASSERT_TRUE( witness.size() == entries.size() );
	}
#endif
}//namespace
