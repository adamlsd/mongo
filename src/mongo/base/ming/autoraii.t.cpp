#include "autoraii.h"
#include <cstdio>

#include "mongo/unittest/unittest.h"

#include "mongo/util/scopeguard.h"

TEST( AutoRAIITest, basicTest )
{
	mongo::ming::AutoRAII< FILE * > fp( []{ return fopen( "foo.txt", "wb" ); },
			[]( FILE *const fp ) { if( fp ) fclose( fp ); } );

	fprintf( fp, "Hello world!" );

	auto file= mongo::ming::makeUniqueRAII( []{ return fopen( "foo2.txt", "wb" ); },
			[]( FILE *const fp ) { if( fp ) fclose( fp ); } );

	mongo::ScopeGuard guard= mongo::MakeGuard( printf, "Hello World" );
}
