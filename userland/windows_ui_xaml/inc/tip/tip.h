/* NovaOS: Microsoft's "test in production" library (tip/tip.h, internal to
 * Microsoft, not shipped), whose tests report how far a real run got
 * through telemetry (XAML's DXamlInitializeCoreTest, dxaml/lib
 * DXamlCoreTipTests.h, records each step of DXamlCore's start).  Compiled
 * out, as NovaOS compiles out TraceLogging: a test is a struct its run
 * fills in and nothing reads. */
#pragma once
#include <utility>

#define TIP_declare_test(name, id) struct name
#define TIP_set_process_scope()
#define TIP_require_any_flag_set(...)
#define TIP_require_all_flags_set(...)
#define TIP_require_flag_clear(...)
#define TIP_fail_if_complete_not_called()
#define TIP_succeed()
#define TIP_value(v) (v)
#define TIP_reason(r) (r)

namespace tip {

/* a running test: its fields through ->, the steps it passed, its end */
template <class Test>
class test_scope
{
public:
    Test *operator->() { return &test_; }
    template <class Reason> void set_flag(Reason) {}
    void complete() {}

private:
    Test test_{};
};

template <class Test>
test_scope<Test> start_and_watch_errors()
{
    return test_scope<Test>();
}

} // namespace tip
