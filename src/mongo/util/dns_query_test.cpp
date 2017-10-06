#include "mongo/util/dns_query.h"

#include "mongo/unittest/unittest.h"

namespace
{
TEST( MongoDnsQuery, srv )
{
	auto entries= mongo::dns::getSRVRecord( "test5.test.build.10gen.cc" );
	std::sort( begin( entries ), end( entries ) );

	const std::vector< mongo::dns::SRVHostEntry > witness=
		{
			{ "localhost", 27017 },
			{ "localhost", 27018 },
			{ "localhost", 27019 },
		};

	//witness.front() == entries.front();

	ASSERT_TRUE( witness.size() == entries.size() );
	ASSERT_TRUE( std::equal( begin( witness ), end( witness ), begin( entries ) ) );
}

TEST( MongoDnsQuery, txt )
{
}

}//namespace
