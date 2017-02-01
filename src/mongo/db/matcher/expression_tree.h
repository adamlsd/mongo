// expression_tree.h

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

#include "mongo/db/matcher/expression.h"


/**
 * this contains all Expessions that define the structure of the tree
 * they do not look at the structure of the documents themselves, just combine other things
 */
namespace mongo {

class ListOfMatchExpression : public MatchExpression {
public:
    ListOfMatchExpression(MatchType type) : MatchExpression(type) {}
    virtual ~ListOfMatchExpression();

    /**
     * @param e - I take ownership
     */
    void add(std::unique_ptr<MatchExpression> e);

    /**
     * Returns the vector of owned `MatchExpressions` for someone else to take ownership.
     */
    std::vector<std::unique_ptr<MatchExpression>> releaseChildren() override {
        return std::move(_expressions);
    }

    std::vector<std::unique_ptr<MatchExpression>> release() {
        return releaseChildren();
    }

    void resetChildren(std::vector<std::unique_ptr<MatchExpression>> expressions) override {
        _expressions = std::move(expressions);
    }

    virtual size_t numChildren() const {
        return _expressions.size();
    }

    virtual MatchExpression* getChild(size_t i) const {
        return _expressions[i].get();
    }

    std::unique_ptr<MatchExpression> releaseChild(std::size_t i) override {
        return std::move(_expressions[i]);
    }

    virtual std::vector<MatchExpression*> getChildVector() {
        std::vector<MatchExpression*> retval;
        using std::begin;
        using std::end;
        std::transform(
            begin(_expressions),
            end(_expressions),
            back_inserter(retval),
            [](const std::unique_ptr<MatchExpression>& element) { return element.get(); });
        return retval;
    }

    bool equivalent(const MatchExpression* other) const;

protected:
    void _debugList(StringBuilder& debug, int level) const;

    void _listToBSON(BSONArrayBuilder* out) const;

private:
    std::vector<std::unique_ptr<MatchExpression>> _expressions;
};

class AndMatchExpression : public ListOfMatchExpression {
public:
    AndMatchExpression() : ListOfMatchExpression(AND) {}
    virtual ~AndMatchExpression() {}

    virtual bool matches(const MatchableDocument* doc, MatchDetails* details = 0) const;
    virtual bool matchesSingleElement(const BSONElement& e) const;

    virtual std::unique_ptr<MatchExpression> shallowClone() const {
        std::unique_ptr<AndMatchExpression> self = stdx::make_unique<AndMatchExpression>();
        for (size_t i = 0; i < numChildren(); ++i) {
            self->add(getChild(i)->shallowClone());
        }
        if (getTag()) {
            self->setTag(getTag()->clone());
        }
        return std::move(self);
    }

    virtual void debugString(StringBuilder& debug, int level = 0) const;

    virtual void serialize(BSONObjBuilder* out) const;
};

class OrMatchExpression : public ListOfMatchExpression {
public:
    OrMatchExpression() : ListOfMatchExpression(OR) {}
    virtual ~OrMatchExpression() {}

    virtual bool matches(const MatchableDocument* doc, MatchDetails* details = 0) const;
    virtual bool matchesSingleElement(const BSONElement& e) const;

    virtual std::unique_ptr<MatchExpression> shallowClone() const {
        std::unique_ptr<OrMatchExpression> self = stdx::make_unique<OrMatchExpression>();
        for (size_t i = 0; i < numChildren(); ++i) {
            self->add(getChild(i)->shallowClone());
        }
        if (getTag()) {
            self->setTag(getTag()->clone());
        }
        return std::move(self);
    }

    virtual void debugString(StringBuilder& debug, int level = 0) const;

    virtual void serialize(BSONObjBuilder* out) const;
};

class NorMatchExpression : public ListOfMatchExpression {
public:
    NorMatchExpression() : ListOfMatchExpression(NOR) {}
    virtual ~NorMatchExpression() {}

    virtual bool matches(const MatchableDocument* doc, MatchDetails* details = 0) const;
    virtual bool matchesSingleElement(const BSONElement& e) const;

    virtual std::unique_ptr<MatchExpression> shallowClone() const {
        std::unique_ptr<NorMatchExpression> self = stdx::make_unique<NorMatchExpression>();
        for (size_t i = 0; i < numChildren(); ++i) {
            self->add(getChild(i)->shallowClone());
        }
        if (getTag()) {
            self->setTag(getTag()->clone());
        }
        return std::move(self);
    }

    virtual void debugString(StringBuilder& debug, int level = 0) const;

    virtual void serialize(BSONObjBuilder* out) const;
};

class NotMatchExpression : public MatchExpression {
public:
    NotMatchExpression() : MatchExpression(NOT) {}
    NotMatchExpression(MatchExpression* e) : MatchExpression(NOT), _exp(e) {}
    /**
     * @param exp - I own it, and will delete
     */
    virtual Status init(std::unique_ptr<MatchExpression> exp) {
        _exp = std::move(exp);
        return Status::OK();
    }

    virtual std::unique_ptr<MatchExpression> shallowClone() const {
        std::unique_ptr<NotMatchExpression> self = stdx::make_unique<NotMatchExpression>();
        self->init(_exp->shallowClone());
        if (getTag()) {
            self->setTag(getTag()->clone());
        }
        return std::move(self);
    }

    virtual bool matches(const MatchableDocument* doc, MatchDetails* details = 0) const {
        return !_exp->matches(doc, NULL);
    }

    virtual bool matchesSingleElement(const BSONElement& e) const {
        return !_exp->matchesSingleElement(e);
    }

    virtual void debugString(StringBuilder& debug, int level = 0) const;

    virtual void serialize(BSONObjBuilder* out) const;

    bool equivalent(const MatchExpression* other) const;

    virtual size_t numChildren() const {
        return 1;
    }

    virtual MatchExpression* getChild(size_t i) const {
        return _exp.get();
    }

    std::vector<std::unique_ptr<MatchExpression>> releaseChildren() override {
        std::vector<std::unique_ptr<MatchExpression>> rv;
        rv.push_back(std::move(_exp));
        return rv;
    }

    void resetChildren(std::vector<std::unique_ptr<MatchExpression>> newChildren) override {
        invariant(newChildren.size() <= 1);
        _exp.reset();
        if (!newChildren.empty()) {
            _exp = std::move(newChildren.front());
        }
    }

	using MatchExpression::releaseChild;
    std::unique_ptr<MatchExpression> releaseChild() {
        return std::move(_exp);
    }

    void resetChild(std::unique_ptr<MatchExpression> newChild) {
        _exp = std::move(newChild);
    }

private:
    std::unique_ptr<MatchExpression> _exp;
};
}
