#include "mongo/s/is_mongos.h"

namespace mongo
{
	namespace
	{
		bool mongosState= false;
	}
}

bool
mongo::isMongos()
{
	return mongosState;
}

void
mongo::setMongos( const bool state )
{
	mongosState= state;
}
