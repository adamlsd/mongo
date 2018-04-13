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
#ifndef LIBMONGODBCAPI_H
#define LIBMONGODBCAPI_H

#include <stddef.h>
#include <string>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An object which describes the details of the failure of an operation.
 * @note All `libmongodbcapi` functions which take a `status` object may be passed a null pointer.
 * In that case the function will not be able to report detailed status information; however, the
 * function may still be called.
 */
typedef struct libmongodbcapi_status libmongodbcapi_status;

typedef struct libmongodbcapi_lib libmongodbcapi_lib;
typedef struct libmongodbcapi_db libmongodbcapi_db;
typedef struct libmongodbcapi_client libmongodbcapi_client;

typedef enum {
    LIBMONGODB_CAPI_ERROR_UNKNOWN = -1,
    LIBMONGODB_CAPI_SUCCESS = 0,

    LIBMONGODB_CAPI_ERROR_EXCEPTION,
    LIBMONGODB_CAPI_ERROR_LIBRARY_ALREADY_INITIALIZED,
    LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED,
    LIBMONGODB_CAPI_ERROR_DB_INITIALIZATION_FAILED,
    LIBMONGODB_CAPI_ERROR_HAS_DB_HANDLES_OPEN,
    LIBMONGODB_CAPI_ERROR_DB_MAX_OPEN,
    LIBMONGODB_CAPI_ERROR_DB_CLIENTS_OPEN,
    LIBMONGODB_CAPI_ERROR_ENOMEM,
} libmongodbcapi_error;

/**
 * @return Returns a pointer to a newly allocated `libmongodbcapi_status` object which will hold
 * details of any failures of operations to which it was passed.
 * @return `NULL` when construction of a `libmongodbcapi_status` object fails.  `errno` will be set
 * with an appropriate error code, in this case.
 */
libmongodbcapi_status* libmongodbcapi_allocate_status();


/**
 * Frees the storage associated with a valid `libmongodbcapi_status` object.
 * @param status The status object to release.
 * @pre `status` must be a valid `libmongodbcapi_status` object.
 * @note This function does not report failures.
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 */

void libmongodbcapi_destroy_status(libmongodbcapi_status* status);

/**
 * Get an error code from a `libmongodbcapi_status` object.
 * @param status The `libmongodbcapi_status` object from which to get an associated error code.
 * @return Returns the `libmongodbcapi_error` code associated with the `status` parameter.
 * @note This function will report the `libmongodbcapi_error` for the failure associated with
 * `status`, therefore if the failing function returned a `libmongodbcapi_error` value, then calling
 * this function is superfluous.
 */

int libmongodbcapi_status_get_error(const libmongodbcapi_status* status);

/**
 * Get a descriptive error message from a `libmongodbcapi_status` object.
 * @param status The `libmongodbcapi_status` object from which to get an associated error message.
 * @return A null-terminated string containing an error message.
 * @note For failures where the `libmongodbcapi_error == LIBMONGODB_CAPI_ERROR_EXCEPTION`, this
 * returns a string representation of the exception
 */

const char* libmongodbcapi_status_get_what(const libmongodbcapi_status* status);

/**
 * Get a status code from a `libmongodbcapi_status` object.
 * @param status The `libmongodbcapi_status` object from which to get an associated status code.
 * @note For failures where the `libmongodbcapi_error == LIBMONGODB_CAPI_ERROR_EXCEPTION` and the
 * exception was of type `mongo::DBException`, this returns the numeric code indicating which
 * specific `mongo::DBException` was thrown
 */

int libmongodbcapi_status_get_code(const libmongodbcapi_status* status);

/**
 * Initializes the mongodbcapi library, required before any other call. Cannot be called again
 * without libmongodbcapi_fini() being called first.
 *
 * @param yaml_config Null-terminated YAML formatted MongoDB configuration. See documentation for
 * valid options.
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @note This function is not thread safe.
 *
 * @return Returns a pointer to a libmongodbcapi_lib on success.
 * @return `NULL` and modifies `status` on failure.
 */
libmongodbcapi_lib* libmongodbcapi_init(const char* yaml_config, libmongodbcapi_status* status);

/**
 * Tears down the state of the library, all databases must be closed before calling this.
 *
 * @pre All `libmongodbcapi_db` instances associated with this library handle must be destroyed.
 *
 * @param lib A pointer to a `libmongodbcapi_lib` handle which represents this library.
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @note This function is not thread safe.
 *
 * @return Returns `LIBMONGODB_CAPI_SUCCESS` on success.
 * @return Returns `LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED` and modifies `status` if
 * libmongodbcapi_init() has not been called previously.
 * @return Returns `LIBMONGODB_CAPI_ERROR_DB_MAX_OPEN` and modifies `status` if there are open
 * databases that haven't been closed with `libmongodbcapi_db_destroy()`.
 * @return Returns `LIBMONGODB_CAPI_ERROR_EXCEPTION` and modifies `status` for errors that resulted
 * in an exception. Details can be retrived via `libmongodbcapi_process_get_status()`.
 *
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 * @note This function may return diagnosic errors for violations of its preconditions, but this behavior is not guaranteed.
 */
int libmongodbcapi_fini(libmongodbcapi_lib* lib, libmongodbcapi_status* status);

/**
 * Creates an embedded MongoDB instance and returns a handle with the service context.
 *
 * @param argc The number of arguments in `argv`.
 * @param argv The arguments that will be passed to mongod at startup to initialize state.
 * @param envp Environment variables that will be passed to mongod at startup to initilize state.
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @return A pointer to a `libmongdbcapi_db` handle.
 * @return `NULL` and modifies `status` on failure.
*/
libmongodbcapi_db* libmongodbcapi_db_new(libmongodbcapi_lib* lib,
                                         int argc,
                                         const char** argv,
                                         const char** envp,
                                         libmongodbcapi_status* status);

/**
 * Shuts down an embedded MongoDB instance.
 *
 * @pre The `db` must not be `NULL`.
 * @pre All `libmongodbcapi_client` instances associated with this database must be destroyed.
 *
 * @param db A pointer to a valid `libmongodbcapi_db` instance to be destroyed.
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @return Returns `LIBMONGODB_CAPI_SUCCESS` on success.
 * @return `LIBMONGODB_CAPI_ERROR_DB_CLIENTS_OPEN` and modifies `status` if there are
 * `libmongodbcapi_client` objects still open attached to the `db`.
 * @return `LIBMONGODB_CAPI_ERROR_EXCEPTION`and modifies `status` for other unspecified errors.
 *
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 * @note This function may return diagnosic errors for violations of its precondition, but this behavior is not guaranteed.
 */
int libmongodbcapi_db_destroy(libmongodbcapi_db* db, libmongodbcapi_status* status);

/**
 * Creates a new client and returns it.
 * A client must be destroyed before the owning db is destroyed
 *
 * @param db The database that will own this client and execute its RPC calls
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @return A pointer to a client.
 * @return `NULL` on error, and modifies `status` on failure.
 */
libmongodbcapi_client* libmongodbcapi_client_new(libmongodbcapi_db* db,
                                                 libmongodbcapi_status* status);

/**
 * Destroys a client and removes it from the db context.

 * @pre The `client` must not be `NULL`.
 *
 * @param client A pointer to the client to be destroyed
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @return Returns LIBMONGODB_CAPI_SUCCESS on success.
 * @return An error code and modifies `status` on failure.
 *
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 * @note This function may return diagnosic errors for violations of its precondition, but this behavior is not guaranteed.
 */
int libmongodbcapi_client_destroy(libmongodbcapi_client* client, libmongodbcapi_status* status);

/**
 * Makes an RPC call to the database
 *
 * @param client The client that will be performing the query on the database
 * @param input The query to be sent to and then executed by the database
 * @param input_size The size (number of bytes) of the input query
 * @param output A pointer to a `void *` where the database can write the location of the output.
 * The library will manage the memory pointed to by * `output`.
 * @note The storage associated with `output` will be valid until the next call to
 * `libmongodbcapi_client_wire_protocol_rpc` on the specified `client` object.
 * @param output_size A pointer to a location where this function will write the size (number of
 * bytes) of the `output` buffer.
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @return Returns LIBMONGODB_CAPI_SUCCESS on success.
 * @return An error code and modifies `status` on failure
 */
int libmongodbcapi_client_wire_protocol_rpc(libmongodbcapi_client* client,
                                            const void* input,
                                            size_t input_size,
                                            void** output,
                                            size_t* output_size,
                                            libmongodbcapi_status* status);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
