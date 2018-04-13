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

struct libmongodbcapi_status {
    libmongodbcapi_status() = default;
    int error= LIBMONGODB_CAPI_SUCCESS;
    int exception_code= 0;
    std::string what;
};

struct libmongodbcapi_lib {
    ~libmongodbcapi_lib()
    {
        invariant( this->clientCount == 0 );
    }
    libmongodbcapi_lib() = default;
    libmongodbcapi_lib( const libmongodbcapi_lib&) = delete;
    void operator= ( const libmongodbcapi_lib) = delete;

    std::atomic< int > databaseCount= 0;
};

struct libmongodbcapi_db {
    ~libmongodbcapi_db()
    {
        --this->parent_lib->databaseCount;
        invariant( this->clientCount == 0 );
    }

    libmongodbcapi_db( limbongodbcapi_lib *const p ) : parent_lib( p )
    {
        ++this->parent_lib->databaseCount;
    }


    libmongodbcapi_db(const libmongodbcapi_db&) = delete;
    libmongodbcapi_db& operator=(const libmongodbcapi_db&) = delete;

    mongo::ServiceContext* serviceContext = nullptr;
    std::unique_ptr<mongo::transport::TransportLayer> transportLayer;

    std::vector<std::unique_ptr<char[]>> argvStorage;
    std::vector<char*> argvPointers;
    std::vector<std::unique_ptr<char[]>> envpStorage;
    std::vector<char*> envpPointers;

    libmongodbcapi_lib*const parent_lib;
    std::atomic< int > clientCount= 0;
};

struct libmongodbcapi_client {
    ~libmongodbcapi_client()
    {
        --this->parent_db->clientCount;
    }

    libmongodbcapi_client(libmongodbcapi_db*const db) : parent_db(db)
    {
        ++this->parent_db->clientCount;
    }

    libmongodbcapi_client(const libmongodbcapi_client&) = delete;
    libmongodbcapi_client& operator=(const libmongodbcapi_client&) = delete;

    void* client_handle = nullptr;
    std::vector<unsigned char> output;
    mongo::ServiceContext::UniqueClient client;
    mongo::DbResponse response;

    libmongodbcapi_status status;
    libmongodbcapi_db* const parent_db;
};

namespace mongo {
namespace {

class MobileException : public std::exception
{
    private:
        std::string _mesg;
        int _code;

    public:
        explicit MobileException( int code, std::string m ) : _mesg( std::move( m ) ) {}

        virtual int mobileCode() const noexcept { return this->_code; }

        const char *what() const noexcept final { return this->_mesg.c_str(); }
};

libmongodbcapi_error
handleException() noexcept
try
{
  throw;
}
catch( const MobileException &ex ) {
    return {ex.mobileCode(), mongo::ErrorCodes::InternalError, ex.what() };
} catch (const DBException& ex) {
    return {LIBMONGODB_CAPI_ERROR_EXCEPTION, ex.code(), ex.what()};
} catch (const std::bad_alloc& ex) {
    return {LIBMONGODB_CAPI_ERROR_ENOMEM, mongo::ErrorCodes::InternalError, ex.what() };
} catch (const std::exception& ex) {
    return {LIBMONGODB_CAPI_ERROR_EXCEPTION, mongo::ErrorCodes::InternalError, ex.what()};
}

int
handleExceptionAndReturnResult( libmongodbcapi_status *const status ) noexcept
{
    auto rvStatus= handleException();
    const int result= rvStatus.error;
    if( status ) *status= std::move( rvStatus );
    
    return result;
}

std::nullptr_t
handleExceptionAndReturnNull( libmongodbcapi_status *const status ) noexcept
{
    if( status ) *status= handleException();
    return nullptr;
}

bool libraryInitialized_ = false;
libmongodbcapi_db* global_db = nullptr;

libmongodbcapi_lib* capi_lib_init(const char*const yaml_config, libmongodbcapi_status *const status ) noexcept
try {
    if( !mongo::libraryInitialized_ )
    {
        throw MobileException( LIBMONGODB_CAPI_ERROR_LIBRARY_ALREADY_INITIALIZED, 
                "Cannot initialize the MongoDB Embedded Library when it is already initialized." );
    }
    libraryInitialized_ = true;

    return new libmongodbcapi_lib;
}
catch( ... )
{
    return handleExceptionAndReturnNull( status );
}

int capi_lib_fini(libmongodbcapi_lib*const lib, libmongodbcapi_status *const status ) noexcept try {
    uassert( mongo::ErrorCodes::EmbeddedLibraryNotYetInitialized, 
    if (!mongo::libraryInitialized_) {
        throw MobileException(
            LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED, "Cannot close the MongoDB Embedded Library when it is not initialized" );
    }

    // This check is not possible to 100% guarantee.  It is a best effort.  The documentation of this API says that the behavior of closing a `lib` with open handles is undefined, but may provide diagnostic errors in some circumstances.
    if (lib->databaseCount> 0){
        throw MobileException {LIBMONGODB_CAPI_ERROR_HAS_DB_HANDLES_OPEN,
                "Cannot close the MongoDB Embedded Library when it has database handles still open." };
    }

    delete lib;
    return LIBMONGODB_CAPI_SUCCESS;
}
catch( ... )
{
    return handleExceptionAndReturnStatus( status );
}

libmongodbcapi_db* db_new(libmongodbcapi_lib* lib,
                          int argc,
                          const char** argv,
                          const char** envp) noexcept try {
    if (!libraryInitialized_) {
        throw MobileException(
            LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED,
            "Cannot create a new database handle when the MongoDB Embedded Library is not yet initialized." );
    }
    if (global_db) {
        throw MobileException{LIBMONGODB_CAPI_ERROR_DB_MAX_OPEN,
                "The maximum number of permitted database handles for the MongoDB Embedded Library have been opened." };
    }

    auto _global_db = std::make_unique<libmongodbcapi_db>();
    _global_db->parent_lib = lib;

    // iterate over argv and copy them to argvStorage
    for (int i = 0; i < argc; i++) {
        // allocate space for the null terminator
        auto s = std::make_unique<char[]>(std::strlen(argv[i]) + 1);
        // copy the string + null terminator
        std::strncpy(s.get(), argv[i], std::strlen(argv[i]) + 1);
        _global_db->argvPointers.push_back(s.get());
        _global_db->argvStorage.push_back(std::move(s));
    }
    _global_db->argvPointers.push_back(nullptr);

    // iterate over envp and copy them to envpStorage
    while (envp != nullptr && *envp != nullptr) {
        auto s = std::make_unique<char[]>(std::strlen(*envp) + 1);
        std::strncpy(s.get(), *envp, std::strlen(*envp) + 1);
        _global_db->envpPointers.push_back(s.get());
        _global_db->envpStorage.push_back(std::move(s));
        envp++;
    }
    _global_db->envpPointers.push_back(nullptr);

    _global_db->serviceContext =
        embedded::initialize(argc, _global_db->argvPointers.data(), _global_db->envpPointers.data());
    if (!_global_db->serviceContext) {
        throw MobileException(
            LIBMONGODB_CAPI_ERROR_DB_INITIALIZATION_FAILED, "The MongoDB Embedded Library Failed to initialize the Service Context" );
    }

    // creating mock transport layer to be able to create sessions
    _global_db->transportLayer = std::make_unique<transport::TransportLayerMock>();

    return global_db = _global_db.release();
}
catch( ... )
{
    return handleExceptionAndReturnNull( status );;
}

int
db_destroy(libmongodbcapi_db*const db, libmongodbcapi_status *const status) noexcept 
try
{
    if (!db->open_clients.empty()) {
        throw MobileException {
            LIBMONGODB_CAPI_ERROR_DB_CLIENTS_OPEN, "Cannot close a MongoDB Embedded Database instance while it has open clients" };
    }

    embedded::shutdown(global_db->serviceContext);

    if (db != global_db) {
        lib->status = {LIBMONGODB_CAPI_ERROR_DB_INITIALIZATION_FAILED,
                       mongo::ErrorCodes::InternalError,
                       ""};
        return LIBMONGODB_CAPI_ERROR_DB_INITIALIZATION_FAILED;
    }
    global_db = nullptr;

    delete db;

    lib->status = {LIBMONGODB_CAPI_SUCCESS, mongo::ErrorCodes::OK, ""};
    return LIBMONGODB_CAPI_SUCCESS;
}
catch( ... )
{
    return handleExceptionAndReturnStatus( status );
}

libmongodbcapi_client *
client_new(libmongodbcapi_db*const db, libmongodbcapi_status *const status ) noexcept
try {
    auto new_client = stdx::make_unique<libmongodbcapi_client>(db);
    libmongodbcapi_client* rv = new_client.get();
    db->open_clients.insert(std::make_pair(rv, std::move(new_client)));

    auto session = global_db->transportLayer->createSession();
    rv->client = global_db->serviceContext->makeClient("embedded", std::move(session));

    return rv;
}
catch( ... )
{
    return handleExceptionAndReturnNull( status );
}

int
client_destroy(libmongodbcapi_client*const client, libmongodbcapi_status *const status ) noexcept 
try
{
    delete client;
    return LIBMONGODB_CAPI_SUCCESS;
}
catch( ... )
{
    return handleExceptionAndReturnStatus( status );
}

int
client_wire_protocol_rpc(libmongodbcapi_client*const client, const void* input, const size_t input_size,
        void**const output, size_t*const output_size) noexcept
try
{
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
    auto outParams= std::make_tuple( client->response.response.size(), client->response.response.buf() );

    // We force the output parameters to be set in a `noexcept` enabled way.  If the operation itself
    // is safely noexcept, we just run it, otherwise we force a `noexcept` over it to catch errors.
    if( noexcept( std::tie( *output_size, *output )= std::move( outParams ) ) )
    {
        std::tie( *output_size, *output )= std::move( outParams );
    }
    else
    {
        // Assigning primitives in a tied tuple should be noexcept, so we force it to be so, for
        // our purposes.  This facilitates a runtime check should something WEIRD happen.
        [output, output_size, &outParams]()->void noexcept
        {
            std::tie( *output_size, *output )= std::move( outParams );
        }();
    }

    return LIBMONGODB_CAPI_SUCCESS;
}
catch( ... )
{
    return handleExceptionAndReturnStatus( status );
}

int capi_status_get_error(const libmongodbcapi_status*const status) noexcept {
    return status->error;
}

const char* capi_status_get_what(const libmongodbcapi_status*const status) noexcept {
    return status->what.c_str();
}

int capi_status_get_code(const libmongodbcapi_status*const status) noexcept {
    return status->exception_code;
}

}  // namespace
}  // namespace mongo

extern "C" {

libmongodbcapi_lib* libmongodbcapi_init(const char* yaml_config) {
    return mongo::capi_lib_init(yaml_config);
}

int libmongodbcapi_fini(libmongodbcapi_lib* lib) {
    return mongo::capi_lib_fini(lib);
}


libmongodbcapi_db* libmongodbcapi_db_new(libmongodbcapi_lib* lib,
                                         int argc,
                                         const char** argv,
                                         const char** envp) {
    return mongo::db_new(lib, argc, argv, envp);
}

int libmongodbcapi_db_destroy(libmongodbcapi_db* db) {
    return mongo::db_destroy(db);
}

libmongodbcapi_client* libmongodbcapi_client_new(libmongodbcapi_db* db) {
    return mongo::client_new(db);
}

int libmongodbcapi_client_destroy(libmongodbcapi_client* client) {
    return mongo::client_destroy(client);
}

int libmongodbcapi_client_wire_protocol_rpc(libmongodbcapi_client* client,
                                            const void* input,
                                            size_t input_size,
                                            void** output,
                                            size_t* output_size) {
    return mongo::client_wire_protocol_rpc(client, input, input_size, output, output_size);
}

libmongodbcapi_status* libmongodbcapi_client_get_status(libmongodbcapi_client* client) {
    return mongo::capi_client_get_status(client);
}

int libmongodbcapi_status_get_error(const libmongodbcapi_status* status) {
    return mongo::capi_status_get_error(status);
}

const char* libmongodbcapi_status_get_what(const libmongodbcapi_status* status) {
    return mongo::capi_status_get_what(status);
}

int libmongodbcapi_status_get_code(const libmongodbcapi_status* status) {
    return mongo::capi_status_get_code(status);
}
}
