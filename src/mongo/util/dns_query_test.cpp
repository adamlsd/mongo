#include "mongo/util/dns_query.h"

#include "mongo/unittest/unittest.h"

using namespace std::literals::string_literals;

namespace
{
	TEST( MongoDnsQuery, basic )
	{
        // We only require 75% of the records to pass, because it is possible that
        // some large scale outages could cause some of these records to fail.
        const double kPassingPercentage= 0.75;
        std::size_t resolution_count= 0;
        const struct
        {
            std::string dns;
            std::string ip;
        }
        tests[]=
            // The reason for a vast number of tests over basic DNS query calls is to provide a
            // redundancy in testing.  We'd like to make sure that this test always passes.  Lazy
            // maintanance will cause some references to be commented out.  Our belief is that all
            // 13 root servers and both of Google's public servers will all be unresolvable (when
            // connections are available) only when a major problem occurs.  This test only fails if
            // more than half of the resolved names fail.
            {
                // These can be kept up to date by checking the root-servers.org listings.  Note
                // that root name servers are located in the `root-servers.net.` domain, NOT in
                // the `root-servers.org.` domain.  The `.org` domain is for webpages with
                // statistics on these servers.  The `.net` domain is the domain with the canonical
                // addresses for these servers.
                { "a.root-servers.net.", "198.41.0.4" },
                { "b.root-servers.net.", "192.228.79.201" },
                { "c.root-servers.net.", "192.33.4.12" },
                { "d.root-servers.net.", "199.7.91.13" },
                { "e.root-servers.net.", "192.203.230.10" },
                { "f.root-servers.net.", "192.5.5.241" },
                { "g.root-servers.net.", "192.112.36.4" },
                { "h.root-servers.net.", "198.97.190.53" },
                { "i.root-servers.net.", "192.36.148.17" },
                { "j.root-servers.net.", "192.58.128.30" },
                { "k.root-servers.net.", "193.0.14.129" },
                { "l.root-servers.net.", "199.7.83.42" },
                { "m.root-servers.net.", "202.12.27.33" },

                // These can be kept up to date by checking with Google's public dns service.
                { "google-public-dns-a.google.com.", "8.8.8.8" },
                { "google-public-dns-b.google.com.", "8.8.4.4" },
            };
        for( const auto &test: tests )
        try
        {
            const auto witness= mongo::dns::getARecord( test.dns );
            std::cout << "Resolved " << test.dns << " to: " << witness << std::endl;

            const bool resolution= ( witness == test.ip );
            if( !resolution ) std::cerr << "Warning: Did not correctly resolve " << test.dns << std::endl;
            resolution_count+= resolution;
        }
        // Failure to resolve is okay, but not great
        catch( const mongo::dns::DNSLookupException & )
        {
            std::cerr << "Warning: Did not resolve " << test.dns << " at all." << std::endl;
        }

        const std::size_t kPassingRate= sizeof( tests ) / sizeof( tests[ 0 ] ) * kPassingPercentage;
        ASSERT_GTE( resolution_count, kPassingRate );
	}

	TEST( MongoDnsQuery, srvRecords )
	{
		const auto kMongodbSRVPrefix= "_mongodb._tcp."s;
		const struct
		{
			std::string query;
			std::vector< mongo::dns::SRVHostEntry > result;
		}
		tests[]=
				{
					{
						"test1.test.build.10gen.cc.",
						{
							{ "localhost.build.10gen.cc.", 27017 },
							{ "localhost.build.10gen.cc.", 27018 },
						}
					},
					{
						"test2.test.build.10gen.cc.",
						{
							{ "localhost.build.10gen.cc.", 27018 },
							{ "localhost.build.10gen.cc.", 27019 },
						}
					},
					{
						"test3.test.build.10gen.cc.",
						{
							{ "localhost.build.10gen.cc.", 27017 },
						}
					},

                    // Test case 4 does not exist in the expected DNS records.
                    { "test4.test.build.10gen.cc.", {} },

					{
						"test5.test.build.10gen.cc.",
						{
							{ "localhost.build.10gen.cc.", 27017 },
						}
					},
					{
						"test6.test.build.10gen.cc.",
						{
							{ "localhost.build.10gen.cc.", 27017 },
						}
					},
				};
		for( const auto &test: tests )
		{
			const auto &expected= test.result;
            if( expected.empty() )
            {
                ASSERT_THROWS( mongo::dns::getSRVRecord( kMongodbSRVPrefix + test.query ),
                        mongo::dns::DNSLookupException );
                continue;
            }

            auto witness= mongo::dns::getSRVRecord( kMongodbSRVPrefix + test.query );
			std::sort( begin( witness ), end( witness ) );

			for( const auto &entry: witness )
			{
				std::cout << "Entry: " << entry << std::endl;
			}

			for( std::size_t i= 0; i < witness.size() && i < expected.size(); ++i )
			{
				std::cout << "Expected: " << expected.at( i ) << std::endl;
				std::cout << "Witness:  " << witness.at( i ) << std::endl;
				ASSERT_EQ( witness.at( i ), expected.at( i ) );
			}

			ASSERT_TRUE( std::equal( begin( witness ), end( witness ), begin( expected ), end( expected ) ) );
			ASSERT_TRUE( witness.size() == expected.size() );
		}
	}

	TEST( MongoDnsQuery, txtRecords )
	{
		const struct
		{
			std::string query;
			std::vector< std::string > result;
		}
		tests[]=
				{
                    // Test case 4 does not exist in the expected DNS records.
                    { "test4.test.build.10gen.cc.", {} },

					{
						"test5.test.build.10gen.cc",
						{
							"connectTimeoutMS=300000&socketTimeoutMS=300000",
						}
					},
					{
						"test6.test.build.10gen.cc",
						{
							"connectTimeoutMS=200000",
							"socketTimeoutMS=200000",
						}
					},
				};
		
		for( const auto &test: tests )
		{
			auto witness= mongo::dns::getTXTRecord( test.query );
			std::sort( begin( witness ), end( witness ) );

			for( const auto &entry: witness )
			{
				std::cout << "Entry: " << entry << std::endl;
			}

			const auto &expected= test.result;

			ASSERT_TRUE( std::equal( begin( witness ), end( witness ), begin( expected ), end( expected ) ) );
			ASSERT_TRUE( witness.size() == expected.size() );
		}
	}
}//namespace
