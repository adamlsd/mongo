/**
 *    Copyright (C) 2017 MongoDB Inc.
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

#include "mongo/client/embedded/libmongodbcapi.h"

#include <cstring>
#include <exception>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mongo/client/embedded/embedded.h"
#include "mongo/client/embedded/embedded_log_appender.h"
#include "mongo/db/client.h"
#include "mongo/db/dbmain.h"
#include "mongo/db/service_context.h"
#include "mongo/logger/logger.h"
#include "mongo/logger/message_event_utf8_encoder.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/transport/service_entry_point.h"
#include "mongo/transport/transport_layer_mock.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/net/message.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/shared_buffer.h"

struct libmongodbcapi_status {
    libmongodbcapi_status() noexcept = default;
    libmongodbcapi_status(const int e, const int ec, std::string w)
        : error(e), exception_code(ec), what(std::move(w)) {}

    int error = LIBMONGODB_CAPI_SUCCESS;
    int exception_code = 0;
    std::string what;

    void clean() noexcept {
        error = LIBMONGODB_CAPI_SUCCESS;
    }
};

namespace {
struct StatusGuard {
private:
    libmongodbcapi_status* status;
    libmongodbcapi_status fallback;

public:
    explicit StatusGuard(libmongodbcapi_status* const statusPtr) noexcept : status(statusPtr) {
        if (status)
            status->clean();
    }

    libmongodbcapi_status& get() noexcept {
        return status ? *status : fallback;
    }

    const libmongodbcapi_status& get() const noexcept {
        return status ? *status : fallback;
    }

    operator libmongodbcapi_status&() & noexcept {
        return this->get();
    }
    operator libmongodbcapi_status&() && noexcept {
        return this->get();
    }
};
}

namespace mongo {
namespace {
class MobileException : public std::exception {
private:
    std::string _mesg;
    int _code;

public:
    explicit MobileException(const int code, std::string m) : _mesg(std::move(m)), _code(code) {}

    virtual int mobileCode() const noexcept {
        return this->_code;
    }

    const char* what() const noexcept final {
        return this->_mesg.c_str();
    }
};

libmongodbcapi_status translateException() noexcept try {
    throw;
} catch (const MobileException& ex) {
    return {ex.mobileCode(), mongo::ErrorCodes::InternalError, ex.what()};
} catch (const DBException& ex) {
    return {LIBMONGODB_CAPI_ERROR_EXCEPTION, ex.code(), ex.what()};
} catch (const std::bad_alloc& ex) {
    return {LIBMONGODB_CAPI_ERROR_ENOMEM, mongo::ErrorCodes::InternalError, ex.what()};
} catch (const std::exception& ex) {
    return {LIBMONGODB_CAPI_ERROR_EXCEPTION, mongo::ErrorCodes::InternalError, ex.what()};
}

std::nullptr_t handleException(libmongodbcapi_status& status) noexcept {
    try {
        status = translateException();
    } catch (const std::exception& ex) {
        status.error = LIBMONGODB_CAPI_ERROR_IN_REPORTING_ERROR;

        try {
            status.exception_code = 0;

            status.what.clear();

            // Expected to be small enough to fit in the capacity that string always has.
            const char severeErrorMessage[] = "Severe Error";

            if (status.what.capacity() < sizeof(severeErrorMessage)) {
                status.what = severeErrorMessage;
            }
        } catch (...) { /* Ignore any errors at this point. */
        }
    }
    return nullptr;
}
}
}

struct libmongodbcapi_lib {
    ~libmongodbcapi_lib() {
        invariant(this->databaseCount == 0);
    }

    libmongodbcapi_lib() = default;

    libmongodbcapi_lib(const libmongodbcapi_lib&) = delete;
    void operator=(const libmongodbcapi_lib) = delete;

    std::atomic<int> databaseCount = {};

    bool guaranteedStatusSent = false;
    libmongodbcapi_status guaranteedStatus;
    std::unique_ptr<libmongodbcapi_db> onlyDB;
    mongo::logger::ComponentMessageLogDomain::AppenderHandle logCallbackHandle;
};

namespace mongo {
namespace {
struct ServiceContextDestructor {
    void operator()(mongo::ServiceContext* const serviceContext) const noexcept {
        ::mongo::embedded::shutdown(serviceContext);
    }
};

using EmbeddedServiceContextPtr = std::unique_ptr<mongo::ServiceContext, ServiceContextDestructor>;
}  // namespace
}  // namespace mongo

struct libmongodbcapi_db {
    ~libmongodbcapi_db() {
        --this->parent_lib->databaseCount;
        invariant(this->clientCount == 0);
    }

    libmongodbcapi_db(libmongodbcapi_lib* const p) : parent_lib(p) {
        ++this->parent_lib->databaseCount;
    }


    libmongodbcapi_db(const libmongodbcapi_db&) = delete;
    libmongodbcapi_db& operator=(const libmongodbcapi_db&) = delete;

    mongo::EmbeddedServiceContextPtr serviceContext;
    std::unique_ptr<mongo::transport::TransportLayerMock> transportLayer;

    libmongodbcapi_lib* parent_lib;
    std::atomic<int> clientCount = {};
};

struct libmongodbcapi_client {
    ~libmongodbcapi_client() {
        --this->parent_db->clientCount;
    }

    libmongodbcapi_client(libmongodbcapi_db* const db) : parent_db(db) {
        ++this->parent_db->clientCount;
    }

    libmongodbcapi_client(const libmongodbcapi_client&) = delete;
    libmongodbcapi_client& operator=(const libmongodbcapi_client&) = delete;

    void* client_handle = nullptr;
    std::vector<unsigned char> output;
    mongo::ServiceContext::UniqueClient client;
    mongo::DbResponse response;

    libmongodbcapi_db* const parent_db;
};

namespace mongo {
namespace {

thread_local int callEntryDepth = 0;
std::unique_ptr<libmongodbcapi_lib> library;


class ReentrancyGuard {
public:
    ReentrancyGuard() {
        uassert(ErrorCodes::ReentrancyNotAllowed,
                str::stream() << "Reentry into libmongodbcapi is not allowed",
                callEntryDepth == 0);
        ++callEntryDepth;
    }

    ~ReentrancyGuard() {
        --callEntryDepth;
    }

    ReentrancyGuard(ReentrancyGuard const&) = delete;
    ReentrancyGuard& operator=(ReentrancyGuard const&) = delete;
};

void register_log_callback(libmongodbcapi_lib* const lib,
                           const libmongodbcapi_log_callback log_callback,
                           void* const log_user_data) {
    using logger::globalLogDomain;
    using logger::MessageEventEphemeral;
    using logger::MessageEventUnadornedEncoder;

    lib->logCallbackHandle = globalLogDomain()->attachAppender(
        std::make_unique<embedded::EmbeddedLogAppender<MessageEventEphemeral>>(
            log_callback, log_user_data, std::make_unique<MessageEventUnadornedEncoder>()));
}

void unregister_log_callback(libmongodbcapi_lib* const lib) {
    using logger::globalLogDomain;

    globalLogDomain()->detachAppender(lib->logCallbackHandle);
    lib->logCallbackHandle.reset();
}

libmongodbcapi_lib* capi_lib_init(libmongodbcapi_init_params const* params,
                                  libmongodbcapi_status& status,
                                  const ReentrancyGuard& = {}) noexcept try {
    if (library) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_LIBRARY_ALREADY_INITIALIZED,
            "Cannot initialize the MongoDB Embedded Library when it is already initialized."};
    }

    auto lib = std::make_unique<libmongodbcapi_lib>();


    if (params) {
        using logger::globalLogManager;
        // The standard console log appender may or may not be installed here, depending if this is
        // the first time we initialize the library or not. Make sure we handle both cases.
        if (params->log_flags & LIBMONGODB_CAPI_LOG_STDOUT) {
            if (!globalLogManager()->isDefaultConsoleAppenderAttached())
                globalLogManager()->reattachDefaultConsoleAppender();
        } else {
            if (globalLogManager()->isDefaultConsoleAppenderAttached())
                globalLogManager()->detachDefaultConsoleAppender();
        }

        if ((params->log_flags & LIBMONGODB_CAPI_LOG_CALLBACK) && params->log_callback) {
            register_log_callback(lib.get(), params->log_callback, params->log_user_data);
        }
    }

    library = std::move(lib);

    return library.get();
} catch (...) {
    // Make sure that no actual logger is attached if library cannot be initialized.  Also prevent
    // exception leaking failures here.
    []() noexcept {
        using logger::globalLogManager;
        if (globalLogManager()->isDefaultConsoleAppenderAttached())
            globalLogManager()->detachDefaultConsoleAppender();
    }
    ();
    return handleException(status);
}

void capi_lib_fini(libmongodbcapi_lib* const lib,
                   libmongodbcapi_status& status,
                   const ReentrancyGuard& = {}) noexcept try {
    if (!lib) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_INVALID_LIB_HANDLE,
            "Cannot close a `NULL` pointer referencing a MongoDB Embedded Library Instance"};
    }

    if (!library) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED,
            "Cannot close the MongoDB Embedded Library when it is not initialized"};
    }

    if (library.get() != lib) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_INVALID_LIB_HANDLE,
                              "Invalid MongoDB Embedded Library handle."};
    }
    // This check is not possible to 100% guarantee.  It is a best effort.  The documentation of
    // this API says that the behavior of closing a `lib` with open handles is undefined, but may
    // provide diagnostic errors in some circumstances.
    if (lib->databaseCount > 0) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_HAS_DB_HANDLES_OPEN,
            "Cannot close the MongoDB Embedded Library when it has database handles still open."};
    }

    unregister_log_callback(lib);

    library.reset();
} catch (...) {
    handleException(status);
}

libmongodbcapi_db* db_new(libmongodbcapi_lib* const lib,
                          const char* const yaml_config,
                          libmongodbcapi_status& status,
                          const ReentrancyGuard& = {}) noexcept try {
    if (!library) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED,
                              "Cannot create a new database handle when the MongoDB Embedded "
                              "Library is not yet initialized."};
    }

    if (library.get() != lib) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_INVALID_LIB_HANDLE,
                              "Cannot create a new database handle when the MongoDB Embedded "
                              "Library is not yet initialized."};
    }

    if (lib->onlyDB) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_DB_MAX_OPEN,
                              "The maximum number of permitted database handles for the MongoDB "
                              "Embedded Library have been opened."};
    }

    auto newDb = std::make_unique<libmongodbcapi_db>(lib);

    newDb->serviceContext.reset(embedded::initialize(yaml_config));

    if (!newDb->serviceContext) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_DB_INITIALIZATION_FAILED,
            "The MongoDB Embedded Library Failed to initialize the Service Context"};
    }

    // creating mock transport layer to be able to create sessions
    newDb->transportLayer = std::make_unique<transport::TransportLayerMock>();

    lib->onlyDB = std::move(newDb);

    return lib->onlyDB.get();
} catch (...) {
    return handleException(status);
}

void db_destroy(libmongodbcapi_db* const db, libmongodbcapi_status& status) noexcept try {
    if (!library) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED,
                              "Cannot destroy a database handle when the MongoDB Embedded Library "
                              "is not yet initialized."};
    }

    if (!db) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_INVALID_DB_HANDLE,
            "Cannot close a `NULL` pointer referencing a MongoDB Embedded Database"};
    }

    if (db != library->onlyDB.get()) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_INVALID_DB_HANDLE,
            "Cannot close the specified MongoDB Embedded Database, as it is not a valid instance."};
    }

    if (db->clientCount > 0) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_DB_CLIENTS_OPEN,
            "Cannot close a MongoDB Embedded Database instance while it has open clients"};
    }

    library->onlyDB = nullptr;
} catch (...) {
    handleException(status);
}

libmongodbcapi_client* client_new(libmongodbcapi_db* const db,
                                  libmongodbcapi_status& status,
                                  const ReentrancyGuard& = {}) noexcept try {
    if (!library) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED,
                              "Cannot create a new client handle when the MongoDB Embedded Library "
                              "is not yet initialized."};
    }
    if (!db) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_INVALID_DB_HANDLE,
                              "Cannot use a `NULL` pointer referencing a MongoDB Embedded Database "
                              "when creating a new client"};
    }

    if (db != library->onlyDB.get()) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_INVALID_DB_HANDLE,
                              "The specified MongoDB Embedded Database instance cannot be used to "
                              "create a new client because it is invalid."};
    }

    auto rv = stdx::make_unique<libmongodbcapi_client>(db);

    auto session = db->transportLayer->createSession();
    rv->client = db->serviceContext->makeClient("embedded", std::move(session));

    return rv.release();
} catch (...) {
    return handleException(status);
}

void client_destroy(libmongodbcapi_client* const client,
                    libmongodbcapi_status& status,
                    const ReentrancyGuard& = {}) noexcept try {
    if (!library) {
        throw MobileException(LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED,
                              "Cannot destroy a database handle when the MongoDB Embedded Library "
                              "is not yet initialized.");
    }

    if (!client) {
        throw MobileException{
            LIBMONGODB_CAPI_ERROR_INVALID_CLIENT_HANDLE,
            "Cannot close a `NULL` pointer referencing a MongoDB Embedded Database Client"};
    }

    delete client;
} catch (...) {
    handleException(status);
}

void client_wire_protocol_rpc(libmongodbcapi_client* const client,
                              const void* input,
                              const size_t input_size,
                              void** const output,
                              size_t* const output_size,
                              libmongodbcapi_status& status,
                              const ReentrancyGuard& = {}) noexcept try {
    mongo::Client::setCurrent(std::move(client->client));
    const auto guard = mongo::MakeGuard([&] { client->client = mongo::Client::releaseCurrent(); });

    auto opCtx = cc().makeOperationContext();
    auto sep = client->parent_db->serviceContext->getServiceEntryPoint();

    auto sb = SharedBuffer::allocate(input_size);
    memcpy(sb.get(), input, input_size);

    Message msg(std::move(sb));

    client->response = sep->handleRequest(opCtx.get(), msg);

    // The results of the computations used to fill out-parameters need to be captured and processed
    // before setting the output parameters themselves, in order to maintain the strong-guarantee
    // part of the contract of this function.
    auto outParams =
        std::make_tuple(client->response.response.size(), client->response.response.buf());

    // We force the output parameters to be set in a `noexcept` enabled way.  If the operation
    // itself
    // is safely noexcept, we just run it, otherwise we force a `noexcept` over it to catch errors.
    if (noexcept(std::tie(*output_size, *output) = std::move(outParams))) {
        std::tie(*output_size, *output) = std::move(outParams);
    } else {
        // Assigning primitives in a tied tuple should be noexcept, so we force it to be so, for
        // our purposes.  This facilitates a runtime check should something WEIRD happen.
        [ output, output_size, &outParams ]() noexcept {
            std::tie(*output_size, *output) = std::move(outParams);
        }
        ();
    }
} catch (...) {
    handleException(status);
}

int capi_status_get_error(const libmongodbcapi_status* const status) noexcept {
    invariant(status);
    return status->error;
}

const char* capi_status_get_what(const libmongodbcapi_status* const status) noexcept {
    invariant(status);
    return status->what.c_str();
}

int capi_status_get_code(const libmongodbcapi_status* const status) noexcept {
    invariant(status);
    return status->exception_code;
}

}  // namespace
}  // namespace mongo

extern "C" {
libmongodbcapi_lib* libmongodbcapi_init(const libmongodbcapi_init_params* const params,
                                        libmongodbcapi_status* const statusPtr) {
    return mongo::capi_lib_init(params, StatusGuard(statusPtr));
}

int libmongodbcapi_fini(libmongodbcapi_lib* const lib, libmongodbcapi_status* const statusPtr) {
    StatusGuard status(statusPtr);
    mongo::capi_lib_fini(lib, status);
    return status.get().error;
}

libmongodbcapi_db* libmongodbcapi_db_new(libmongodbcapi_lib* lib,
                                         const char* const yaml_config,
                                         libmongodbcapi_status* const statusPtr) {
    return mongo::db_new(lib, yaml_config, StatusGuard(status));
}

int libmongodbcapi_db_destroy(libmongodbcapi_db* const db, libmongodbcapi_status* const statusPtr) {
    StatusGuard status(statusPtr);
    mongo::db_destroy(db, status);
    return status.get().error;
}

libmongodbcapi_client* libmongodbcapi_client_new(libmongodbcapi_db* const db,
                                                 libmongodbcapi_status* const statusPtr) {
    return mongo::client_new(db, StatusGuard(status));
}

int libmongodbcapi_client_destroy(libmongodbcapi_client* const client,
                                  libmongodbcapi_status* const statusPtr) {
    StatusGuard status(statusPtr);
    mongo::client_destroy(client, status);
    return status.get().error;
}

int libmongodbcapi_client_wire_protocol_rpc(libmongodbcapi_client* const client,
                                            const void* input,
                                            const size_t input_size,
                                            void** const output,
                                            size_t* const output_size,
                                            libmongodbcapi_status* const statusPtr) {
    StatusGuard status(statusPtr);
    mongo::client_wire_protocol_rpc(client, input, input_size, output, output_size, status);
    return status.get().error;
}


int libmongodbcapi_status_get_error(const libmongodbcapi_status* const status) {
    return mongo::capi_status_get_error(status);
}

const char* libmongodbcapi_status_get_what(const libmongodbcapi_status* const status) {
    return mongo::capi_status_get_what(status);
}

int libmongodbcapi_status_get_code(const libmongodbcapi_status* const status) {
    return mongo::capi_status_get_code(status);
}

libmongodbcapi_status* libmongodbcapi_allocate_status(void) {
    return new libmongodbcapi_status;
}

void libmongodbcapi_destroy_status(libmongodbcapi_status* const status) {
    delete status;
}

}  // extern "C"
