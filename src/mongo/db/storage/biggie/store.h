
/**
 *    Copyright (C) 2018-present MongoDB, Inc.
 *
 *    This program is free software: you can redistribute it and/or modify
 *    it under the terms of the Server Side Public License, version 1,
 *    as published by MongoDB, Inc.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    Server Side Public License for more details.
 *
 *    You should have received a copy of the Server Side Public License
 *    along with this program. If not, see
 *    <http://www.mongodb.com/licensing/server-side-public-license>.
 *
 *    As a special exception, the copyright holders give permission to link the
 *    code of portions of this program with the OpenSSL library under certain
 *    conditions as described in each individual source file and distribute
 *    linked combinations including the program with the OpenSSL library. You
 *    must comply with the Server Side Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <array>
#include <boost/optional.hpp>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <string.h>
#include <vector>

#include "mongo/util/assert_util.h"

namespace mongo {
namespace biggie {

class merge_conflict_exception : std::exception {
    virtual const char* what() const noexcept {
        return "conflicting changes prevent successful merge";
    }
};

/**
 * RadixStore is a Trie data structure with the ability to share nodes among copies of trees to
 * minimize data duplication. Each node has a notion of ownership and if modifications are made to
 * non-uniquely owned nodes, they are copied to prevent dirtying the data for the other owners of
 * the node.
 */
template <class Key, class T>
class RadixStore {
    class Node;

public:
    using mapped_type = T;
    using value_type = std::pair<const Key, mapped_type>;
    using allocator_type = std::allocator<value_type>;
    using pointer = typename std::allocator_traits<allocator_type>::pointer;
    using const_pointer = typename std::allocator_traits<allocator_type>::const_pointer;
    using size_type = std::size_t;
    using uint8_t = std::uint8_t;

    template <class pointer_type, class reference_type>
    class radix_iterator {
        friend class RadixStore;

    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = typename RadixStore::value_type;
        using difference_type = std::ptrdiff_t;
        using pointer = pointer_type;
        using reference = reference_type;

        radix_iterator() : _workingRootReference(nullptr), _root(nullptr), _current(nullptr) {}

        ~radix_iterator() = default;

        radix_iterator& operator++() {
            _restoreIfChanged();
            _findNext();
            return *this;
        }

        radix_iterator operator++(int) {
            _restoreIfChanged();
            radix_iterator old = *this;
            ++*this;
            return old;
        }

        bool operator==(const radix_iterator& other) {
            _restoreIfChanged();
            return this->_current == other._current;
        }

        bool operator!=(const radix_iterator& other) {
            _restoreIfChanged();
            return this->_current != other._current;
        }

        reference operator*() {
            _restoreIfChanged();
            return *(_current->data);
        }

        const_pointer operator->() {
            _restoreIfChanged();
            return &*(_current->data);
        }

    private:
        radix_iterator(const std::shared_ptr<Node>& root)
            : _workingRootReference(&root), _root(root), _current(nullptr) {}

        radix_iterator(const std::shared_ptr<Node>& root, Node* current)
            : _workingRootReference(&root), _root(root), _current(current) {}

        /**
         * Tries to restore the iterator if the working tree experienced a change, if it isn't
         * possible to restore the iterator, it invalidates it instead.
         */
        void _restoreIfChanged() {
            if (_workingRootReference->get() != _root.get()) {
                boost::optional<value_type> currentKey = _current->data;

                // Update the iterator to point to the updated working tree
                _root = *_workingRootReference;

                // Try to find the _current node in the new root
                if (!currentKey) {
                    if (_root.get() == _current) {
                        _current = _workingRootReference->get();
                    } else {
                        _current = nullptr;
                    }
                    return;
                }

                RadixStore store;
                store._root = *_workingRootReference;

                // lower_bound() tries to find the node belonging to 'key', if it can't it finds the
                // next closest item.
                _current = store.lower_bound(currentKey->first)._current;
            }
        }

        /**
        * This function traverses the tree to find the next left-most node with data. Modifies
        * '_current' to point to this node. It uses a pre-order traversal ('visit' the current
        * node itself then 'visit' the child subtrees from left to right).
        */
        void _findNext() {
            // If 'current' is a nullptr there is no next node to go to.
            if (_current == nullptr)
                return;

            // If 'current' is not a leaf, continue moving down and left in the tree until the next
            // node.
            if (!_current->isLeaf()) {
                _traverseLeftSubtree();
                return;
            }

            // Get path from root to '_current' since it is required to traverse up the tree.
            Key key = _current->data->first;

            std::vector<Node*> context = RadixStore::_buildContext(key, _root.get());

            // 'node' should equal '_current' because that should be the last element in the stack.
            // Pop back once more to get access to its parent node. The parent node will enable
            // traversal through the neighboring nodes, and if there are none, the iterator will
            // move up the tree to continue searching for the next node with data.
            Node* node = context.back();
            context.pop_back();

            // In case there is no next node, set _current to be 'nullptr' which will mark the end
            // of the traversal.
            _current = nullptr;
            while (!context.empty()) {
                uint8_t oldKey = node->trieKey.front();
                node = context.back();
                context.pop_back();

                // Check the children right of the node that the iterator was at already. This way,
                // there will be no backtracking in the traversal.
                for (auto iter = oldKey + 1 + node->children.begin(); iter != node->children.end();
                     ++iter) {

                    // If the node has a child, then the sub-tree must have a node with data that
                    // has not yet been visited.
                    if (*iter != nullptr) {

                        // If the current node has data, return it and exit. If not, continue
                        // following the nodes to find the next one with data. It is necessary to go
                        // to the left-most node in this sub-tree.
                        if ((*iter)->data) {
                            _current = iter->get();
                            return;
                        }
                        _current = iter->get();
                        _traverseLeftSubtree();
                        return;
                    }
                }
            }
            return;
        }

        void _traverseLeftSubtree() {
            // This function finds the next left-most node with data under the sub-tree where
            // '_current' is root. However, it cannot return the root, and hence at least 1
            // iteration of the while loop is required.
            do {
                for (auto child : _current->children) {
                    if (child != nullptr) {
                        _current = child.get();
                        break;
                    }
                }
            } while (!_current->data);
        }

        // "_workingRootReference" is a pointer to the original root node in the working copy when
        // the iterator was created. It is used to check for any modifications to the tree.
        const std::shared_ptr<Node>* _workingRootReference;

        // "_root" is a pointer to the root of the tree over which this is iterating. This has to be
        // a shared pointer otherwise if _workingRootReference was operating on an already copied
        // tree, further changes to its unique nodes won't be reflected towards the iterator as they
        // are made.
        std::shared_ptr<Node> _root;

        // "_current" is the node that the iterator is currently on. _current->data will never be
        // boost::none (unless it is within the process of tree traversal), and _current will be
        // become a nullptr once there are no more nodes left to iterate.
        Node* _current;
    };

    using iterator = radix_iterator<pointer, value_type&>;
    using const_iterator = radix_iterator<const_pointer, const value_type&>;

    template <class pointer_type, class reference_type>
    class reverse_radix_iterator {
        friend class RadixStore;
        friend class radix_iterator<pointer_type, reference_type&>;

    public:
        using value_type = typename RadixStore::value_type;
        using difference_type = std::ptrdiff_t;
        using pointer = pointer_type;
        using reference = reference_type;

        reverse_radix_iterator()
            : _workingRootReference(nullptr), _root(nullptr), _current(nullptr) {}

        reverse_radix_iterator(const const_iterator& it)
            : _workingRootReference(it._workingRootReference),
              _root(it._root),
              _current(it._current) {
            // If the iterator passed in is at the end(), then set _current to root which is
            // equivalent to rbegin(). Otherwise, move the iterator back one node, due to the fact
            // that the relationship &*r == &*(i-1) must be maintained for any reverse iterator 'r'
            // and forward iterator 'i'.
            if (_current == nullptr) {
                // If the tree is empty, then leave '_current' as nullptr.
                if (_root->isLeaf())
                    return;

                _current = _root.get();
                _traverseRightSubtree();
            } else {
                _findNextReverse();
            }
        }

        reverse_radix_iterator(const iterator& it)
            : _workingRootReference(it._workingRootReference),
              _root(it._root),
              _current(it._current) {
            if (_current == nullptr) {
                _current = _root;
                _traverseRightSubtree();
            } else {
                _findNextReverse();
            }
        }

        ~reverse_radix_iterator() = default;

        reverse_radix_iterator& operator++() {
            _restoreIfChanged();
            _findNextReverse();
            return *this;
        }

        reverse_radix_iterator operator++(int) {
            _restoreIfChanged();
            reverse_radix_iterator old = *this;
            ++*this;
            return old;
        }

        bool operator==(const reverse_radix_iterator& other) {
            _restoreIfChanged();
            return this->_current == other._current;
        }

        bool operator!=(const reverse_radix_iterator& other) {
            _restoreIfChanged();
            return this->_current != other._current;
        }

        reference operator*() {
            _restoreIfChanged();
            return *(_current->data);
        }

        const_pointer operator->() {
            _restoreIfChanged();
            return &*(_current->data);
        }


    private:
        reverse_radix_iterator(const std::shared_ptr<Node>& root)
            : _workingRootReference(&root), _root(root), _current(nullptr) {}

        reverse_radix_iterator(const std::shared_ptr<Node>& root, Node* current)
            : _workingRootReference(&root), _root(root), _current(current) {}

        /**
         * Tries to restore the iterator if the working tree experienced a change, if it isn't
         * possible to restore the iterator, it invalidates it instead.
         */
        void _restoreIfChanged() {
            if (_workingRootReference->get() != _root.get()) {
                auto currentKey = _current->data;

                // Update the iterator to point to the updated working tree.
                _root = *_workingRootReference;

                // Try to find the _current node in the new root.
                if (!currentKey) {
                    if (_root.get() == _current) {
                        _current = _workingRootReference->get();
                    } else {
                        _current = nullptr;
                    }
                    return;
                }

                RadixStore store;
                store._root = *_workingRootReference;

                // lower_bound() tries to find the node belonging to 'key', if it can't it finds the
                // next closest item.
                const_iterator it = store.lower_bound(currentKey->first);

                // Couldn't find any nodes with key greater than currentKey in lower_bound().
                // So make _current point to the beginning, since rbegin() will point to the
                // previous node before currentKey.
                if (!it._current)
                    _current = store.rbegin()._current;
                else {
                    _current = it._current;
                    // lower_bound(), moved us one up in a forwards direction since the currentKey
                    // didn't exist anymore, move one back.
                    if (_current->data->first > currentKey->first)
                        _findNextReverse();
                }
            }
        }

        void _findNextReverse() {
            // Reverse find iterates through the tree to find the "next" node containing data,
            // searching from right to left. Normally a pre-order traversal is used, but for
            // reverse, the ordering is to visit child nodes from right to left, then 'visit'
            // current node.
            if (_current == nullptr)
                return;

            Key key = _current->data->first;

            std::vector<Node*> context = RadixStore::_buildContext(key, _root.get());
            Node* node = context.back();
            context.pop_back();

            // Due to the nature of the traversal, it will always be necessary to move up the tree
            // first because when the 'current' node was visited, it meant all its children had been
            // visited as well.
            uint8_t oldKey;
            _current = nullptr;
            while (!context.empty()) {
                oldKey = node->trieKey.front();
                node = context.back();
                context.pop_back();

                // After moving up in the tree, continue searching for neighboring nodes to see if
                // they have data, moving from right to left.
                for (int i = oldKey - 1; i >= 0; i--) {
                    if (node->children[i] != nullptr) {
                        // If there is a sub-tree found, it must have data, therefore it's necessary
                        // to traverse to the right most node.
                        _current = node->children[i].get();
                        _traverseRightSubtree();
                        return;
                    }
                }

                // If there were no sub-trees that contained data, and the 'current' node has data,
                // it can now finally be 'visited'.
                if (node->data) {
                    _current = node;
                    return;
                }
            }
        }

        void _traverseRightSubtree() {
            // This function traverses the given tree to the right most leaf of the subtree where
            // 'current' is the root.
            do {
                for (auto iter = _current->children.rbegin(); iter != _current->children.rend();
                     ++iter) {
                    if (*iter != nullptr) {
                        _current = iter->get();
                        break;
                    }
                }
            } while (!_current->isLeaf());
        }


        // "_workingRootReference" is a pointer to the original root node in the working copy when
        // the iterator was created. It is used to check for any modifications to the tree.
        const std::shared_ptr<Node>* _workingRootReference;

        // "_root" is a pointer to the root of the tree over which this is iterating.
        std::shared_ptr<Node> _root;

        // "_current" is a the node that the iterator is currently on. _current->data will never be
        // boost::none, and _current will be become a nullptr once there are no more nodes left to
        // iterate.
        Node* _current;
    };

    using reverse_iterator = reverse_radix_iterator<pointer, value_type&>;
    using const_reverse_iterator = reverse_radix_iterator<const_pointer, const value_type&>;

    // Constructor
    RadixStore(const RadixStore& other) {
        _root = other._root;
    }

    RadixStore() {
        _root = std::make_shared<Node>();
    }

    ~RadixStore() = default;

    // Equality
    bool operator==(const RadixStore& other) const {
        RadixStore::const_iterator iter = this->begin();
        RadixStore::const_iterator other_iter = other.begin();

        while (iter != this->end()) {
            if (other_iter == other.end() || *iter != *other_iter) {
                return false;
            }

            iter++;
            other_iter++;
        }

        return other_iter == other.end();
    }

    bool sameRoot(const RadixStore& other) const {
        return this->_root.get() == other._root.get();
    }

    // Capacity
    bool empty() const {
        return _root->_numSubtreeElems == 0;
    }

    size_type size() const {
        return _root->_numSubtreeElems;
    }

    size_type dataSize() const {
        return _root->_sizeSubtreeElems;
    }

    // Modifiers
    void clear() noexcept {
        _root = std::make_shared<Node>();
    }

    std::pair<const_iterator, bool> insert(value_type&& value) {
        Key key = value.first;
        mapped_type m = value.second;

        Node* item = _findNode(key);
        if (item != nullptr || key.size() == 0)
            return std::make_pair(end(), false);

        return _upsertWithCopyOnSharedNodes(key, std::move(value));
    }

    std::pair<const_iterator, bool> update(value_type&& value) {
        Key key = value.first;
        mapped_type m = value.second;

        // Ensure that the item to be updated exists.
        auto item = RadixStore::find(key);
        if (item == RadixStore::end())
            return std::make_pair(item, false);

        return _upsertWithCopyOnSharedNodes(key, std::move(value), item->second.size());
    }

    size_type erase(const Key& key) {
        std::vector<std::pair<Node*, bool>> context;

        Node* prev = _root.get();
        bool isUniquelyOwned = _root.use_count() == 1;
        context.push_back(std::make_pair(prev, isUniquelyOwned));

        Node* node = nullptr;

        const char* charKey = key.data();
        size_t depth = prev->depth + prev->trieKey.size();
        while (depth < key.size()) {
            uint8_t c = static_cast<uint8_t>(charKey[depth]);
            node = prev->children[c].get();
            if (node == nullptr) {
                return 0;
            }

            // If the prefixes mismatch, this key cannot exist in the tree.
            size_t p = _comparePrefix(node->trieKey, charKey + depth, key.size() - depth);
            if (p != node->trieKey.size()) {
                return 0;
            }

            isUniquelyOwned = isUniquelyOwned && prev->children[c].use_count() == 1;
            context.push_back(std::make_pair(node, isUniquelyOwned));
            depth = node->depth + node->trieKey.size();
            prev = node;
        }

        size_t sizeOfRemovedNode = node->data->second.size();
        Node* deleted = context.back().first;
        context.pop_back();

        if (!deleted->isLeaf()) {
            // The to-be deleted node is an internal node, and therefore updating its data to be
            // boost::none will "delete" it.
            _upsertWithCopyOnSharedNodes(key, boost::none, -1 * sizeOfRemovedNode);
            return 1;
        }

        Node* parent = context.at(0).first;
        isUniquelyOwned = context.at(0).second;

        if (!isUniquelyOwned) {
            _root = std::make_shared<Node>(*parent);
            parent = _root.get();
        }

        parent->_numSubtreeElems -= 1;
        parent->_sizeSubtreeElems -= sizeOfRemovedNode;

        for (size_t node = 1; node < context.size(); node++) {
            Node* child = context.at(node).first;
            isUniquelyOwned = context.at(node).second;

            uint8_t childFirstChar = child->trieKey.front();
            if (!isUniquelyOwned) {
                parent->children[childFirstChar] = std::make_shared<Node>(*child);
                child = parent->children[childFirstChar].get();
            }

            child->_numSubtreeElems -= 1;
            child->_sizeSubtreeElems -= sizeOfRemovedNode;

            parent = child;
        }

        // Handle the deleted node, as it is a leaf.
        parent->children[deleted->trieKey.front()] = nullptr;

        // 'parent' may only have one child, in which case we need to evaluate whether or not
        // this node is redundant.
        _compressOnlyChild(parent);

        return 1;
    }

    void merge3(const RadixStore& base, const RadixStore& other) {
        std::vector<Node*> context;
        std::vector<uint8_t> trieKeyIndex;

        invariant(this->_root->trieKey.size() == 0 && base._root->trieKey.size() == 0 &&
                  other._root->trieKey.size() == 0);
        _merge3Helper(
            this->_root.get(), base._root.get(), other._root.get(), context, trieKeyIndex);
    }

    // Iterators
    const_iterator begin() const noexcept {
        if (this->empty())
            return RadixStore::end();

        Node* node = _begin(_root.get());
        return RadixStore::const_iterator(_root, node);
    }

    const_reverse_iterator rbegin() const noexcept {
        if (this->empty())
            return RadixStore::rend();

        auto node = _root;
        while (!node->isLeaf()) {
            for (auto iter = node->children.rbegin(); iter != node->children.rend(); ++iter) {
                if (*iter != nullptr) {
                    node = *iter;
                    break;
                }
            }
        }
        return RadixStore::const_reverse_iterator(_root, node.get());
    }

    const_iterator end() const noexcept {
        return RadixStore::const_iterator(_root);
    }

    const_reverse_iterator rend() const noexcept {
        return RadixStore::const_reverse_iterator(_root);
    }

    const_iterator find(const Key& key) const {
        RadixStore::const_iterator it = RadixStore::end();

        Node* node = _findNode(key);
        if (node == nullptr)
            return it;
        else
            return RadixStore::const_iterator(_root, node);
    }

    const_iterator lower_bound(const Key& key) const {
        Node* node = _root.get();
        std::vector<Node*> context;
        context.push_back(node);

        const char* charKey = key.data();
        // When we search a child array, always search to the right of 'idx' so that
        // when we go back up the tree we never search anything less than something
        // we already examined.
        uint8_t idx = 0;
        size_t depth = node->depth + node->trieKey.size();

        // Traverse the path given the key to see if the node exists.
        while (depth < key.size()) {
            // idx is an unsigned int, and converting from a signed 8-bit char to an unsigned int is
            // not trivial. It is necessary to convert the signed char to a unsigned 8-bit int (aka
            // 'unsigned char' or 'uint8_t'). Then only it can be assigned to an unsigned int.
            idx = static_cast<uint8_t>(charKey[depth]);
            if (node->children[idx] == nullptr) {
                break;
            }

            node = node->children[idx].get();
            // We may eventually need to search this node's parent for larger children.
            idx += 1;
            size_t mismatchIdx = _comparePrefix(node->trieKey, charKey + depth, key.size() - depth);

            // There is a prefix mismatch, so we don't need to traverse anymore.
            if (mismatchIdx < node->trieKey.size()) {
                // Check if the current key in the tree is greater than the one we are looking
                // for since it can't be equal at this point. It can be greater in two ways:
                // It can be longer or it can have a larger character at the mismatch index.
                uint8_t mismatchChar = static_cast<uint8_t>(charKey[mismatchIdx + depth]);
                if (mismatchIdx == key.size() - depth ||
                    node->trieKey[mismatchIdx] > mismatchChar) {
                    // If the current key is greater and has a value it is the lower bound.
                    if (node->data) {
                        return const_iterator(_root, node);
                    }

                    // If the current key has no value, place it in the context
                    // so that we can search its children.
                    context.push_back(node);
                    idx = 0;
                } else {
                    // If the current key is less, we will need to go back up the
                    // tree and this node does not need to be pushed into the context.
                    unsigned char c = static_cast<unsigned char>(charKey[depth]);
                    idx = c + 1;
                }
                break;
            }

            context.push_back(node);
            depth = node->depth + node->trieKey.size();
        }

        if (depth == key.size() && node->data) {
            // If the node exists, then we can just return an iterator to that node.
            return const_iterator(_root, node);
        } else if (depth == key.size()) {
            // The search key is an exact prefix, so we need to search all of this node's
            // children.
            idx = 0;
        }

        // The node did not exist, so must find an node with the next largest key (if it exists).
        // Use the context stack to move up the tree and keep searching for the next node with data
        // if need be.
        while (!context.empty()) {
            node = context.back();
            context.pop_back();

            for (auto iter = idx + node->children.begin(); iter != node->children.end(); ++iter) {
                if (*iter != nullptr) {
                    // There exists a node with a key larger than the one given, traverse to
                    // this node which will be the left-most node in this sub-tree.
                    node = iter->get();
                    while (!node->data) {
                        for (auto iter = node->children.begin(); iter != node->children.end();
                             ++iter) {
                            if (*iter != nullptr) {
                                node = iter->get();
                                break;
                            }
                        }
                    }
                    return const_iterator(_root, node);
                }
            }

            if (node->trieKey.empty()) {
                // We have searched the root. There's nothing left to search.
                return end();
            } else {
                unsigned char c = static_cast<unsigned char>(node->trieKey.front());
                idx = c + 1;
            }
        }

        // If there was no node with a larger key than the one given, return end().
        return end();
    }

    const_iterator upper_bound(const Key& key) const {
        const_iterator it = lower_bound(key);
        if (it == end())
            return it;

        if (it->first == key)
            return ++it;

        return it;
    }

    typename RadixStore::iterator::difference_type distance(iterator iter1, iterator iter2) {
        return std::distance(iter1, iter2);
    }

    typename RadixStore::iterator::difference_type distance(const_iterator iter1,
                                                            const_iterator iter2) {
        return std::distance(iter1, iter2);
    }

    std::string to_string_for_test() {
        return _walkTree(_root.get(), 0);
    }

private:
    class Node {
        friend class RadixStore;

    public:
        Node() {
            depth = 0;
            data = boost::none;
            children.fill(nullptr);
            _numSubtreeElems = 0;
            _sizeSubtreeElems = 0;
        }

        Node(std::vector<uint8_t> key) : trieKey(key) {
            depth = 0;
            data = boost::none;
            children.fill(nullptr);
            _numSubtreeElems = 0;
            _sizeSubtreeElems = 0;
        }

        bool isLeaf() const {
            for (auto child : children) {
                if (child != nullptr)
                    return false;
            }
            return true;
        }

        std::vector<uint8_t> trieKey;
        unsigned int depth;
        boost::optional<value_type> data;
        std::array<std::shared_ptr<Node>, 256> children;

    private:
        size_type _numSubtreeElems;
        size_type _sizeSubtreeElems;
    };

    /**
     * Return a string representation of all the nodes in this tree.
     * The string will look like:
     *
     *  food
     *   s
     *  bar
     *
     *  The number of spaces in front of each node indicates the depth
     *  at which the node lies.
     */
    std::string _walkTree(Node* node, int depth) {
        std::string ret;
        for (int i = 0; i < depth; i++) {
            ret.push_back(' ');
        }

        for (uint8_t ch : node->trieKey) {
            ret.push_back(ch);
        }
        if (node->data) {
            ret.push_back('*');
        }
        ret.push_back('\n');

        for (auto child : node->children) {
            if (child != nullptr) {
                ret.append(_walkTree(child.get(), depth + 1));
            }
        }
        return ret;
    }

    Node* _findNode(const Key& key) const {
        const char* charKey = key.data();

        unsigned int depth = _root->depth;
        unsigned int initialDepthOffset = depth;

        // If the root node's triekey is not empty then the tree is a subtree, and so we examine it.
        for (unsigned int i = 0; i < _root->trieKey.size(); i++) {
            if (charKey[i + initialDepthOffset] != _root->trieKey[i]) {
                return nullptr;
            }
            depth++;

            if (depth == key.size()) {
                return _root.get();
            }
        }

        depth = _root->depth + _root->trieKey.size();
        uint8_t childFirstChar = static_cast<uint8_t>(charKey[depth]);
        auto node = _root->children[childFirstChar];

        while (node != nullptr) {

            depth = node->depth;

            size_t mismatchIdx = _comparePrefix(node->trieKey, charKey + depth, key.size() - depth);
            if (mismatchIdx != node->trieKey.size()) {
                return nullptr;
            } else if (mismatchIdx == key.size() - depth && node->data) {
                return node.get();
            }

            depth = node->depth + node->trieKey.size();

            childFirstChar = static_cast<uint8_t>(charKey[depth]);
            node = node->children[childFirstChar];
        }

        return nullptr;
    }

    /**
     * _upsertWithCopyOnSharedNodes is a helper function to help manage copy on modification for the
     * tree. This function follows the path for the to-be modified node using the keystring. If at
     * any point, the path is no longer uniquely owned, the following nodes are copied to prevent
     * modification to other owner's data.
     *
     * 'key' is the key which can be followed to find the data.
     * 'value' is the data to be inserted or updated. It can be an empty value in which case it is
     * equivalent to removing that data from the tree.
     * 'sizeDiff' is used to determine the change in number of elements and size for the tree. If it
     * is positive, then we are updating an element, and the sizeDiff represents the size of the
     * original element (and value contains the size of new element). If it is negative, that means
     * we are removing an element that has a size of sizeDiff (which is negative to indicate
     * deletion).
     */
    std::pair<const_iterator, bool> _upsertWithCopyOnSharedNodes(Key key,
                                                                 boost::optional<value_type> value,
                                                                 int sizeDiff = 0) {

        int elemNum = 1;
        int elemSize = 0;
        if (sizeDiff > 0) {
            elemNum = 0;
            elemSize = value->second.size() - sizeDiff;
        } else if (!value || sizeDiff < 0) {
            elemNum = -1;
            elemSize = sizeDiff;
        } else {
            elemSize = value->second.size();
        }

        const char* charKey = key.data();

        int depth = _root->depth + _root->trieKey.size();
        uint8_t childFirstChar = static_cast<uint8_t>(charKey[depth]);

        if (_root.use_count() > 1) {
            // Copy node on a modifying operation when the root isn't unique.
            _root = std::make_shared<Node>(*_root.get());
        }

        _root->_numSubtreeElems += elemNum;
        _root->_sizeSubtreeElems += elemSize;

        Node* prev = _root.get();
        std::shared_ptr<Node> node = prev->children[childFirstChar];
        while (node != nullptr) {
            if (node.use_count() - 1 > 1) {
                // Copy node on a modifying operation when it isn't owned uniquely.
                node = std::make_shared<Node>(*node);
                prev->children[childFirstChar] = node;
            }

            // 'node' is uniquely owned at this point, so we are free to modify it.
            // Get the index at which node->trieKey and the new key differ.
            size_t mismatchIdx = _comparePrefix(node->trieKey, charKey + depth, key.size() - depth);

            // The keys mismatch, so we need to split this node.
            if (mismatchIdx != node->trieKey.size()) {

                // Make a new node with whatever prefix is shared between node->trieKey
                // and the new key. This will replace the current node in the tree.
                std::vector<uint8_t> newKey = _makeKey(node->trieKey, 0, mismatchIdx);
                Node* newNode = _addChild(prev, newKey, boost::none);

                depth += mismatchIdx;
                const_iterator it(_root, newNode);
                if (key.size() - depth != 0) {
                    // Make a child with whatever is left of the new key.
                    newKey = _makeKey(charKey + depth, key.size() - depth);
                    Node* newChild = _addChild(newNode, newKey, value);
                    newNode->_numSubtreeElems += 1;
                    newNode->_sizeSubtreeElems += value->second.size();
                    it = const_iterator(_root, newChild);
                } else {
                    // The new key is a prefix of an existing key, and has its own node, so we don't
                    // need to add any new nodes.
                    newNode->data.emplace(value->first, value->second);
                    newNode->_numSubtreeElems += 1;
                    newNode->_sizeSubtreeElems += value->second.size();
                }

                // Change the current node's trieKey and make a child of the new node.
                newKey = _makeKey(node->trieKey, mismatchIdx, node->trieKey.size() - mismatchIdx);
                newNode->children[newKey.front()] = node;

                node->trieKey = newKey;
                node->depth = newNode->depth + newNode->trieKey.size();

                return std::pair<const_iterator, bool>(it, true);
            } else if (mismatchIdx == key.size() - depth) {
                // Update an internal node.
                if (!value) {
                    node->data = boost::none;
                    _compressOnlyChild(node.get());
                } else {
                    node->data.emplace(value->first, value->second);
                }
                node->_numSubtreeElems += elemNum;
                node->_sizeSubtreeElems += elemSize;
                const_iterator it(_root, node.get());

                return std::pair<const_iterator, bool>(it, true);
            }

            node->_numSubtreeElems += elemNum;
            node->_sizeSubtreeElems += elemSize;

            depth = node->depth + node->trieKey.size();
            childFirstChar = static_cast<uint8_t>(charKey[depth]);

            prev = node.get();
            node = node->children[childFirstChar];
        }

        // Add a completely new child to a node. The new key at this depth does not
        // share a prefix with any existing keys.
        std::vector<uint8_t> newKey = _makeKey(charKey + depth, key.size() - depth);
        Node* newNode = _addChild(prev, newKey, value);
        const_iterator it(_root, newNode);

        return std::pair<const_iterator, bool>(it, true);
    }

    /**
     * Return a uint8_t vector with the first 'count' characters of
     * 'old'.
     */
    std::vector<uint8_t> _makeKey(const char* old, size_t count) {
        std::vector<uint8_t> key;
        for (size_t i = 0; i < count; ++i) {
            uint8_t c = static_cast<uint8_t>(old[i]);
            key.push_back(c);
        }
        return key;
    }

    /**
     * Return a uint8_t vector with the [pos, pos+count) characters from old.
     */
    std::vector<uint8_t> _makeKey(std::vector<uint8_t> old, size_t pos, size_t count) {
        std::vector<uint8_t> key;
        for (size_t i = pos; i < pos + count; ++i) {
            key.push_back(old[i]);
        }
        return key;
    }

    /**
     * Add a child with trieKey 'key' and value 'value' to 'node'.
     */
    Node* _addChild(Node* node, std::vector<uint8_t> key, boost::optional<value_type> value) {

        std::shared_ptr<Node> newNode = std::make_shared<Node>(key);
        newNode->depth = node->depth + node->trieKey.size();
        if (value) {
            newNode->data.emplace(value->first, value->second);
            newNode->_numSubtreeElems = 1;
            newNode->_sizeSubtreeElems = value->second.size();
        }
        if (node->children[key.front()] != nullptr) {
            newNode->_numSubtreeElems += node->children[key.front()]->_numSubtreeElems;
            newNode->_sizeSubtreeElems += node->children[key.front()]->_sizeSubtreeElems;
        }
        node->children[key.front()] = newNode;
        return newNode.get();
    }

    /**
     * This function traverses the tree starting at the provided node using the provided the
     * key. It returns the stack which is used in tree traversals for both the forward and
     * reverse iterators. Since both iterator classes use this function, it is declared
     * statically under RadixStore.
     *
     * This assumes that the key is present in the tree.
     */
    static std::vector<Node*> _buildContext(Key key, Node* node) {
        std::vector<Node*> context;
        context.push_back(node);

        const char* charKey = key.data();
        size_t depth = node->depth + node->trieKey.size();

        while (depth < key.size()) {
            uint8_t c = static_cast<uint8_t>(charKey[depth]);
            node = node->children[c].get();
            context.push_back(node);
            depth = node->depth + node->trieKey.size();
        }
        return context;
    }

    /**
     * Return the index at which 'key1' and 'key2' differ.
     * This function will interpret the bytes in 'key2' as unsigned values.
     */
    size_t _comparePrefix(std::vector<uint8_t> key1, const char* key2, size_t len2) const {
        size_t smaller = std::min(key1.size(), len2);

        size_t i = 0;
        for (; i < smaller; ++i) {
            uint8_t c = static_cast<uint8_t>(key2[i]);
            if (key1[i] != c) {
                return i;
            }
        }
        return i;
    }

    /**
     * Compresses a child node into its parent if necessary. This is required when an erase results
     * in a node with no value and only one child.
     */
    void _compressOnlyChild(Node* node) {
        // Don't compress if this node has an actual value associated with it or is the root.
        if (node->data || node->trieKey.empty()) {
            return;
        }

        // Determine if this node has only one child.
        std::shared_ptr<Node> onlyChild = nullptr;

        for (size_t i = 0; i < node->children.size(); ++i) {
            if (node->children[i] != nullptr) {
                if (onlyChild != nullptr) {
                    return;
                }
                onlyChild = node->children[i];
            }
        }

        // Append the child's key onto the parent.
        for (char item : onlyChild->trieKey) {
            node->trieKey.push_back(item);
        }

        if (onlyChild->data) {
            node->data.emplace(onlyChild->data->first, onlyChild->data->second);
        }
        node->children = onlyChild->children;
    }

    /**
     * Rebuilds the context by replacing stale raw pointers with the new pointers. The pointers
     * can become stale when running an operation that copies the node on modification, like
     * insert or erase.
     */
    void _rebuildContext(std::vector<Node*>& context, std::vector<uint8_t>& trieKeyIndex) {
        Node* replaceNode = _root.get();
        context[0] = replaceNode;

        for (size_t node = 1; node < context.size(); node++) {
            replaceNode = replaceNode->children[trieKeyIndex[node - 1]].get();
            context[node] = replaceNode;
        }
    }

    Node* _makeBranchUnique(std::vector<Node*>& context) {

        if (context.empty())
            return nullptr;

        // The first node should always be the root node.
        if (_root.use_count() > 1)
            _root = std::make_shared<Node>(*_root);
        context[0] = _root.get();

        // If the context only contains the root, and it was copied, return the new root.
        if (context.size() == 1)
            return _root.get();

        Node* node = nullptr;
        Node* prev = _root.get();

        // Create copies of the nodes until the leaf node.
        for (size_t idx = 1; idx < context.size(); idx++) {
            node = context[idx];

            if (prev->children[node->trieKey.front()].use_count() > 1) {
                std::shared_ptr<Node> nodeCopy = std::make_shared<Node>(*node);
                prev->children[nodeCopy->trieKey.front()] = nodeCopy;
                context[idx] = nodeCopy.get();
                prev = nodeCopy.get();
            } else {
                prev = prev->children[node->trieKey.front()].get();
            }
        }

        return context.back();
    }

    /**
     * Resolves conflicts within subtrees due to the complicated structure of path-compressed radix
     * tries.
     */
    void _mergeResolveConflict(const Node* current, const Node* baseNode, const Node* otherNode) {

        // Merges all differences between this and other, using base to determine whether operations
        // are allowed or should throw a merge conflict.
        RadixStore base, other, node;
        node._root = std::make_shared<Node>(*current);
        base._root = std::make_shared<Node>(*baseNode);
        other._root = std::make_shared<Node>(*otherNode);

        // Merges insertions and updates from the master tree into the working tree, if possible.
        for (const value_type otherVal : other) {
            RadixStore::const_iterator baseIter = base.find(otherVal.first);
            RadixStore::const_iterator thisIter = node.find(otherVal.first);

            if (thisIter != node.end() && baseIter != base.end()) {
                // All three trees have a record of the node with the same key.
                if (thisIter->second == baseIter->second && baseIter->second != otherVal.second) {
                    // No changes occured in the working tree, so the value in the master tree can
                    // be merged in cleanly.
                    this->update(RadixStore::value_type(otherVal));
                } else if (thisIter->second != baseIter->second &&
                           baseIter->second != otherVal.second) {
                    // Both the working copy and master nodes changed the same value at the same
                    // key. This results in a merge conflict.
                    throw merge_conflict_exception();
                } else if (thisIter->second != baseIter->second &&
                           thisIter->second == otherVal.second) {
                    // Both the working copy and master nodes are inserting the same value at the
                    // same key. But this is a merge conflict because if that operation was an
                    // increment, it's no different than a race condition on an unguarded variable.
                    throw merge_conflict_exception();
                }
            } else if (baseIter != base.end() && baseIter->second != otherVal.second) {
                // The working tree removed this node while the master updated the node, this
                // results in a merge conflict.
                throw merge_conflict_exception();
            } else if (thisIter != node.end()) {
                // Both the working copy and master tree are either inserting the same value or
                // different values at the same node, resulting in a merge conflict.
                throw merge_conflict_exception();
            } else if (thisIter == node.end() && baseIter == base.end()) {
                // The working tree and merge base do not have any record of this node. The node can
                // be merged in cleanly from the master tree.
                this->insert(RadixStore::value_type(otherVal));
            }
        }

        // Perform deletions from the master tree in the working tree, if possible.
        for (const value_type baseVal : base) {
            RadixStore::const_iterator otherIter = other.find(baseVal.first);
            RadixStore::const_iterator thisIter = node.find(baseVal.first);

            if (otherIter == other.end()) {
                if (thisIter != node.end() && thisIter->second == baseVal.second) {
                    // Nothing changed between the working tree and merge base, so it is safe to
                    // perform the deletion that occured in the master tree.
                    this->erase(baseVal.first);
                } else if (thisIter != node.end() && thisIter->second != baseVal.second) {
                    // The working tree made a change to the node while the master tree removed the
                    // node, resulting in a merge conflict.
                    throw merge_conflict_exception();
                }
            }
        }
    }

    /**
     * Merges elements from the master tree into the working copy if they have no presence in the
     * working copy, otherwise we throw a merge conflict.
     */
    void _mergeTwoBranches(const Node* current, const Node* otherNode) {

        RadixStore other, node;
        node._root = std::make_shared<Node>(*current);
        other._root = std::make_shared<Node>(*otherNode);

        for (const value_type otherVal : other) {
            RadixStore::const_iterator thisIter = node.find(otherVal.first);

            if (thisIter != node.end())
                throw merge_conflict_exception();
            this->insert(RadixStore::value_type(otherVal));
        }
    }

    /**
     * Returns the number of changes in terms of elements and data size from both 'current' and
     * 'other' compared to base.
     * Throws merge_conflict_exception if there are merge conflicts.
     */
    std::pair<int, int> _merge3Helper(Node* current,
                                      const Node* base,
                                      const Node* other,
                                      std::vector<Node*>& context,
                                      std::vector<uint8_t>& trieKeyIndex) {
        // Remember the number of elements, and the size of the elements that changed to
        // properly update parent nodes in our recursive stack.
        int sizeDelta = 0;
        int numDelta = 0;
        context.push_back(current);

        // Root doesn't have a trie key.
        if (!current->trieKey.empty())
            trieKeyIndex.push_back(current->trieKey.at(0));

        for (size_t key = 0; key < 256; ++key) {
            // Since _makeBranchUnique may make changes to the pointer addresses in recursive calls.
            current = context.back();

            Node* node = current->children[key].get();
            Node* baseNode = base->children[key].get();
            Node* otherNode = other->children[key].get();

            if (!node && !baseNode && !otherNode)
                continue;

            bool unique = node != otherNode && node != baseNode;

            // If the current tree does not have this node, check if the other trees do.
            if (!node) {
                if (!baseNode && otherNode) {
                    // If base and node do NOT have this branch, but other does, then
                    // merge in the other's branch.
                    int localSizeDelta = otherNode->_sizeSubtreeElems;
                    int localNumDelta = otherNode->_numSubtreeElems;

                    current = _makeBranchUnique(context);

                    // Need to rebuild our context to have updated pointers due to the
                    // modifications that go on in _makeBranchUnique.
                    _rebuildContext(context, trieKeyIndex);

                    current->children[key] = other->children[key];
                    current->_sizeSubtreeElems += localSizeDelta;
                    current->_numSubtreeElems += localNumDelta;

                    sizeDelta += localSizeDelta;
                    numDelta += localNumDelta;
                } else if (baseNode && (!otherNode || (otherNode && baseNode != otherNode))) {
                    // Either the master tree and working tree remove the same branch, or the master
                    // tree updated the branch while the working tree removed the branch, resulting
                    // in a merge conflict.
                    throw merge_conflict_exception();
                }
            } else if (!unique) {
                if (baseNode && !otherNode && baseNode == node) {
                    // Other has a deleted branch that must also be removed from current tree.
                    int localSizeDelta = node->_sizeSubtreeElems;
                    int localNumDelta = node->_numSubtreeElems;

                    current = _makeBranchUnique(context);
                    _rebuildContext(context, trieKeyIndex);
                    current->children[key] = nullptr;
                    current->_sizeSubtreeElems -= localSizeDelta;
                    current->_numSubtreeElems -= localNumDelta;

                    sizeDelta -= localSizeDelta;
                    numDelta -= localNumDelta;
                } else if (baseNode && otherNode && baseNode == node) {
                    // If base and current point to the same node, then master changed.
                    int localSizeDelta = otherNode->_sizeSubtreeElems - node->_sizeSubtreeElems;
                    int localNumDelta = otherNode->_numSubtreeElems - node->_numSubtreeElems;

                    current = _makeBranchUnique(context);
                    _rebuildContext(context, trieKeyIndex);
                    current->children[key] = other->children[key];
                    current->_sizeSubtreeElems += localSizeDelta;
                    current->_numSubtreeElems += localNumDelta;

                    sizeDelta += localSizeDelta;
                    numDelta += localNumDelta;
                }
            } else if (baseNode && otherNode && baseNode != otherNode) {
                // If all three are unique and leaf nodes, then it is a merge conflict.
                if (node->isLeaf() && baseNode->isLeaf() && otherNode->isLeaf())
                    throw merge_conflict_exception();

                // If the keys are all the exact same, then we can keep recursing.
                // Otherwise, we manually resolve the differences element by element. The
                // structure of compressed radix tries makes it difficult to compare the
                // trees node by node, hence the reason for resolving these differences
                // element by element.
                if (node->trieKey == baseNode->trieKey && baseNode->trieKey == otherNode->trieKey) {
                    std::pair<int, int> diff =
                        _merge3Helper(node, baseNode, otherNode, context, trieKeyIndex);
                    numDelta += diff.first;
                    sizeDelta += diff.second;
                } else {
                    _mergeResolveConflict(node, baseNode, otherNode);
                    _rebuildContext(context, trieKeyIndex);
                }
            } else if (baseNode && !otherNode) {
                // Throw a write conflict since current has modified a branch but master has
                // removed it.
                throw merge_conflict_exception();
            } else if (!baseNode && otherNode) {
                // Both the working tree and master added branches that were nonexistent in base.
                // This requires us to resolve these differences element by element since the
                // changes may not be conflicting.
                _mergeTwoBranches(node, otherNode);
                _rebuildContext(context, trieKeyIndex);
            }
        }

        context.pop_back();
        if (!trieKeyIndex.empty())
            trieKeyIndex.pop_back();

        return std::make_pair(numDelta, sizeDelta);
    }

    Node* _begin(Node* root) const noexcept {
        Node* node = root;
        while (!node->data) {
            if (node->children.empty())
                return nullptr;

            for (auto child : node->children) {
                if (child != nullptr) {
                    node = child.get();
                    break;
                }
            }
        }
        return node;
    }

    std::shared_ptr<Node> _root = nullptr;
};

using StringStore = RadixStore<std::string, std::string>;
}  // namespace biggie
}  // namespace mongo
