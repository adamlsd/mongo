#include "mongo/db/storage/test_harness_helper.h"

#include <exception>
#include <stdexcept>

#include "mongo/util/assert_util.h"

namespace mongo {
namespace {
std::function<std::unique_ptr<HarnessHelper>()> basicHarnessFactory =
    []() -> std::unique_ptr<HarnessHelper> { fassertFailed(ErrorCodes::BadValue); };
}  // namespace
}  // namespace mongo


mongo::HarnessHelper::~HarnessHelper() noexcept = default;

void mongo::registerHarnessHelperFactory(std::function<std::unique_ptr<HarnessHelper>()> factory) {
    basicHarnessFactory = std::move(factory);
}

auto mongo::newHarnessHelper() -> std::unique_ptr<HarnessHelper> {
    return basicHarnessFactory();
}
