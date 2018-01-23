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

#ifdef __cplusplus
extern "C" {
#endif

typedef struct libmongodbcapi_v1_db libmongodbcapi_v1_db;
typedef struct libmongodbcapi_v1_client libmongodbcapi_v1_client;

typedef enum {
    LIBMONGODB_CAPI_V1_ERROR_UNKNOWN = -1,
    LIBMONGODB_CAPI_V1_ERROR_SUCCESS
} libmongodbcapi_v1_error;

/**
 * @return a per-thread value indicating the last error
 */
int libmongodbcapi_v1_get_last_error(void);

/**
 * Starts the embedded database library.
 *
 * @note This function must be called before any other functions in this library.
 * @note This function cannot be called again, unless the library has been deinitialized using the
 *       `libmongodbcapi_v1_fini()` function.  The results of multiple calls to this function without
 *       intermediate calls to `libmongodbcapi_v1_fini()` are undefined.
 * @return 0 on success -1 on error.
 */
int libmongodbcapi_v1_init(int argc, const char **argv, const char **envp);

/**
 * Terminates the embedded database library.
 *
 * @note This function must be called before the program is shutdown.  The effects of program
 *       termination without calling this function are undefined.
 * @note This function cannot be called multiple times, unless the library has been reinitialized
 *       using the `libmongodbcapi_vi_init()` function.
 */
void libmongodbcapi_v1_fini(void);

/**
 * Starts the database and returns a handle with the service context.
 *
 * @param argc
 *      The number of arguments in argv
 * @param argv
 *      The arguments that will be passed to mongod at startup to initialize state
 * @param envp
 *      Environment variables that will be passed to mongod at startup to initilize state
 *
 * @return A pointer to a db handle or null on error
 */
libmongodbcapi_v1_db* libmongodbcapi_v1_db_new(int argc, const char** argv, const char** envp);

/**
 * Shuts down the database
 *
 * @param db
 *      A pointer to a db handle to be destroyed
 */
void libmongodbcapi_v1_db_destroy(libmongodbcapi_v1_db* db);

/**
 * Let the database do background work. Returns an int from the error enum
 *
 * @param db
 *      The database that has work that needs to be done
 *
 * @return 0 on success -1 on error.
 */
int libmongodbcapi_v1_db_pump(libmongodbcapi_v1_db* db);

/**
 * Creates a new clienst and retuns it so the caller can do operation
 * A client will be destroyed when the owning db is destroyed
 *
 * @param db
 *      The datadase that will own this client and execute its RPC calls
 *
 * @return A pointer to a client or null on error
 */
libmongodbcapi_v1_client* libmongodbcapi_v1_db_client_new(libmongodbcapi_v1_db* db);

/**
 * Destroys a client and removes it from the db/service context
 * Cannot be called after the owning db is destroyed
 *
 * @param client
 *      A pointer to the client to be destroyed
 */
void libmongodbcapi_v1_db_client_destroy(libmongodbcapi_v1_client* client);
 
/**
 * Makes an RPC call to the database
 *
 * @param client
 *      The client that will be performing the query on the database
 * @param input
 *      The query to be sent to and then executed by the database
 * @param input_size
 *      The size (number of bytes) of the input query
 * @param output
 *      A pointer to a void * where the database can write the location of the output.  The library
 *      will manage the memory pointed to by `*output` -- do not call free on this pointer..  The
 *      contents of this buffer will be valid until the next call to
 *      `libmongodbcapi_v1_db_client_wire_protocol_rpc`.  The caller may choose to leave the data
 *      
 * @param output_size
 *      A pointer to a location where this function will write the size (number of bytes) of the
 *      output
 *
 * @return 0 on success -1 on error.
 */
int libmongodbcapi_v1_db_client_wire_protocol_rpc(libmongodbcapi_v1_client* client,
                                                  const void* input,
                                                  size_t input_size,
                                                  void** output,
                                                  size_t* output_size);

#ifdef __cplusplus
}
#endif

#endif
