#include <iostream>
#include <cstdlib>

namespace
{
	void somethingToCompile() { abort(); }
}


namespace NotUsed
{
	void illegal_to_ever_call_me() { somethingToCompile(); }
}
