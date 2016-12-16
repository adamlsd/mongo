/*    Copyright 2016 10gen Inc.
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

#pragma once

#include <vector>

#include "mongo/stdx/list.h"
#include "mongo/stdx/mutex.h"

namespace mongo {
/**
 * The AtomicRegistrar provides a threadsafe touchpoint for tracking registrations and
 * deregistrations of elements from a tracking set. Its usecases can include connection tracking,
 * user-session tracking, limited resource tracking, and child-thread tracking.
 *
 * The `AtomicRegistrar` stores multiple objects in a threadsafe manner and returns a lightweight
 * "ticket" to allow for tracking and management. These tickets are somewhat like pointers or
 * iterators; they do not expire before the Registrar expires, nor do they get invalidated by any
 * mutating operations on the Registrar.
 *
 * In some sense the `AtomicRegistrar` can be thought of like a coat-check with claim-tickets. A
 * coat is given at the checkin desk, and a claim ticket is presented, representing the coat in the
 * coat check. At the end of an event, when a coat check is closed, all coats are divested. During
 * the event, coat owners may present claim-tickets to retrieve their coats when they wish to
 * depart. This registrar interface tracks elements in much the same way, and it is threadsafe.
 *
 * Sample Usecase
 * --------------
 * ~~~
 * class Networking {
 * private:
 *     using ConnectionRegistrar = AtomicRegistrar<std::shared_ptr<std::iostream>>;
 *
 * public:
 *     struct Socket {
 *         ConnectionRegistrar::Ticket registration;
 *         std::iostream* socket;
 *     }
 *
 *     // Acquire a connection -- the Networking class owns the connections.
 *     Socket
 *     startSession(const ServerAddress& target) {
 *         std::unique_ptr<std::iostream> socket = openConnection(target);
 *         auto socket_ptr = socket.get() auto ticket = connections.enroll(std::move(socket));
 *         return Socket{ticket, socket_ptr};
 *     }
 *
 *     // Manually close a connection.
 *     void closeSession(const Socket& socket) {
 *         _connections.retire(socket.ticket);
 *     }
 *
 *     // Print statistics about presently opened connections.
 *     void printStatistics() const {
 *         for (const auto& connection : _connections.snapshot()) {
 *             std::cerr << statistics(_connection) << std::endl;
 *         }
 *     }
 *
 * private:
 *     ConnectionRegistrar _connections;
 * };
 *
 * // ...
 *
 * int main() {
 *     Networking network;
 *
 *     while (true) {
 *         Socket s = network.startSession({"someserver.com", 4242});
 *
 *         // Start session thread
 *         std::thread t{[s] {
 *             while (true) {
 *                 // ...
 *
 *                 // In case of error, close our session and terminate.
 *                 if (s.error()) {
 *                     network.closeSession(s);
 *                     break;
 *                 }
 *             }
 *         }};
 *
 *         registerBackgroundThread(std::move(t));
 *
 *         network.printStatistics();
 *     }
 *
 *     closeBackgroundThreads();
 *
 *     return EXIT_SUCCESS;
 * }
 *
 * ~~~
 */
template <typename T>
class AtomicRegistrar {
private:
    using StorageType = stdx::list<T>;

    // TODO: Modernize to C++11 lifecycle declarations.
	MONGO_DISALLOW_COPY_AND_ASSIGN(AtomicRegistrar);

public:
    using RegistrationList = std::vector<T>;

    struct Ticket {
    public:
        Ticket() = default;

    private:
        friend AtomicRegistrar;

        using Data = typename StorageType::const_iterator;

        explicit Ticket(Data d) : _it(std::move(d)) {}

        Data _it;
    };

    AtomicRegistrar() = default;

    /**
     * Returns the number of items presently managed by this registrar.
     * NOTE: The results may be invalid as soon as this function returns.  It should be used for
     * debugging and tuning purposes only.
     */
    std::size_t size() const {
        const stdx::lock_guard<stdx::mutex> lock(this->_access);
        return this->_list.size();
    }

    /**
     * Registers the specified object for tracking by this registrar and returns a ticket
     * representing that registration.
     * `item`: Object to register
     * RETURNS: A ticket for later use in manual retirement.
     */
    Ticket enroll(T item) {
        const stdx::lock_guard<stdx::mutex> lock(this->_access);
        this->_list.push_front(std::move(item));
        using std::begin;
        return Ticket{begin(this->_list)};
    }

    /**
     * Retires the specified object from tracking by this registrar as specified by its management
     * ticket.
     * `ticket`: The ticket for the item to be deregistered.
     */
    void retire(Ticket ticket) {
        const stdx::lock_guard<stdx::mutex> lock(this->_access);
        this->_list.erase(this->_ticket.it);
    }

    /**
     * Retrieves a container of copies of `T` representing a snapshot of all elements under
     * management by this AtomicRegistrar at the time of call.
     */
    RegistrationList snapshot() const {
        const stdx::lock_guard<stdx::mutex> lock(this->_access);
        return RegistrationList{begin(this->_list), end(this->_list)};
    }

private:
    mutable stdx::mutex _access;
    StorageType _list;
};
}  // namespace mongo
