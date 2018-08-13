/**
 *    Copyright 2018 MongoDB, Inc.
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

#include "mongo/util/unique_function.h"

#include "mongo/unittest/unittest.h"

namespace {
template <int channel>
struct RunDetection {
    ~RunDetection() {
        itRan = false;
    }
    RunDetection(const RunDetection&) = delete;
    RunDetection& operator=(const RunDetection&) = delete;

    RunDetection() {
        itRan = false;
    }

    static bool itRan;
};

template <int channel>
bool RunDetection<channel>::itRan = false;

TEST(UniqueFunctionTest, construct_simple_unique_function_from_lambda) {
    // Implicit construction
    {
        RunDetection<0> runDetection;
        mongo::unique_function<void()> uf = [] { RunDetection<0>::itRan = true; };

        uf();

        ASSERT_TRUE(runDetection.itRan);
    }

    // Explicit construction
    {
        RunDetection<0> runDetection;
        mongo::unique_function<void()> uf{[] { RunDetection<0>::itRan = true; }};

        uf();

        ASSERT_TRUE(runDetection.itRan);
    }
}

TEST(UniqueFunctionTest, assign_simple_unique_function_from_lambda) {
    // Implicit construction
    RunDetection<0> runDetection;
    mongo::unique_function<void()> uf;
    uf = [] { RunDetection<0>::itRan = true; };

    uf();

    ASSERT_TRUE(runDetection.itRan);
}

TEST(UniqueFunctionTest, reassign_simple_unique_function_from_lambda) {
    // Implicit construction
    RunDetection<0> runDetection0;
    RunDetection<1> runDetection1;

    mongo::unique_function<void()> uf = [] { RunDetection<0>::itRan = true; };

    uf = [] { RunDetection<1>::itRan = true; };

    uf();

    ASSERT_FALSE(runDetection0.itRan);
    ASSERT_TRUE(runDetection1.itRan);
}

TEST(UniqueFunctionTest, calling_an_unassigned_unique_function_throws_std_bad_function_call) {
    mongo::unique_function<void()> uf;

    try {
        uf();
        ASSERT_FALSE(true);
    } catch (const std::bad_function_call&) {
    }
    ASSERT_TRUE(true);
}

TEST(UniqueFunctionTest, calling_a_nullptr_assigned_unique_function_throws_std_bad_function_call) {
    RunDetection<0> runDetection;
    mongo::unique_function<void()> uf = [] { RunDetection<0>::itRan = true; };

    uf = nullptr;

    try {
        uf();
        ASSERT_FALSE(true);
    } catch (const std::bad_function_call&) {
    }
    ASSERT_TRUE(true);

    ASSERT_FALSE(runDetection.itRan);
}

TEST(UniqueFunctionTest, accepts_a_functor_that_is_move_only) {
    struct Checker {};

    mongo::unique_function<void()> uf = [checkerPtr = std::make_unique<Checker>()]{};

    mongo::unique_function<void()> uf2 = std::move(uf);

    uf = std::move(uf2);
}

TEST(UniqueFunctionTest, dtor_releases_functor_object_and_does_not_call_function) {
    RunDetection<0> runDetection0;
    RunDetection<1> runDetection1;

    struct Checker {
        ~Checker() {
            RunDetection<0>::itRan = true;
        }
    };

    {
        mongo::unique_function<void()> uf = [checkerPtr = std::make_unique<Checker>()] {
            RunDetection<1>::itRan = true;
        };

        ASSERT_FALSE(runDetection0.itRan);
        ASSERT_FALSE(runDetection1.itRan);
    }

    ASSERT_TRUE(runDetection0.itRan);
    ASSERT_FALSE(runDetection1.itRan);
}

TEST(UniqueFunctionTest, comparison_checks) {
    mongo::unique_function<void()> uf;

    // Using true/false assertions, as we're testing the actual operators and commutativity here.
    ASSERT_TRUE(uf == nullptr);
    ASSERT_TRUE(nullptr == uf);
    ASSERT_FALSE(uf != nullptr);
    ASSERT_FALSE(nullptr != uf);

    uf = [] {};

    ASSERT_FALSE(uf == nullptr);
    ASSERT_FALSE(nullptr == uf);
    ASSERT_TRUE(uf != nullptr);
    ASSERT_TRUE(nullptr != uf);

    uf = nullptr;

    ASSERT_TRUE(uf == nullptr);
    ASSERT_TRUE(nullptr == uf);
    ASSERT_FALSE(uf != nullptr);
    ASSERT_FALSE(nullptr != uf);
}

TEST(UniqueAndSharedFunctionTest, convertability_tests) {
    static_assert(
        !std::is_convertible<mongo::unique_function<void()>, std::function<void()>>::value, "");
    static_assert(std::is_convertible<std::function<void()>, mongo::unique_function<void()>>::value,
                  "");
    static_assert(
        !std::is_convertible<mongo::shared_function<void()>, mongo::unique_function<void()>>::value,
        "");
    static_assert(
        std::is_convertible<mongo::unique_function<void()>, mongo::unique_function<void()>>::value,
        "");
    static_assert(
        std::is_convertible<mongo::unique_function<void()>, mongo::shared_function<void()>>::value,
        "");
    static_assert(
        std::is_convertible<mongo::shared_function<void()>, mongo::shared_function<void()>>::value,
        "");
    static_assert(std::is_convertible<std::function<void()>, mongo::shared_function<void()>>::value,
                  "");
    static_assert(std::is_convertible<mongo::shared_function<void()>, std::function<void()>>::value,
                  "");
}

TEST(SharedFunctionTest, construct_simple_shared_function_from_lambda) {
    // Implicit construction
    {
        RunDetection<0> runDetection;
        mongo::shared_function<void()> sf = [] { RunDetection<0>::itRan = true; };

        sf();

        ASSERT_TRUE(runDetection.itRan);
    }

    // Explicit construction
    {
        RunDetection<0> runDetection;
        mongo::shared_function<void()> sf{[] { RunDetection<0>::itRan = true; }};

        sf();

        ASSERT_TRUE(runDetection.itRan);
    }
}

TEST(SharedFunctionTest, assign_simple_shared_function_from_lambda) {
    // Implicit construction
    RunDetection<0> runDetection;
    mongo::shared_function<void()> sf;
    sf = [] { RunDetection<0>::itRan = true; };

    sf();

    ASSERT_TRUE(runDetection.itRan);
}

TEST(SharedFunctionTest, reassign_simple_shared_function_from_lambda) {
    // Implicit construction
    RunDetection<0> runDetection0;
    RunDetection<1> runDetection1;

    mongo::shared_function<void()> sf = [] { RunDetection<0>::itRan = true; };

    sf = [] { RunDetection<1>::itRan = true; };

    sf();

    ASSERT_FALSE(runDetection0.itRan);
    ASSERT_TRUE(runDetection1.itRan);
}

TEST(SharedFunctionTest, calling_an_unassigned_shared_function_throws_std_bad_function_call) {
    mongo::shared_function<void()> sf;

    try {
        sf();
        ASSERT_FALSE(true);
    } catch (const std::bad_function_call&) {
    }
    ASSERT_TRUE(true);
}

TEST(SharedFunctionTest, calling_a_nullptr_assigned_shared_function_throws_std_bad_function_call) {
    RunDetection<0> runDetection;
    mongo::shared_function<void()> sf = [] { RunDetection<0>::itRan = true; };

    sf = nullptr;

    try {
        sf();
        ASSERT_FALSE(true);
    } catch (const std::bad_function_call&) {
    }
    ASSERT_TRUE(true);

    ASSERT_FALSE(runDetection.itRan);
}

TEST(SharedFunctionTest, accepts_a_functor_that_is_move_only) {
    struct Checker {};

    mongo::shared_function<void()> sf = [checkerPtr = std::make_unique<Checker>()]{};

    mongo::shared_function<void()> sf2 = std::move(sf);

    sf = std::move(sf2);
}

TEST(SharedFunctionTest, accepts_a_functor_that_is_move_only_and_shares_a_single_copy) {
    auto dataPtr = std::make_unique<int>(0);
    int& data = *dataPtr;

    mongo::shared_function<void()> sf = [checkerPtr = std::move(dataPtr)] {
        ++*checkerPtr;
    };

    ASSERT_EQ(data, 0);

    sf();

    ASSERT_EQ(data, 1);

    mongo::shared_function<void()> sf2 = sf;

    ASSERT_EQ(data, 1);

    sf();

    ASSERT_EQ(data, 2);

    sf2();

    ASSERT_EQ(data, 3);

    sf = sf2;

    ASSERT_EQ(data, 3);

    sf2();

    ASSERT_EQ(data, 4);

    sf();

    ASSERT_EQ(data, 5);
}

TEST(SharedFunctionTest, accepts_a_functor_that_is_copyable_and_shares_a_single_copy) {
    mongo::shared_function<const int&()> sf = [data = int(0)]() mutable->const int& {
        ++data;
        return data;
    };

    const int& data = sf();

    ASSERT_EQ(data, 1);

    sf();

    ASSERT_EQ(data, 2);

    mongo::shared_function<const int&()> sf2 = sf;

    ASSERT_EQ(data, 2);

    sf();

    ASSERT_EQ(data, 3);

    sf2();

    ASSERT_EQ(data, 4);

    sf = sf2;

    ASSERT_EQ(data, 4);

    sf2();

    ASSERT_EQ(data, 5);

    sf();

    ASSERT_EQ(data, 6);
}

TEST(SharedFunctionTest,
     accepts_a_functor_that_is_copyable_and_started_life_as_a_unique_and_shares_a_single_copy) {
    mongo::unique_function<const int&()> uf = [data = int(0)]() mutable->const int& {
        ++data;
        return data;
    };

    const int& data = uf();

    ASSERT_EQ(data, 1);

    mongo::shared_function<const int&()> sf = std::move(uf);

    ASSERT_EQ(data, 1);

    sf();

    ASSERT_EQ(data, 2);

    mongo::shared_function<const int&()> sf2 = sf;

    ASSERT_EQ(data, 2);

    sf();

    ASSERT_EQ(data, 3);

    sf2();

    ASSERT_EQ(data, 4);

    sf = sf2;

    ASSERT_EQ(data, 4);

    sf2();

    ASSERT_EQ(data, 5);

    sf();

    ASSERT_EQ(data, 6);

    std::function<void()> f = sf;

    ASSERT_EQ(data, 6);

    f();

    ASSERT_EQ(data, 7);
}

TEST(SharedFunctionTest, comparison_checks) {
    mongo::shared_function<void()> sf;

    // Using true/false assertions, as we're testing the actual operators and commutativity here.
    ASSERT_TRUE(sf == nullptr);
    ASSERT_TRUE(nullptr == sf);
    ASSERT_FALSE(sf != nullptr);
    ASSERT_FALSE(nullptr != sf);

    sf = [] {};

    ASSERT_FALSE(sf == nullptr);
    ASSERT_FALSE(nullptr == sf);
    ASSERT_TRUE(sf != nullptr);
    ASSERT_TRUE(nullptr != sf);

    sf = nullptr;

    ASSERT_TRUE(sf == nullptr);
    ASSERT_TRUE(nullptr == sf);
    ASSERT_FALSE(sf != nullptr);
    ASSERT_FALSE(nullptr != sf);
}

TEST(SharedFunctionTest, dtor_releases_functor_object_and_does_not_call_function) {
    RunDetection<0> runDetection0;
    RunDetection<1> runDetection1;

    struct Checker {
        ~Checker() {
            RunDetection<0>::itRan = true;
        }
    };

    {
        mongo::shared_function<void()> sf = [checkerPtr = std::make_unique<Checker>()] {
            RunDetection<1>::itRan = true;
        };

        ASSERT_FALSE(runDetection0.itRan);
        ASSERT_FALSE(runDetection1.itRan);
    }

    ASSERT_TRUE(runDetection0.itRan);
    ASSERT_FALSE(runDetection1.itRan);
}

}  // namespace
