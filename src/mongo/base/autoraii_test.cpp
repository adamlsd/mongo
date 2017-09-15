/**
 *    Copyright (C) 2017 10gen Inc.
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

#include "mongo/base/unique_raii.h"
#include "mongo/base/scoped_raii.h"

#include "mongo/unittest/unittest.h"


namespace mongo
{
	namespace
	{
		struct DtorCheck : private boost::noncopyable
		{
			bool *const notification;

			explicit DtorCheck( bool *const n ) : notification( n ) {}

			~DtorCheck() { *notification= true; }
		};

		TEST( ScopedRAIITest, TestBasicCtorAndDtor )
		{
			bool destroyed= false;
			{
				ScopedRAII< DtorCheck * > owned( [d= &destroyed]{ return new DtorCheck( d ); }, []( DtorCheck *p ) { delete p; } );
				ASSERT_FALSE( destroyed );
			}
			ASSERT_TRUE( destroyed );
		}

		TEST( ScopedRAIITest, NoParam )
		{
			int state= 0;
			{
				ASSERT_TRUE( state == 0 );
				ScopedRAII<> scope( [s= &state] { *s= 1; }, [s= &state] { *s= 2; } );
				ASSERT_TRUE( state == 1 );
			}
			ASSERT_TRUE( state == 2 );
		}

		TEST( DismissableRAIITest, BasicTest )
		{
			int state= 0;
			for( int i= 0; i < 20; ++i )
			{
				DismissableRAII scope( [s= &state]{ ++*s; }, [s= &state] { ++*s; } );

				if( i % 2 ) scope.dismiss();
			}
			ASSERT_TRUE( state == 30 );
		}

		TEST( UniqueRAIITest, BasicTest )
		{
			bool destroyed= false;
			{
				auto release= []( DtorCheck *const p ) { delete p; };
				UniqueRAII< DtorCheck *, decltype( release ) > raii( [d= &destroyed]{ return new DtorCheck( d ); }, release );
				ASSERT_FALSE( destroyed );
			}
			ASSERT_TRUE( destroyed );
		}

		TEST( UniqueRAIITest, TransferTestInner )
		{
			bool destroyed= false;
			{
				auto release= []( DtorCheck *const p ) { delete p; };
				UniqueRAII< DtorCheck *, decltype( release ) > raii( [d= &destroyed]{ return new DtorCheck( d ); }, release );
				ASSERT_FALSE( destroyed );
				{
					UniqueRAII< DtorCheck *, decltype( release ) > raii2= std::move( raii );
					ASSERT_FALSE( destroyed );
				}
				ASSERT_TRUE( destroyed );
				destroyed= false;
			}
			ASSERT_FALSE( destroyed );
		}

		TEST( UniqueRAIITest, TransferTestOuter )
		{
			bool destroyed= false;
			{
				auto release= []( DtorCheck *const p ) { delete p; };
				UniqueRAII< DtorCheck *, decltype( release ) > raii( [d= &destroyed]{ return new DtorCheck( d ); }, release );
				ASSERT_FALSE( destroyed );
				{
					UniqueRAII< DtorCheck *, decltype( release ) > raii2= std::move( raii );
					ASSERT_FALSE( destroyed );
					//raii= std::move( raii2 );
					ASSERT_FALSE( destroyed );
				}
				ASSERT_TRUE( destroyed );
			}
			ASSERT_TRUE( destroyed );
		}

		TEST( UniqueRAIITest, makeRAII )
		{
			bool destroyed= false;
			{
				auto raii= make_unique_raii( [d= &destroyed]{ return new DtorCheck( d ); }, []( DtorCheck *const p ) { delete p; } );
				ASSERT_FALSE( destroyed );
			}
			ASSERT_TRUE( destroyed );
		}
	}//namespace
}//namespace mongo
