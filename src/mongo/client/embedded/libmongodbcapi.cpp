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

#include <exception>
#include <thread>
#include <unordered_map>
#include <vector>

#include "mongo/db/client.h"
#include "mongo/db/dbmain.h"
#include "mongo/db/service_context.h"
#include "mongo/stdx/memory.h"
#include "mongo/stdx/unordered_map.h"
#include "mongo/transport/service_entry_point.h"
#include "mongo/transport/transport_layer_mock.h"
#include "mongo/util/assert_util.h"
#include "mongo/util/net/message.h"
#include "mongo/util/scopeguard.h"
#include "mongo/util/shared_buffer.h"


class ProcessContext {};

ProcessContext* globalProcessContext = nullptr;

struct libmongodbcapi_v1_db {
    libmongodbcapi_v1_db() = default;

    libmongodbcapi_v1_db(const libmongodbcapi_v1_db&) = delete;
    libmongodbcapi_v1_db& operator=(const libmongodbcapi_v1_db&) = delete;

    mongo::ServiceContext* serviceContext = nullptr;
    mongo::stdx::thread mongodThread;
    std::unique_ptr<mongo::transport::TransportLayerMock> transportLayer;

    std::vector<std::string> argvStorage;
    std::vector<char*> argvPointers;
    std::vector<std::string> envpStorage;
    std::vector<char*> envpPointers;
};

struct libmongodbcapi_v1_client {
    explicit libmongodbcapi_v1_client(libmongodbcapi_v1_db* db) : parent_db(db) {}

    libmongodbcapi_v1_client(const libmongodbcapi_v1_client&) = delete;
    libmongodbcapi_v1_client& operator=(const libmongodbcapi_v1_client&) = delete;

    void* client_handle = nullptr;
    std::vector<unsigned char> output;
    libmongodbcapi_v1_db* parent_db = nullptr;
    mongo::ServiceContext::UniqueClient client;
    mongo::DbResponse response;
};

namespace mongo {
namespace {

libmongodbcapi_v1_db* global_db = nullptr;
thread_local int last_error;
bool run_setup = false;

std::unique_ptr<libmongodbcapi_v1_db> db_new(int argc, const char** argv, const char** envp) {
    if (global_db) {
        throw std::runtime_error("DB already exists");
    }
    auto rv = std::make_unique<libmongodbcapi_v1_db>();

    if (!run_setup) {
        // iterate over argv and copy them to argvStorage
        for (int i = 0; i < argc; i++) {
            // copy the string + null terminator
            rv->argvStorage.push_back(argv[i]);
            rv->argvPointers.push_back(&rv->argvStorage.back()[0]);
        }

        // iterate over envp and copy them to envpStorage
        while (envp != nullptr && *envp != nullptr) {
            rv->envpStorage.push_back(*++envp);
            rv->envpPointers.push_back(&rv->envpStorage.back()[0]);
            envp++;
        }
        rv->envpPointers.push_back(nullptr);

        // call mongoDbMain() in a new thread because it currently does not terminate
        rv->mongodThread = stdx::thread(
            [argc, &rv] { mongoDbMain(argc, rv->argvPointers.data(), rv->envpPointers.data()); });
        rv->mongodThread.detach();

        // wait until the global service context is not null
        rv->serviceContext = waitAndGetGlobalServiceContext();

        // block until the global service context is initialized
        rv->serviceContext->waitForStartupComplete();

        run_setup = true;
    } else {
        // wait until the global service context is not null
        rv->serviceContext = waitAndGetGlobalServiceContext();
    }
    // creating mock transport layer
    rv->transportLayer = std::make_unique<transport::TransportLayerMock>();

    global_db = rv.get();
    return rv;
}

void db_destroy(libmongodbcapi_v1_db* const db) {
    delete db;
    invariant(db || db == global_db);
    if (db) {
        global_db = nullptr;
    }
}

void db_pump(libmongodbcapi_v1_db* const db) {}

std::unique_ptr<libmongodbcapi_v1_client> client_new(libmongodbcapi_v1_db* db) {
    auto new_client = stdx::make_unique<libmongodbcapi_v1_client>(db);
    libmongodbcapi_v1_client* rv = new_client.get();

    auto session = global_db->transportLayer->createSession();
    rv->client = global_db->serviceContext->makeClient("embedded", std::move(session));
    return new_client;
}

void client_destroy(libmongodbcapi_v1_client* client) {
    last_error = LIBMONGODB_CAPI_V1_ERROR_SUCCESS;
    if (!client) {
        return;
    }
}

void client_wire_protocol_rpc(libmongodbcapi_v1_client* client,
                              const void* input,
                              size_t input_size,
                              void** output,
                              size_t* output_size) {
    mongo::Client::setCurrent(std::move(client->client));
    const auto guard = mongo::MakeGuard([&] { client->client = mongo::Client::releaseCurrent(); });

    auto opCtx = cc().makeOperationContext();
    auto sep = client->parent_db->serviceContext->getServiceEntryPoint();

    auto sb = SharedBuffer::allocate(input_size);
    memcpy(sb.get(), input, input_size);

    Message msg(std::move(sb));

    client->response = sep->handleRequest(opCtx.get(), msg);
    *output_size = client->response.response.size();
    *output = client->response.response.buf();
}

int get_last_capi_error() noexcept {
    return last_error;
}
}  // namespace
}  // namespace mongo

namespace {
libmongodbcapi_v1_db* v1_db_new(int argc, const char** argv, const char** envp) noexcept try {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_SUCCESS;
    return mongo::db_new(argc, argv, envp).release();
} catch (const std::exception&) {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_UNKNOWN;
    return nullptr;
}

void v1_db_destroy(libmongodbcapi_v1_db* const db) noexcept try {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_SUCCESS;
    mongo::db_destroy(db);
} catch (const std::exception&) {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_UNKNOWN;
}

int v1_db_pump(libmongodbcapi_v1_db* const p) noexcept try {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_SUCCESS;
    mongo::db_pump(p);
    return 0;
} catch (const std::exception&) {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_UNKNOWN;
    return -1;
}

libmongodbcapi_v1_client* v1_db_client_new(libmongodbcapi_v1_db* const db) noexcept try {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_SUCCESS;
    return mongo::client_new(db).release();
} catch (const std::exception&) {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_UNKNOWN;
    return nullptr;
}

void v1_db_client_destroy(libmongodbcapi_v1_client* client) noexcept try {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_SUCCESS;
    return mongo::client_destroy(client);
} catch (const std::exception&) {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_UNKNOWN;
}

int v1_db_client_wire_protocol_rpc(libmongodbcapi_v1_client* const client,
                                   const void* const input,
                                   const size_t input_size,
                                   void** const output,
                                   size_t* const output_size) noexcept try {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_SUCCESS;
    mongo::client_wire_protocol_rpc(client, input, input_size, output, output_size);
    return 0;
} catch (const std::exception&) {
    mongo::last_error = LIBMONGODB_CAPI_V1_ERROR_UNKNOWN;
    return -1;
}

int v1_get_last_capi_error() noexcept {
    return mongo::get_last_capi_error();
}
}  // namespace

extern "C" {
libmongodbcapi_v1_db* libmongodbcapi_v1_db_new(const int argc,
                                               const char** const argv,
                                               const char** const envp) {
    return v1_db_new(argc, argv, envp);
}

void libmongodbcapi_v1_db_destroy(libmongodbcapi_v1_db* const db) {
    return v1_db_destroy(db);
}

int libmongodbcapi_v1_db_pump(libmongodbcapi_v1_db* const p) {
    return v1_db_pump(p);
}

libmongodbcapi_v1_client* libmongodbcapi_v1_db_client_new(libmongodbcapi_v1_db* const db) {
    return v1_db_client_new(db);
}

void libmongodbcapi_v1_db_client_destroy(libmongodbcapi_v1_client* const client) {
    v1_db_client_destroy(client);
}

int libmongodbcapi_v1_db_client_wire_protocol_rpc(libmongodbcapi_v1_client* const client,
                                                  const void* const input,
                                                  const size_t input_size,
                                                  void** const output,
                                                  size_t* const output_size) {
    return v1_db_client_wire_protocol_rpc(client, input, input_size, output, output_size);
}

int libmongodbcapi_v1_get_last_error() {
    return v1_get_last_capi_error();
}
}  // extern "C"
