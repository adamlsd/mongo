#pragma once

#include "mongo/base/ming/copy_constructible.h"
#include "mongo/base/ming/copy_assignable.h"

namespace mongo {
namespace ming {
namespace concept {
/*!
 * The Assignable concept models a type which can be copy assigned and copy constructed.
 */
struct Assignable : CopyConstructible, CopyAssignable {};
}  // namespace concept
}  // namespace ming
}  // namespace mongo
