/*-
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
#include <stdint.h>

#ifdef _DOXYGEN
/**
 * Embeddable MongoDB Library.
 *
 * @invariant All functions in this library (those `extern "C"` functions starting with
 * `libmongodbcapi_` in their names) have undefined behavior unless their thread safety requirements
 * are met.
 *
 * We define "Thread Safety" to mean that a program will not exhibit undefined behavior in multiple
 * concurrent execution contexts over this library.  Please note, however, that values returned from
 * a function may be stale, if the parameter objects passed to that function are subsequently passed
 * to any function in another thread.  Although the library will not exhibit undefined behavior, the
 * program may not function as desired.
 *
 * @note The definition of "undefined behavior" with respect to this library includes any
 * undocumented result up to and including undefined behavior of the entire program under the C and
 * C++ language standards.
 * @note The specification of post-conditions in this library only holds if undefined behavior does
 * not occur.
 * @note Some functions provide runtime diagnostics for some violations of their preconditions --
 * this behavior is not guaranteed and is provided as a convenience for both debugging and
 * protection of data integrity.
 */
namespace LibMongoDBCAPI {
// Doxygen requires a namespace when processing global scope functions, in order to generate
// documentation.  We also use it as a hook to provide library-wide documentation.
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * An object which describes the details of the failure of an operation.
 *
 * The `libmongodbcapi` library uses allocated objects of this type to report the details of any
 * failure, when an operation cannot be completed.  Several `libmongodbcapi_status` functions are
 * provided which permit observing the details of these failures.  Further a construction function
 * and a destruction function for these objects are also provided.
 *
 * @invariant The use of `libmongodbcapi_status` objects from multiple threads is not threadsafe
 * unless all of the threads accessing a single `libmongodbcapi_status` object are passing that
 * object as a const-qualified (`const libmongodbcapi_status *`) pointer.  If a single thread is
 * passing a `libmongodbcapi_status` object a function taking it by non-const-qualified
 * (`libmongodbcapi_status *`) pointer, then no other thread may access the `libmongodbcapi_status`
 * object.
 *
 * @note All `libmongodbcapi_` functions which take a `status` object may be passed a `NULL`
 * pointer.  In that case the function will not be able to report detailed status information;
 * however, that function may still be called.
 *
 * @note All `libmongodbcapi_status` functions can be used before the `libmongodbcapi` library is
 * initialized.  This facilitates detailed error reporting from all library functions.
 */
typedef struct libmongodbcapi_status libmongodbcapi_status;

/**
 * Allocate and construct an API-return-status buffer object of type `libmongodbcapi_status`.
 *
 * All `libmongodbcapi_` functions outside of the `libmongodbcapi_status` family accept pointers to
 * these objects (specifically a parameter of type `libmongodbcapi_status *`).  These functions use
 * that output-parameter as a mechanism for detailed error reporting.  If a null pointer is passed,
 * then these functions will not be able to report the details of their error.
 *
 * @pre None.
 *
 * @returns A pointer to a newly allocated `libmongodbcapi_status` object which will hold details of
 * any failures of operations to which it was passed.
 * @returns `NULL` when construction of a `libmongodbcapi_status` object fails.
 *
 * @invariant This function is completely threadsafe.
 *
 * @note It is possible to use the rest of the `libmongodbcapi` functions without status objects if
 * detailed error reporting is unnecessary; however, if allocation of status objects fails it is
 * likely that all other `libmongodbcapi` operations will fail as well.
 * @note Allocation of an Embedded MongoDB Status buffer should rarely fail, except for
 * out-of-memory reasons.
 *
 * @note This function may be called before `libmongodbcapi_lib_init`.
 */
libmongodbcapi_status* libmongodbcapi_status_create(void);

/**
 * Frees the storage associated with a valid `libmongodbcapi_status` object.
 *
 * @pre The specified `status` object must not be `NULL`.
 * @pre The specified `status` object must be a valid `libmongodbcapi_status` object.
 *
 * @param status The `libmongodbcapi_status` object to release.
 *
 * @invariant This function not threadsafe unless the specified `status` object is not passed
 * concurrently to any other function.  It is safe to destroy distinct `libmongodbcapi_status`
 * objects on distinct threads.
 *
 * @note This function does not report failures.
 * @note This behavior of this function is undefined unless is its preconditions are met.
 *
 * @note This function may be called before `libmongodbcapi_lib_init`.
 *
 * @note This function causes all storage associated with the specified `status` to be released,
 * including the storage referenced by functions that returned observable storage buffers from this
 * status, such as strings.
 */
void libmongodbcapi_status_destroy(libmongodbcapi_status* status);

/**
 * The error codes reported by `libmongodbcapi` functions will be given the symbolic names as mapped
 * by this enum.
 *
 * When a `limbongdbcapi` function fails (and it has been documented report errors) it will report
 * that error in the form of an `int` status code.  That status code will always be returned as the
 * type `int`; however, the values in this enum can be used to classify the failure.
 */
typedef enum {
    LIBMONGODB_CAPI_ERROR_IN_REPORTING_ERROR = -2,
    LIBMONGODB_CAPI_ERROR_UNKNOWN = -1,
    LIBMONGODB_CAPI_SUCCESS = 0,

    // Note to maintainers: Do not re-order the names in this list.  The value of each error is part
    // of the abi and its position determines its value.

    LIBMONGODB_CAPI_ERROR_ENOMEM,
    LIBMONGODB_CAPI_ERROR_EXCEPTION,
    LIBMONGODB_CAPI_ERROR_LIBRARY_ALREADY_INITIALIZED,
    LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED,
    LIBMONGODB_CAPI_ERROR_INVALID_LIB_HANDLE,
    LIBMONGODB_CAPI_ERROR_DB_INITIALIZATION_FAILED,
    LIBMONGODB_CAPI_ERROR_INVALID_DB_HANDLE,
    LIBMONGODB_CAPI_ERROR_HAS_DB_HANDLES_OPEN,
    LIBMONGODB_CAPI_ERROR_DB_MAX_OPEN,
    LIBMONGODB_CAPI_ERROR_DB_CLIENTS_OPEN,
    LIBMONGODB_CAPI_ERROR_INVALID_CLIENT_HANDLE,
    LIBMONGODB_CAPI_ERROR_REENTRANCY_NOT_ALLOWED,
} libmongodbcapi_error;

/**
 * Gets an error code from a `libmongodbcapi_status` object.
 *
 * When a `libmongodbcapi` function fails (and it has been documented to report errors) it will
 * report its error in the form of an `int` status code which is stored into a supplied
 * `libmongodbcapi_status` object, if provided.  Some of these functions may also report extra
 * information, which will be reported by other observer functions.  Every `libmongodbcapi` function
 * which reports errors will always update the `Error` code stored in a `libmongodbcapi_status`
 * object, even upon success.
 *
 * @pre The specified `status` object must not be `NULL`.
 * @pre The specified `status` object must be a valid `libmongodbcapi_status` object.
 * @pre The specified `status` object must have been passed to a `libmongodbcapi` function.
 *
 * @param status The `libmongodbcapi_status` object from which to get an associated error code.
 *
 * @returns `LIBMONGODB_CAPI_SUCCESS` if the last function to which `status` was passed succeeded.
 * @returns The `libmongodbcapi_error` code associated with the `status` parameter.
 *
 * @invariant This function is thread-safe, if the thread safety requirements specified by
 * `libmongodbcapi_status`'s invariants are met.
 *
 * @note This function will report the `libmongodbcapi_error` value for the failure associated with
 * `status`, therefore if the failing function returned a `libmongodbcapi_error` value itself, then
 * calling this function is superfluous.
 *
 * @note This function does not report its own failures.
 * @note This behavior of this function is undefined unless is its preconditions are met.
 */
int libmongodbcapi_status_get_error(const libmongodbcapi_status* status);

/**
 * Gets a descriptive error message from a `libmongodbcapi_status` object.
 *
 * Any `libmongodbcapi` function which reports failure must, when it fails, update the specified
 * `libmongodbcapi_status` object, if it exists, to contain a string indicating a user-readable
 * description of the failure.  This error message string is dependent upon the kind of error
 * encountered and may contain dynamically generated information describing the specifics of the
 * failure.
 *
 * @pre The specified `status` must not be `NULL`.
 * @pre The specified `status` must be a valid `libmongodbcapi_status` object.
 * @pre The specified `status` must have been passed to a `libmongodbcapi` function.
 * @pre The function to which the specified `status` was passed must not have returned
 * `LIBMONGODB_CAPI_SUCCESS` as its error code.
 *
 * @param status The `libmongodbcapi_status` object from which to get an associated error message.
 *
 * @returns A null-terminated string containing an error message.  This string will be valid until
 * the next time that the specified `status` is passed to any other `libmongodbcapi` function
 * (including those in the `libmongodbcapi_status` family).
 *
 * @invariant This function is thread-safe, if the thread safety requirements specified by
 * `libmongodbcapi_status`'s invariants are met; however, the pointer returned by this function is
 * considered to be part of the specified `status` object for the purposes of thread safety.  If the
 * `libmongodbcapi_status` is changed, by any thread, it will invalidate the string returned by this
 * function.
 *
 * @note For failures where the `libmongodbcapi_status_cet_error( status ) ==
 * LIBMONGODB_CAPI_ERROR_EXCEPTION`, this returns a string representation of the internal C++
 * exception.
 *
 * @note The storage for the returned string is associated with the specified `status` object, and
 * therefore it will be deallocated when the `status` is destroyed using
 * `libmongodbcapi_destroy_status`.
 *
 * @note This function does not report its own failures.
 * @note This behavior of this function is undefined unless is its preconditions are met.
 */
const char* libmongodbcapi_status_get_what(const libmongodbcapi_status* status);

/**
 * Gets a status code from a `libmongodbcapi_status` object.
 *
 * Any `libmongodbcapi` function which reports failure must, when it fails, update the specified
 * `libmongodbcapi_status` object, if it exists, to contain a numeric code indicating a sub-category
 * of failure.  This error code is one specified by the normal MongoDB Driver interface.
 *
 * @pre The specified `status` must not be `NULL`.
 * @pre The specified `status` must be a valid `libmongodbcapi_status` object.
 * @pre The specified `status` must have been passed to a `libmongodbcapi` function.
 * @pre The function to which the specified `status` was passed must not have returned
 * `LIBMONGODB_CAPI_SUCCESS` as its error code.
 *
 * @param status The `libmongodbcapi_status` object from which to get an associated status code.
 *
 * @returns A numeric status code associated with the `status` parameter.
 *
 * @invariant This function is thread-safe, if the thread safety requirements specified by
 * `libmongodbcapi_status`'s invariants are met.
 *
 * @note For failures where the `libmongodbcapi_error == LIBMONGODB_CAPI_ERROR_EXCEPTION` and the
 * exception was of type `mongo::DBException`, this returns the numeric code indicating which
 * specific `mongo::DBException` was thrown
 *
 * @note This function does not report its own failures.
 * @note This behavior of this function is undefined unless is its preconditions are met.
 */
int libmongodbcapi_status_get_code(const libmongodbcapi_status* status);


/**
 * An object which describes the runtime state of the Embedded MongoDB Library.
 *
 * The `libmongodbcapi` library uses allocated objects of this type to indicate the present state of
 * the library.  Some operations which the library provides need access to this object.  Further a
 * construction function and a destruction function for these objects are also provided.  No more
 * than a single object instance of this type will exist at any given time.
 *
 * @invariant The use of `libmongodbcapi_lib` objects from multiple threads is not threadsafe unless
 * all of the threads accessing a single `libmongodbcapi_lib` object are not destroying this object.
 * If a single thread is passing a `libmongodbcapi_lib` to its destruction function, then no other
 * thread may access the `libmongodbcapi_status` object.
 */
typedef struct libmongodbcapi_lib libmongodbcapi_lib;

/**
 * An object which describes the runtime state of the Embedded MongoDB Library.
 *
 * The `libmongodbcapi` library uses structures of this type to indicate the desired configuration
 * of the library.
 *
 * @invariant Because the library is only initialized once, in a single-threaded fashion, there are
 * no thread-safety requirements on this type.
 */
typedef struct libmongodbcapi_init_params libmongodbcapi_init_params;

/**
 * Log callback. For details on what the parameters mean, see the documentation at
 * https://docs.mongodb.com/manual/reference/log-messages/
 *
 * Severity values, lower means more severe.
 * Severe/Fatal = -4
 * Error = -3
 * Warning = -2
 * Info = -1
 * Log = 0
 * Debug = 1 to 5
 */
typedef void (*libmongodbcapi_log_callback)(
    void* user_data, const char* message, const char* component, const char* context, int severity);

/**
 * Valid bits for the log_flags bitfield in libmongodbcapi_init_params.
 */
typedef enum {
    /** Placeholder for no logging */
    LIBMONGODB_CAPI_LOG_NONE = 0,

    /** Logs to stdout */
    LIBMONGODB_CAPI_LOG_STDOUT = 1,

    /** Logs to stderr (not supported yet) */

    // LIBMONGODB_CAPI_LOG_STDERR = 2,

    /** Logs via log callback that must be provided when this bit is set. */
    LIBMONGODB_CAPI_LOG_CALLBACK = 4
} libmongodbcapi_log_flags;

// See the documentation of this object on the comments above its forward declaration
struct libmongodbcapi_init_params {
    /**
     * Optional null-terminated YAML formatted MongoDB configuration string.
     * See documentation for valid options.
     */
    const char* yaml_config;

    /**
     * Bitfield of log destinations, accepts values from libmongodbcapi_log_flags.
     * Default is stdout.
     */
    uint64_t log_flags;

    /**
     * Optional log callback to the mongodbcapi library, it is not allowed to reentry the
     * mongodbcapi library from the callback.
     */
    libmongodbcapi_log_callback log_callback;

    /**
     * Optional user data to be returned in the log callback.
     */
    void* log_user_data;
};

/**
 * Initializes the mongodbcapi library, required before any other call.
 *
 * The Embedded MongoDB Library must be initialized before it can be used.  However, it is
 * permissible to create and destroy `libmongodbcapi_status` objects without the library having been
 * initialized.  Initializing the library sets up internal state for all Embedded MongoDB Library
 * operations, including creating embedded "server-like" instances and creating clients.
 *
 * @pre The specified `params` object must either be a valid `libmongodbcapi_lib_params` object (in
 * a valid state) or `NULL`.
 * @pre The specified `status` object must either be a valid `libmongodbcapi_status` object or
 * `NULL`.
 * @pre Either `limbongodbcapi_fini` must have never been called in this process, or it was called
 * and returned success and `libmongodbcapi_init` was not called after this.
 * @pre Either `limbongodbcapi_init` must have never been called in this process, or it was called
 * and then the embedded library was terminated by a successful call to `libmongodbcapi_fini`.
 * @pre No valid `libmongodbcapi_lib` must exist.
 *
 * @param params A pointer to libmongodbcapi_init_params containing library initialization
 * parameters.  A default configuration will be used if `params == NULL`.
 *
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @post Either the Embedded MongoDB Library will be initialized, or an error will be reported.
 *
 * @returns A pointer to a `libmongodbcapi_lib` object on success.
 * @returns `NULL` and modifies `status` on failure.
 *
 * @invariant This function is not thread safe.  It must be called and have completed before any
 * other non-`limbongodbcapi_status` operations can be called on any thread.
 *
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 * @note This function may return diagnosic errors for violations of its preconditions, but this
 * behavior is not guaranteed.
 */
libmongodbcapi_lib* libmongodbcapi_init(const libmongodbcapi_init_params* params,
                                        libmongodbcapi_status* status);

/**
 * Tears down the state of the library, all databases must be closed before calling this.
 *
 * The Embedded MongoDB Library must be quiesced before the containg process can be safely
 * terminated.  Dataloss is not a risk; however, some database repair routines may be executed at
 * next initialization if the library is not properly quiesced.  It is permissible to create and
 * destroy `libmongodbcapi_status` objects after the library has been quiesced.  The library may be
 * re-initialized with a potentially different configuration after it has been queisced.
 *
 * @pre All `libmongodbcapi_db` instances associated with this library handle must be destroyed.
 * @pre The specified `lib` object must not be `NULL`.
 * @pre The specified `lib` object must be a valid `libmongodbcapi_lib` object.
 * @pre The specified `status` object must either be a valid `libmongodbcapi_status` object or
 * `NULL`.
 *
 * @param lib A pointer to a `libmongodbcapi_lib` handle which represents this library.
 *
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @post Either the Embedded MongoDB Library will be deinitialized, or an error will be reported.
 *
 * @returns Returns `LIBMONGODB_CAPI_SUCCESS` on success.
 * @returns Returns `LIBMONGODB_CAPI_ERROR_LIBRARY_NOT_INITIALIZED` and modifies `status` if
 * libmongodbcapi_init() has not been called previously.
 * @returns Returns `LIBMONGODB_CAPI_ERROR_DB_MAX_OPEN` and modifies `status` if there are open
 * databases that haven't been closed with `libmongodbcapi_db_destroy()`.
 * @returns Returns `LIBMONGODB_CAPI_ERROR_EXCEPTION` and modifies `status` for errors that resulted
 * in an exception. Details can be retrived via `libmongodbcapi_process_get_status()`.
 *
 * @invariant This function is not thread safe.  It cannot be called concurrently with any other
 * non-`libmongodbcapi_status` operation.
 *
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 * @note This function may return diagnosic errors for violations of its preconditions, but this
 * behavior is not guaranteed.
 */
int libmongodbcapi_fini(libmongodbcapi_lib* lib, libmongodbcapi_status* status);

/**
 * An object which represents an instance of an Embedded MongoDB Server.
 *
 * The Embedded MongoDB Library uses allocated objects of this type (`limbongodbcapi_db`) to
 * indicate the present state of a single "server-like" MongoDB instance.  Some operations which the
 * library provides need access to this object.  Further a construction function and a destruction
 * function for these objects are also provided.  No more than a single object instance of this type
 * will exist at any given time.
 *
 * @invariant The use of `libmongodbcapi_db` objects from multiple threads is not threadsafe unless
 * all of the threads accessing a single `libmongodbcapi_db` object are not destroying this object.
 * If a single thread is passing a `libmongodbcapi_db` to its destruction function, then no other
 * thread may access the `libmongodbcapi_db` object.
 */
typedef struct libmongodbcapi_db libmongodbcapi_db;

/**
 * Creates an embedded MongoDB instance and returns a handle with the service context.
 *
 * A `libmongodbcapi_db` object which represents a single embedded "server-like" context is created
 * and returned by this function.  At present, only a single server-like instance is supported;
 * however, multiple concurrent "server-like" instances may be permissible in future releases.
 *
 * @pre The specified `lib` object must not be `NULL`
 * @pre The specified `lib` object must be a valid `libmongodbcapi_lib` object.
 * @pre The specified `yaml_config` string must either point to an ASCII null-terminated string or
 * be `NULL`.
 * @pre The specified `status` object must be either a valid `libmongodbcapi_status` object or
 * `NULL`.
 *
 * @param yaml_config A null-terminated YAML formatted MongoDB configuration. See documentation for
 * valid options.
 *
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @post Either a new Embedded MongoDB Server will be created, or an error will be reported.
 *
 * @return A pointer to a `libmongdbcapi_db` handle.
 * @return `NULL` and modifies `status` on failure.
 *
 * @invariant This function is completely threadsafe, as long as its preconditions are met.
 *
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 * @note This function may return diagnosic errors for violations of its preconditions, but this
 * behavior is not guaranteed.
 */
libmongodbcapi_db* libmongodbcapi_db_new(libmongodbcapi_lib* lib,
                                         const char* yaml_config,
                                         libmongodbcapi_status* status);

/**
 * Shuts down an embedded MongoDB instance.
 *
 * A `libmongodbcapi_db` embedded "server-like" instance can be terminated by this function.  All
 * resources used by this instance will be released, and all background tasks associated with it
 * will be terminated.
 *
 * @pre The `db` must not be `NULL`.
 * @pre The specified `status` object must be either a valid `libmongodbcapi_status` object or
 * `NULL`.
 * @pre All `libmongodbcapi_client` instances associated with this database must be destroyed.
 *
 * @param db A pointer to a valid `libmongodbcapi_db` instance to be destroyed.
 *
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @post Either the specified Embedded MongoDB Server will be destroyed, or an error will be
 * reported.
 *
 * @returns `LIBMONGODB_CAPI_SUCCESS` on success.
 * @returns `LIBMONGODB_CAPI_ERROR_DB_CLIENTS_OPEN` and modifies `status` if there are
 * `libmongodbcapi_client` objects still open attached to the `db`.
 * @returns `LIBMONGODB_CAPI_ERROR_EXCEPTION`and modifies `status` for other unspecified errors.
 *
 * @invariant This function not threadsafe unless the specified `db` object is not passed
 * concurrently to any other function.  It is safe to destroy distinct `libmongodbcapi_db` objects
 * on distinct threads.
 *
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 * @note This function may return diagnosic errors for violations of its precondition, but this
 * behavior is not guaranteed.
 */
int libmongodbcapi_db_destroy(libmongodbcapi_db* db, libmongodbcapi_status* status);

/**
 * An object which represents "client connection" to an Embedded MongoDB Server.
 *
 * A `libmongodbcapi_client` connection object is necessary to perform most database operations,
 * such as queries.  Some operations which the library provides need access to this object.  Further
 * a construction function and a destruction function for these objects are also provided.  Multiple
 * object instances of this type may exist at any given time.
 *
 * @invariant The use of `libmongodbcapi_capi` objects from multiple threads is not threadsafe.
 */
typedef struct libmongodbcapi_client libmongodbcapi_client;

/**
 * Creates a new client and returns it.
 *
 * A client must be created in order to perform database operations.
 *
 * @pre The specified `db` object must not be `NULL`
 * @pre The specified `db` object must be a valid `libmongodbcapi_db` object.
 * @pre The specified `status` object must be either a valid `libmongodbcapi_status` object or
 * `NULL`.
 *
 * @post Either a new Embedded MongoDB Client will be created, or an error will be reported.
 *
 * @param db The database that will own this client and execute its RPC calls
 *
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @return A pointer to a newly constructed valid `libmongodbcapi_client`.
 * @return `NULL` on error, and modifies `status` on failure.
 *
 * @invariant This function is completely threadsafe, as long as its preconditions are met.
 */
libmongodbcapi_client* libmongodbcapi_client_new(libmongodbcapi_db* db,
                                                 libmongodbcapi_status* status);

/**
 * Destroys a client and removes it from the db context.
 *
 * A client must be destroyed before the owning db is destroyed.
 *
 * @pre The specified `client` object must not be `NULL`.
 * @pre The specified `client` object must be a valid `libmongodbcapi_client` object.
 * @pre The specified `status` object must be either a valid `libmongodbcapi_status` object or
 * `NULL`.
 *
 * @param client A pointer to the client to be destroyed
 *
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @post Either the specified Embedded MongoDB Client will be destroyed, or an error will be
 * reported.
 *
 * @returns `LIBMONGODB_CAPI_SUCCESS` on success.
 * @returns An error code and modifies the specified `status` object on failure.
 *
 * @invariant This function not threadsafe unless the specified `client` object is not passed
 * concurrently to any other function.  It is safe to destroy distinct `libmongodbcapi_client`
 * objects on distinct threads.
 *
 * @note This function exhibits undefined behavior unless is its preconditions are met.
 * @note This function may return diagnosic errors for violations of its precondition, but this
 * behavior is not guaranteed.
 */
int libmongodbcapi_client_destroy(libmongodbcapi_client* client, libmongodbcapi_status* status);

/**
 * Makes an RPC call to the database.
 *
 * A MongoDB client operation is performed according to the provided BSON object specified by
 * `input` and `input_size`.
 *
 * @pre The specified `client` object must not be `NULL`.
 * @pre The specified `client` object must be a valid `libmongodbcapi_client` object.
 * @pre The specified `input` buffer must not be `NULL`.
 * @pre The specified `input` buffer must be a valid BSON request.
 * @pre The specified `output` pointer must not be `NULL`
 * @pre The specified `output` pointer must point to a valid, non-const `void *` variable.
 * @pre The specified `output_size` pointer must not be `NULL`
 * @pre The specified `output` pointer must point to a valid, non-const `size_t` variable.
 * @pre The specified `status` object must be either a valid `libmongodbcapi_status` object or
 * `NULL`.
 *
 * @param client The client that will be performing the query on the database
 *
 * @param input The query to be sent to and then executed by the database
 *
 * @param input_size The size (number of bytes) of the input query
 *
 * @param output A pointer to a `void *` where the database can write the location of the output.
 * The library will manage the memory pointed to by * `output`.
 *
 * @param output_size A pointer to a location where this function will write the size (number of
 * bytes) of the `output` buffer.
 *
 * @param status A pointer to a `libmongodbcapi_status` object which will not be modified unless
 * this function reports a failure.
 *
 * @post Either the requested database operation will have been performed, or an error will be
 * reported.
 *
 * @return Returns LIBMONGODB_CAPI_SUCCESS on success.
 * @return An error code and modifies `status` on failure
 *
 * @invariant This function is not thread-safe unless its preconditions are met, and the specified
 * `libmongodbcapi_client` object is not concurrently accessed by any other thread until after this
 * call has completed.
 *
 * @note The `output` and `output_size` parameters will not be modified unless the function
 * succeeds.
 * @note The storage associated with `output` will be valid until the next call to
 * `libmongodbcapi_client_wire_protocol_rpc` on the specified `client` object, or the `client` is
 * destroyed using `libmongodbcapi_client_destroy`.
 * @note That the storage which is referenced by `output` upon successful completion is considered
 * to be part of the specified `client` object for the purposes of thread-safety and undefined
 * behavior.
 */
int libmongodbcapi_client_invoke(libmongodbcapi_client* client,
                                 const void* input,
                                 size_t input_size,
                                 void** output,
                                 size_t* output_size,
                                 libmongodbcapi_status* status);

#ifdef __cplusplus
}  // extern "C"
#endif

#ifdef _DOXYGEN
}  // namespace LibMongoDBCAPI
#endif

#endif
