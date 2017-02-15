#pragma once

namespace mongo {
namespace ming {
namespace concept {
/**
 * The CopyConstructable concept models a type which can be copy constructed.
 *
 * The expression: `CopyConstructible{ copyConstructible }` should be valid.
 */
struct CopyConstructible {
    CopyConstructible(const CopyConstructible&);
};
}  // namespace concept
}  // namespace ming
}  // namespace mongo
