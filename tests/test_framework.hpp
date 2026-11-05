// Minimal test framework. Knows nothing about the engine.
//
//   TEST_CASE("name") { ... }   define and register a test
//   CHECK(expr)                 record a failure, keep going
//   REQUIRE(expr)               record a failure, stop this test
//   CHECK_EQ(a, b)              like CHECK(a == b), prints both sides on failure
//   REQUIRE_EQ(a, b)            like REQUIRE(a == b), prints both sides on failure
//   FAIL("message")             unconditional failure, stops this test
//
// Values are printed on failure only if they support `os << value`;
// anything else (e.g. a plain enum class) prints as <unprintable>.
// Add an operator<< for your own types if you want readable failures.

#pragma once

#include <algorithm>
#include <exception>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace testfw {

using TestFn = void (*)();

struct TestInfo {
    std::string name;
    const char* file;
    int line;
    TestFn fn;
};

inline std::vector<TestInfo>& registry() {
    static std::vector<TestInfo> tests;
    return tests;
}

struct Registrar {
    Registrar(const char* name, const char* file, int line, TestFn fn) {
        registry().push_back({name, file, line, fn});
    }
};

// Thrown by REQUIRE / FAIL to abort the current test only.
struct AbortTest {};

inline int& current_failures() {
    static int n = 0;
    return n;
}

inline void report_failure(const char* file, int line, const std::string& what) {
    ++current_failures();
    std::cerr << "    " << file << ":" << line << ": FAILED: " << what << "\n";
}

template <typename T>
std::string to_printable(const T& value) {
    if constexpr (requires(std::ostream& os, const T& v) { os << v; }) {
        std::ostringstream os;
        os << value;
        return os.str();
    } else {
        return "<unprintable>";
    }
}

template <typename A, typename B>
bool check_eq(const A& a, const B& b, const char* a_expr, const char* b_expr,
              const char* file, int line) {
    if (a == b) return true;
    report_failure(file, line,
                   std::string(a_expr) + " == " + b_expr + "\n      left:  " +
                       to_printable(a) + "\n      right: " + to_printable(b));
    return false;
}

// Runs every registered test whose name contains `filter` (empty = all).
// Flags: --list prints test names and exits.
inline int run_all(int argc, char** argv) {
    std::string_view filter;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--list") {
            for (const auto& t : registry()) std::cout << t.name << "\n";
            return 0;
        }
        filter = arg;
    }

    // Registration order across files is unspecified; sort for a stable run order.
    auto tests = registry();
    std::sort(tests.begin(), tests.end(),
              [](const TestInfo& a, const TestInfo& b) { return a.name < b.name; });

    int ran = 0, failed = 0;
    for (const auto& t : tests) {
        if (!filter.empty() && t.name.find(filter) == std::string::npos) continue;
        ++ran;
        current_failures() = 0;
        try {
            t.fn();
        } catch (const AbortTest&) {
            // failure already recorded
        } catch (const std::exception& e) {
            report_failure(t.file, t.line, std::string("unexpected exception: ") + e.what());
        } catch (...) {
            report_failure(t.file, t.line, "unexpected non-std exception");
        }
        if (current_failures() > 0) {
            ++failed;
            std::cerr << "[FAIL] " << t.name << "\n";
        } else {
            std::cout << "[ ok ] " << t.name << "\n";
        }
    }

    std::cout << "\n" << (ran - failed) << "/" << ran << " tests passed";
    if (!filter.empty()) std::cout << " (filter: \"" << filter << "\")";
    std::cout << "\n";
    if (ran == 0) {
        std::cerr << "no tests matched\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}

}  // namespace testfw

#define TESTFW_CONCAT_INNER(a, b) a##b
#define TESTFW_CONCAT(a, b) TESTFW_CONCAT_INNER(a, b)
#define TESTFW_UNIQUE(prefix) TESTFW_CONCAT(prefix, __LINE__)

#define TESTFW_TEST_CASE_IMPL(fn, name)                                              \
    static void fn();                                                                \
    static const ::testfw::Registrar TESTFW_CONCAT(fn, _reg){name, __FILE__, __LINE__, \
                                                             &fn};                   \
    static void fn()

#define TEST_CASE(name) TESTFW_TEST_CASE_IMPL(TESTFW_UNIQUE(testfw_case_), name)

#define CHECK(expr)                                                          \
    do {                                                                     \
        if (!(expr)) ::testfw::report_failure(__FILE__, __LINE__, #expr);    \
    } while (0)

#define REQUIRE(expr)                                                        \
    do {                                                                     \
        if (!(expr)) {                                                       \
            ::testfw::report_failure(__FILE__, __LINE__, #expr);             \
            throw ::testfw::AbortTest{};                                     \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b) \
    do { ::testfw::check_eq((a), (b), #a, #b, __FILE__, __LINE__); } while (0)

#define REQUIRE_EQ(a, b)                                                       \
    do {                                                                       \
        if (!::testfw::check_eq((a), (b), #a, #b, __FILE__, __LINE__))         \
            throw ::testfw::AbortTest{};                                       \
    } while (0)

#define FAIL(msg)                                                            \
    do {                                                                     \
        ::testfw::report_failure(__FILE__, __LINE__, msg);                   \
        throw ::testfw::AbortTest{};                                         \
    } while (0)
