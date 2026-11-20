# GeForce 621.24 scans the Windows device tree ten times during first D3D11 device creation

On an RTX 3090, a fresh process spent a median 211.4 ms creating its first
hardware device and 18.4 ms creating a second device with the first still alive.
The affected application is Composia Editor. Device creation sits on its path
to the first hardware-rendered frame. A standalone probe reproduced the delay
before creating a window or swap chain. Testing used a Ryzen 9 5900X,
Windows 11 26H2 build 26300.9974, NVIDIA 32.0.16.2124, and an inactive desktop. Each timing series
used 12 fresh processes after one warm-up process, with filesystem caches warm.

**Request for the Windows D3D11 driver team.** Can you confirm why the driver
resolves the same display device ten times during one creation call and whether
it could reuse that lookup within the call? This report covers one driver
version. Whether an earlier version behaved differently is unknown.

## Standalone reproducer

Build the [single source file](D3D11DeviceStartup.cpp) from an x64 Visual Studio
Developer PowerShell with the Windows SDK installed. From the directory
containing the source, run the following commands.

```powershell
cl /nologo /O2 /EHsc /std:c++17 D3D11DeviceStartup.cpp /link d3d11.lib
.\D3D11DeviceStartup.exe | Out-Null
1..12 | ForEach-Object { .\D3D11DeviceStartup.exe }
```

Each line gives the graphics adapter's PCI vendor and device ID, followed by
both creation times. Our adapter was `10DE:2204`. The original timing probe
observed 208.8–220.9 ms for the first call.

## Observed driver calls

During the first creation, `nvwgf2umx.dll` called `SetupDiGetClassDevsW`
ten times with null class and enumerator and `DIGCF_PRESENT | DIGCF_ALLCLASSES`.
Each pass visited the same 279 Plug and Play devices, issued 500
`SPDRP_COMPATIBLEIDS` queries, and issued two `SPDRP_DRIVER` queries for the
same display device. Registry reads between passes included
`UserModeDriverName`, `OpenGLDriverName`, and `DriverSupportModules`.
The second device creation produced zero of these all-class scans.
[Windows device enumeration](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdigetclassdevsw)

The process also called `CryptQueryObject` on four files from the active NVIDIA
driver package. It requested binary, embedded PKCS #7 signed data.
`nvgpucomp64.dll` and `nvwgf2umx.dll` accounted for most of the time in a
separate replay. The other files were `NvMemMapStoragex.dll` and `nvppex.dll`.
The second creation produced zero file queries through this API.
[Windows signed-object query](https://learn.microsoft.com/en-us/windows/win32/api/wincrypt/nf-wincrypt-cryptqueryobject)

## Independent timings

| Standalone operation | Median elapsed |
| --- | ---: |
| Find the RTX 3090 ten times, searching all present device classes | 74.0 ms |
| Find it once, searching all present device classes | 10.7 ms |
| Find it ten times, restricting the search to the display class | 4.0 ms |
| Query signed data from the four driver DLLs in the observed order | 55.0 ms |

The device lookup replay used `SetupDiEnumDeviceInfo`, queried the buffer size
and value of `SPDRP_COMPATIBLEIDS`, and stopped at `PCI\VEN_10DE&DEV_2204`.
It reproduced the observed 279 visits and 500 compatible-ID queries per pass.
The display-class variant visited three devices and found the same adapter.
The certificate replay supplied the observed content and format flags and
requested the certificate store and message handles. These are independent
workload timings. They estimate the opportunity for a driver change. An
end-to-end saving requires measurement with that change in place.

## Reproducing the driver trace

1. In a fresh x64 process, call `D3D11CreateDevice` with the default adapter,
   `D3D_DRIVER_TYPE_HARDWARE`, `D3D11_CREATE_DEVICE_BGRA_SUPPORT`, and feature
   levels 11.1 and 11.0. Request the device and immediate context. On success,
   repeat while holding both first-call interfaces. Record elapsed times
   around each call without a debugger.
2. In a separate launch under a native debugger, mark both device creation
   entries. Count calls to `SetupDiGetClassDevsW`, `SetupDiEnumDeviceInfo`,
   and `SetupDiGetDeviceRegistryPropertyW` by caller, flags, property, and
   device instance. Record file arguments to `CryptQueryObject`. Use this
   launch for call counts only.
3. When comparing drivers, keep the Windows build, physical adapter, device
   inventory, and probe executable fixed. Record the driver version and first
   and second device times for each run.

The most direct vendor experiment is to reuse the device lookup across the
initialization sequence, preserving adapter identity on multi-GPU systems and
handling topology changes. The signed-data path warrants separate investigation
with the driver's integrity guarantees preserved. These observations cover one
driver version and one machine.
