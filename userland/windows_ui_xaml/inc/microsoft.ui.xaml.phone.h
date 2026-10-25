// NovaOS: the phone controls' Windows Runtime ABI header (Microsoft's MIDL
// writes it from dxaml/phone's IDL, not vendored).  FlyoutBase_partial.cpp
// includes it only to ask whether a flyout is a DatePickerFlyout or a
// TimePickerFlyout, controls not built yet: their interfaces, with the IDs
// abigen gives an interface its IDL names none for (tools/winrt/abigen.py
// name_uuid), which no object answers.
#pragma once
#include <inspectable.h>

namespace ABI { namespace Microsoft { namespace UI { namespace Xaml { namespace Controls {
struct __declspec(uuid("75864915-1b7a-5053-849e-861265b882d2")) __declspec(novtable) IDatePickerFlyout : public IInspectable
{
};
struct __declspec(uuid("c5356212-0575-52b7-b8cb-c6b54c565122")) __declspec(novtable) ITimePickerFlyout : public IInspectable
{
};
} } } } }
