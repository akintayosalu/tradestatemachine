# Tests

## Running

| Command | What it does |
|---|---|
| `make test` | build and run every test |
| `make test FILTER=replace` | run only tests whose name contains `replace` |
| `make test-list` | print all test names |
| `make test-asan` | run the tests under AddressSanitizer + UndefinedBehaviorSanitizer |

The exit code is non-zero if any test fails, so `make test` works in CI and scripts.

## How the build fits together

- `src/main.cpp` is the app's entry point and is **not** linked into the tests.
- Every other `.cpp` under `src/` is linked into **both** the app and the test binary.
  So engine code you put in `src/` is automatically testable. Keep `main()` thin.
- Every `.cpp` under `tests/` is linked into the test binary. New files are picked up automatically.
- `tests/test_main.cpp` provides the test binary's `main()`. Don't write another one.

## Writing a test

Include `test_framework.hpp` plus whatever engine headers you need, then:

| Macro | Meaning |
|---|---|
| `TEST_CASE("name") { ... }` | define a test; names must be unique-ish so `FILTER` is useful |
| `CHECK(expr)` | record a failure if false, keep going |
| `REQUIRE(expr)` | record a failure if false, stop this test |
| `CHECK_EQ(a, b)` / `REQUIRE_EQ(a, b)` | same, but print both values on failure |
| `FAIL("msg")` | fail and stop this test |

Values only print on failure if they support `std::ostream << value`. Plain `enum class`
values print as `<unprintable>` until you give them an `operator<<`.

Tests run in name order, so a naming scheme gives a readable report. For example,
prefix by area: `state: New x Cancel -> Cancelled`, `invariant: ...`, `book: ...`.

`framework_selftest.cpp` only checks the framework itself. Use it as a template,
or delete it once you have real tests.
