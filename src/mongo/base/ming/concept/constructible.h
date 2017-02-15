#pragma once

namespace mongo {
namespace ming {
namespace concept {
/**
 * The Constructable concept models a type which can be passed to a single-argument constructor of
 * `T`.
 * This is not possible to describe in the type `Constructible`.
 *
 * The expression: `T{ Constructible< T >{} }` should be valid.
 *
 * This concept is more broadly applicable than `ConvertibleTo`.  `ConvertibleTo` uses implicit
 * conversion, whereas `Constructible` uses direct construction.
 */
template <typename T>
struct Constructible;
}  // namespace concept
}  // namespace ming
}  // namespace mongo
