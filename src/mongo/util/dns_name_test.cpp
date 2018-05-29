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


#include "mongo/util/dns_name.h"

#include "mongo/unittest/unittest.h"
#include "mongo/stdx/utility.h"

using namespace std::literals::string_literals;

namespace mongo
{
    namespace
    {
        TEST( DNSNameTest, CorrectParsing )
        {
            enum FQDNBool : bool { kIsFQDN= true, kNotFQDN= false };
            const struct
            {
                std::string input;
                std::vector< std::string > parsedDomains;
                FQDNBool isFQDN;
            }
            tests[]=
            {
                { "com."s, { "com"s }, kIsFQDN },
                { "com"s, { "com"s }, kNotFQDN },
                { "mongodb.com."s, { "com"s, "mongodb"s }, kIsFQDN },
                { "mongodb.com"s, { "com"s, "mongodb"s }, kNotFQDN },
                { "atlas.mongodb.com."s, { "com"s, "mongodb"s, "atlas"s }, kIsFQDN },
                { "atlas.mongodb.com"s, { "com"s, "mongodb"s, "atlas"s }, kNotFQDN },
                { "server.atlas.mongodb.com."s, { "com"s, "mongodb"s, "atlas"s, "server"s }, kIsFQDN },
                { "server.atlas.mongodb.com"s, { "com"s, "mongodb"s, "atlas"s, "server"s }, kNotFQDN },
            };

            for( const auto &test: tests )
            {
                const ::mongo::dns::HostName host( test.input );

                ASSERT_EQ( host.nameComponents().size(), test.parsedDomains.size() );
                for( std::size_t i= 0; i < host.nameComponents().size(); ++i )
                {
                    ASSERT_EQ( host.nameComponents()[ i ], test.parsedDomains[ i ] );
                }
                ASSERT( host.isFQDN() == test.isFQDN );
            }
        }

        TEST( DNSNameTest, CanonicalName )
        {
            const struct
            {
                std::string input;
                std::string result;
            }
            tests[]=
            {
                { "com."s, "com."s },
                { "com"s, "com"s },
                { "mongodb.com."s, "mongodb.com."s },
                { "mongodb.com"s, "mongodb.com"s },
                { "atlas.mongodb.com."s, "atlas.mongodb.com."s },
                { "atlas.mongodb.com"s, "atlas.mongodb.com"s },
                { "server.atlas.mongodb.com."s, "server.atlas.mongodb.com."s },
                { "server.atlas.mongodb.com"s, "server.atlas.mongodb.com"s },
            };

            for( const auto &test: tests )
            {
                const ::mongo::dns::HostName host( test.input );

                ASSERT_EQ( host.canonicalName(), test.result );
            }
        }

        TEST( DNSNameTest, SSLName )
        {
            const struct
            {
                std::string input;
                std::string result;
            }
            tests[]=
            {
                { "com."s, "com"s },
                { "com"s, "com"s },
                { "mongodb.com."s, "mongodb.com"s },
                { "mongodb.com"s, "mongodb.com"s },
                { "atlas.mongodb.com."s, "atlas.mongodb.com"s },
                { "atlas.mongodb.com"s, "atlas.mongodb.com"s },
                { "server.atlas.mongodb.com."s, "server.atlas.mongodb.com"s },
                { "server.atlas.mongodb.com"s, "server.atlas.mongodb.com"s },
            };

            for( const auto &test: tests )
            {
                const ::mongo::dns::HostName host( test.input );

                ASSERT_EQ( host.sslName(), test.result );
            }
        }

        TEST( DNSNameTest, Contains )
        {
            enum IsSubdomain : bool { kIsSubdomain= true, kNotSubdomain= false };
            const struct
            {
                std::string domain;
                std::string subdomain;
                IsSubdomain isSubdomain;
            }
            tests[]=
            {
                { "com."s, "mongodb.com."s, kIsSubdomain },
                { "com"s, "mongodb.com"s, kIsSubdomain },
                { "com."s, "mongodb.com"s, kNotSubdomain },
                { "com"s, "mongodb.com."s, kNotSubdomain },

                { "com."s, "atlas.mongodb.com."s, kIsSubdomain },
                { "com"s, "atlas.mongodb.com"s, kIsSubdomain },
                { "com."s, "atlas.mongodb.com"s, kNotSubdomain },
                { "com"s, "atlas.mongodb.com."s, kNotSubdomain },

                { "org."s, "atlas.mongodb.com."s, kNotSubdomain },
                { "org"s, "atlas.mongodb.com"s, kNotSubdomain },
                { "org."s, "atlas.mongodb.com"s, kNotSubdomain },
                { "org"s, "atlas.mongodb.com."s, kNotSubdomain },

                { "com."s, "com."s, kNotSubdomain },
                { "com"s, "com."s, kNotSubdomain },
                { "com."s, "com"s, kNotSubdomain },
                { "com"s, "com"s, kNotSubdomain },

                { "mongodb.com."s, "mongodb.com."s, kNotSubdomain },
                { "mongodb.com."s, "mongodb.com"s, kNotSubdomain },
                { "mongodb.com"s, "mongodb.com."s, kNotSubdomain },
                { "mongodb.com"s, "mongodb.com"s, kNotSubdomain },

                { "mongodb.com."s, "atlas.mongodb.com."s, kIsSubdomain },
                { "mongodb.com"s, "atlas.mongodb.com"s, kIsSubdomain },
                { "mongodb.com."s, "atlas.mongodb.com"s, kNotSubdomain },
                { "mongodb.com"s, "atlas.mongodb.com."s, kNotSubdomain },

                { "mongodb.com."s, "server.atlas.mongodb.com."s, kIsSubdomain },
                { "mongodb.com"s, "server.atlas.mongodb.com"s, kIsSubdomain },
                { "mongodb.com."s, "server.atlas.mongodb.com"s, kNotSubdomain },
                { "mongodb.com"s, "server.atlas.mongodb.com."s, kNotSubdomain },

                { "mongodb.org."s, "server.atlas.mongodb.com."s, kNotSubdomain },
                { "mongodb.org"s, "server.atlas.mongodb.com"s, kNotSubdomain },
                { "mongodb.org."s, "server.atlas.mongodb.com"s, kNotSubdomain },
                { "mongodb.org"s, "server.atlas.mongodb.com."s, kNotSubdomain },
            };

            for( const auto &test: tests )
            {
                const ::mongo::dns::HostName domain( test.domain );
                const ::mongo::dns::HostName subdomain( test.subdomain );

                ASSERT( test.isSubdomain == domain.contains( subdomain ) );
            }
        }

        TEST( DNSNameTest, Resolution )
        {
            enum Failure : bool { kFails= true, kSucceeds= false };
            enum FQDNBool : bool { kIsFQDN= true, kNotFQDN= false };
            const struct
            {
                std::string domain;
                std::string subdomain;
                std::string result;

                Failure fails;
                FQDNBool isFQDN;
            }
            tests[]=
            {
                { "mongodb.com."s, "atlas"s, "atlas.mongodb.com."s, kSucceeds, kIsFQDN },
                { "mongodb.com"s, "atlas"s, "atlas.mongodb.com"s, kSucceeds, kNotFQDN },

                { "mongodb.com."s, "server.atlas"s, "server.atlas.mongodb.com."s, kSucceeds, kIsFQDN },
                { "mongodb.com"s, "server.atlas"s, "server.atlas.mongodb.com"s, kSucceeds, kNotFQDN },

                { "mongodb.com."s, "atlas."s, "FAILS"s, kFails, kNotFQDN },
                { "mongodb.com"s, "atlas."s, "FAILS"s, kFails, kNotFQDN },
            };

            for( const auto &test: tests )
            try
            {
                const ::mongo::dns::HostName domain( test.domain );
                const ::mongo::dns::HostName subdomain( test.subdomain );
                const ::mongo::dns::HostName resolved=
                [&]
                {
                    try
                    {
                        const ::mongo::dns::HostName rv= subdomain.resolvedIn( domain );
                        return rv;
                    }
                    catch( const ExceptionFor< ErrorCodes::DNSRecordTypeMismatch > & )
                    {
                        ASSERT( test.fails );
                        throw;
                    }
                }();
                ASSERT( !test.fails );

                ASSERT_EQ( test.result, resolved.canonicalName() );
                ASSERT( test.isFQDN == resolved.isFQDN() );
            }
            catch( const ExceptionFor< ErrorCodes::DNSRecordTypeMismatch > & )
            {
                ASSERT( test.fails );
            }
        }

        TEST( DNSNameTest, ForceQualification )
        {
            enum FQDNBool : bool { kIsFQDN= true, kNotFQDN= false };
            using Qualification= ::mongo::dns::HostName::Qualification;
            const struct
            {
                std::string domain;
                FQDNBool startedFQDN;
                ::mongo::dns::HostName::Qualification forced;
                FQDNBool becameFQDN;
                std::string becameCanonical;
            }
            tests[]=
            {
                { "mongodb.com."s, kIsFQDN, Qualification::FullyQualified, kIsFQDN, "mongodb.com."s },
                { "mongodb.com"s, kNotFQDN, Qualification::FullyQualified, kIsFQDN, "mongodb.com."s },

                { "atlas.mongodb.com."s, kIsFQDN, Qualification::FullyQualified, kIsFQDN, "atlas.mongodb.com."s },
                { "atlas.mongodb.com"s, kNotFQDN, Qualification::FullyQualified, kIsFQDN, "atlas.mongodb.com."s },

                { "mongodb.com."s, kIsFQDN, Qualification::RelativeName, kNotFQDN, "mongodb.com"s },
                { "mongodb.com"s, kNotFQDN, Qualification::RelativeName, kNotFQDN, "mongodb.com"s },

                { "atlas.mongodb.com."s, kIsFQDN, Qualification::RelativeName, kNotFQDN, "atlas.mongodb.com"s },
                { "atlas.mongodb.com"s, kNotFQDN, Qualification::RelativeName, kNotFQDN, "atlas.mongodb.com"s },
            };

            for( const auto &test: tests )
            {
                ::mongo::dns::HostName domain( test.domain );
                ASSERT( stdx::as_const( domain ).isFQDN() == test.startedFQDN );
                domain.forceQualification( test.forced );
                ASSERT( stdx::as_const( domain ).isFQDN() == test.becameFQDN );

                ASSERT_EQ( stdx::as_const( domain ).canonicalName(), test.becameCanonical );
            }
        }
    }//namespace
}//namespace mongo
