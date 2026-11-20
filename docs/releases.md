# Releases and compatibility

Use the latest patch of the minor line your application has tested. An annotated
`vMAJOR.MINOR.PATCH` tag identifies the source, submodule pin, public headers,
and build recipes. Published version tags and release assets are immutable.
Corrections get a new patch version.

The initial 0.3.0 release was reissued once to correct the editor menu bar.
The changelog identifies the original build and the updated vcpkg port revision.

| Change | Compatibility commitment |
| --- | --- |
| 0.x patch | Existing source using documented headers, CMake targets and components continues to build. Bug fixes may change behavior that contradicted the guide. |
| 0.x minor | May add or change public APIs. Source-breaking changes and upgrade steps go in the changelog. |
| 1.0 and later | Breaking documented API changes require a new major version. Backward-compatible features can ship in a minor release. |

The 0.3 CMake package uses `SameMinorVersion`. Use
`find_package(Composia 0.3 CONFIG REQUIRED)` for the line, or
`find_package(Composia 0.3.0 EXACT CONFIG REQUIRED)` to pin a patch.
Public headers and exported targets are covered. Private headers, examples and
test helpers can evolve between releases. Rebuild consumers when upgrading
the static library. Binary compatibility across toolsets, runtime choices or
configurations is outside the compatibility commitment.

## Installing release assets

The x64 SDK ZIP contains Debug and Release static libraries, headers, the
application manifest and CMake package files. Extract it and set
`CMAKE_PREFIX_PATH` to its root. Supply WIL and C++/WinRT through your vcpkg
toolchain, then use the same `find_package` and static CRT settings as the
source install. The editor ZIP contains a runnable application and its license.
Verify downloads against `SHA256SUMS` on the release page.
[Installation](../README.md#using-composia) · [Releases](https://github.com/BehroozRezvani/Composia/releases)

## Cutting a release

1. Update the CMake and vcpkg versions, changelog date and migration notes. Pin
   the overlay's source revision to the commit containing the intended public
   library. Check the package and consumer manifests against the vcpkg baseline.
2. Run Debug and Release core tests, the overlay consumer and the relevant
   hardware/WARP desktop tests. Record OS versions, skips and any outstanding
   qualification in the release notes. Inspect the editor and verify its
   document round trip. Review the Windows CI results for the release commit.
3. Build both configurations, then run `./scripts/package-release.ps1`. Check
   both ZIP files and test a consumer against the extracted SDK. The archives
   and their SHA-256 manifest are written to `out/dist`.
4. Create the annotated version tag on the verified commit. Push the tag and
   publish a GitHub Release containing both ZIPs, checksums, migration notes and
   links to the reference application and measured comparison.
