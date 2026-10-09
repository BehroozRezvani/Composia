#pragma once

// Every Windows, WIL, graphics, and C++/WinRT header Composia's public API uses, for code that
// wants them all at once. The composia::composition and composia::numerics aliases come from
// Composition.hpp.
#include <composia/Native.hpp>
#include <composia/Composition.hpp>
#include <d2d1_3.h>
#include <d3d11_4.h>
#include <dwrite_3.h>
#include <dxgi1_6.h>
#include <winrt/Windows.System.h>
