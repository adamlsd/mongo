#pragma once

#include <cstdint>
#include <initializer_list>
#include <memory>

#include "mongo/db/jsobj.h"
#include "mongo/db/operation_context_noop.h"
#include "mongo/db/record_id.h"
#include "mongo/db/service_context.h"
#include "mongo/db/service_context_noop.h"
#include "mongo/db/storage/sorted_data_interface.h"
#include "mongo/stdx/memory.h"
#include "mongo/util/unowned_ptr.h"

namespace mongo {
class HarnessHelper {
public:
    virtual ~HarnessHelper() noexcept = 0;

    explicit HarnessHelper() = default;

    virtual ServiceContext::UniqueOperationContext newOperationContext(Client* const client) {
        auto opCtx = client->makeOperationContext();
        opCtx->setRecoveryUnit(newRecoveryUnit().release(), OperationContext::kNotInUnitOfWork);
        return opCtx;
    }

    virtual ServiceContext::UniqueOperationContext newOperationContext() {
        return newOperationContext(_client.get());
    }

    Client* client() const {
        return _client.get();
    }

    ServiceContext* serviceContext() {
        return &_serviceContext;
    }

    const ServiceContext* serviceContext() const {
        return &_serviceContext;
    }

private:
    virtual std::unique_ptr<RecoveryUnit> newRecoveryUnit() = 0;

    ServiceContextNoop _serviceContext;
    ServiceContext::UniqueClient _client = _serviceContext.makeClient("hh");
};

template <typename Target, typename Current>
std::unique_ptr<Target> dynamic_ptr_cast(std::unique_ptr<Current> p) {
    Target* const ck = dynamic_cast<Target*>(p.get());
    p.release();
    return std::unique_ptr<Target>(ck);
}

extern void registerHarnessHelperFactory(std::function<std::unique_ptr<HarnessHelper>()> factory);

extern std::unique_ptr<HarnessHelper> newHarnessHelper();
}  // namespace mongo
