// expression_array.h

/**
 *    Copyright (C) 2013 10gen Inc.
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
 *    must comply with the GNU Affero General Public License in all respects for
 *    all of the code used other than as permitted herein. If you modify file(s)
 *    with this exception, you may extend this exception to your version of the
 *    file(s), but you are not obligated to do so. If you do not wish to do so,
 *    delete this exception statement from your version. If you delete this
 *    exception statement from all source files in the program, then also delete
 *    it in the license file.
 */

#pragma once

#include <vector>

#include "mongo/base/status.h"
#include "mongo/bson/bsonmisc.h"
#include "mongo/bson/bsonobj.h"
#include "mongo/db/matcher/expression.h"
#include "mongo/db/matcher/expression_leaf.h"

namespace mongo {

class ArrayMatchingMatchExpression : public MatchExpression {
public:
    ArrayMatchingMatchExpression(MatchType matchType) : MatchExpression(matchType) {}
    virtual ~ArrayMatchingMatchExpression() {}

    Status setPath(StringData path);

    virtual bool matches(const MatchableDocument* doc, MatchDetails* details) const;

    /**
     * @param e - has to be an array.  calls matchesArray with e as an array
     */
    virtual bool matchesSingleElement(const BSONElement& e) const;

    virtual bool matchesArray(const BSONObj& anArray, MatchDetails* details) const = 0;

    bool equivalent(const MatchExpression* other) const;

    const StringData path() const {
        return _path;
    }

private:
    StringData _path;
    ElementPath _elementPath;
};

class ElemMatchObjectMatchExpression : public ArrayMatchingMatchExpression {
public:
    ElemMatchObjectMatchExpression() : ArrayMatchingMatchExpression(ELEM_MATCH_OBJECT) {}
    Status init(StringData path, std::unique_ptr<MatchExpression> sub);

    bool matchesArray(const BSONObj& anArray, MatchDetails* details) const;

    virtual std::unique_ptr<MatchExpression> shallowClone() const {
        std::unique_ptr<ElemMatchObjectMatchExpression> e =
            stdx::make_unique<ElemMatchObjectMatchExpression>();
        e->init(path(), _sub->shallowClone());
        if (getTag()) {
            e->setTag(getTag()->clone());
        }
        return std::move(e);
    }

    virtual void debugString(StringBuilder& debug, int level) const;

    virtual void serialize(BSONObjBuilder* out) const;

    virtual size_t numChildren() const {
        return 1;
    }

    virtual MatchExpression* getChild(size_t i) const {
        return _sub.get();
    }

    std::vector<std::unique_ptr<MatchExpression>> releaseChildren() override {
        std::vector<std::unique_ptr<MatchExpression>> rv;
        rv.push_back(std::move(_sub));
        return rv;
    }

    void resetChildren(std::vector<std::unique_ptr<MatchExpression>> newChildren) override {
        invariant(newChildren.size() <= 1);
        _sub.reset();
        if (!newChildren.empty()) {
            _sub = std::move(newChildren.front());
        }
    }

private:
    std::unique_ptr<MatchExpression> _sub;
};

class ElemMatchValueMatchExpression : public ArrayMatchingMatchExpression {
public:
    ElemMatchValueMatchExpression() : ArrayMatchingMatchExpression(ELEM_MATCH_VALUE) {}
    virtual ~ElemMatchValueMatchExpression() override;

    Status init(StringData path);
    Status init(StringData path, std::unique_ptr<MatchExpression> sub);
    void add(std::unique_ptr<MatchExpression> sub);

    /**
     * Returns the vector of owned `MatchExpressions` for someone else to take ownership.
     */
    std::vector<std::unique_ptr<MatchExpression>> release() {
        return releaseChildren();
    }

    bool matchesArray(const BSONObj& anArray, MatchDetails* details) const;

    virtual std::unique_ptr<MatchExpression> shallowClone() const {
        std::unique_ptr<ElemMatchValueMatchExpression> e =
            stdx::make_unique<ElemMatchValueMatchExpression>();
        e->init(path());
        for (size_t i = 0; i < _subs.size(); ++i) {
            e->add(_subs[i]->shallowClone());
        }
        if (getTag()) {
            e->setTag(getTag()->clone());
        }
        return std::move(e);
    }

    void debugString(StringBuilder& debug, int level) const override;

    void serialize(BSONObjBuilder* out) const override;

    std::vector<MatchExpression*> getChildVector() override {
        std::vector<MatchExpression*> retval;
        using std::begin;
        using std::end;
        std::transform(
            begin(_subs),
            end(_subs),
            back_inserter(retval),
            [](const std::unique_ptr<MatchExpression>& element) { return element.get(); });
        return retval;
    }

    size_t numChildren() const override {
        return _subs.size();
    }

    MatchExpression* getChild(size_t i) const override {
        return _subs[i].get();
    }

    void resetChildren(std::vector<std::unique_ptr<MatchExpression>> newChildren) override {
        _subs = std::move(newChildren);
    }

    std::vector<std::unique_ptr<MatchExpression>> releaseChildren() override {
        return std::move(_subs);
    }

private:
    bool _arrayElementMatchesAll(const BSONElement& e) const;

    std::vector<std::unique_ptr<MatchExpression>> _subs;
};

class SizeMatchExpression : public ArrayMatchingMatchExpression {
public:
    SizeMatchExpression() : ArrayMatchingMatchExpression(SIZE) {}
    Status init(StringData path, int size);

    std::unique_ptr<MatchExpression> shallowClone() const override {
        std::unique_ptr<SizeMatchExpression> e = stdx::make_unique<SizeMatchExpression>();
        e->init(path(), _size);
        if (getTag()) {
            e->setTag(getTag()->clone());
        }
        return std::move(e);
    }

    bool matchesArray(const BSONObj& anArray, MatchDetails* details) const override;

    void debugString(StringBuilder& debug, int level) const override;

    void serialize(BSONObjBuilder* out) const override;

    bool equivalent(const MatchExpression* other) const override;

    int getData() const {
        return _size;
    }

    void resetChildren(std::vector<std::unique_ptr<MatchExpression>> children) override {
        invariant(children.empty());
    }

    std::vector<std::unique_ptr<MatchExpression>> releaseChildren() override {
        return {};
    }

private:
    int _size;  // >= 0 real, < 0, nothing will match
};
}
