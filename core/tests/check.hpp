#pragma once

#include <exception>
#include <iostream>

namespace kettlelogic::test {

inline int& failureCount() {
    static int count = 0;
    return count;
}

// Runs fn and reports whether it threw any std::exception.
template <typename Fn>
bool throws(Fn fn) {
    try {
        fn();
    } catch (const std::exception&) {
        return true;
    }
    return false;
}

}  // namespace kettlelogic::test

#define CHECK(cond)                                                                         \
    do {                                                                                    \
        if (!(cond)) {                                                                      \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n";      \
            ++kettlelogic::test::failureCount();                                            \
        }                                                                                   \
    } while (0)
