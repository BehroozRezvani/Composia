#pragma once

#include <windows.h>
#include <unknwn.h>
#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dwrite_3.h>
#include <dxgi1_6.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <wil/com.h>
#include <wil/cppwinrt.h>
#include <wil/resource.h>
#include <wil/result.h>

namespace composia {
namespace composition = winrt::Windows::UI::Composition;
namespace numerics = winrt::Windows::Foundation::Numerics;
}
