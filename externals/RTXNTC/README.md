# NVIDIA RTX Neural Texture Compression (runtime only)

Headers: `include/libntc` from RTXNTC-Library at commit `35ec039503ef5e151c661beccc82390948dd5218` (the library submodule of RTXNTC v0.10.0-beta). Only the files the engine includes are vendored.
Binary: `bin/libntc.dll` from the official SDK `RTXNTC_windows_x64_v0.10.0b.zip`, SHA256 `DB97F3AC6EF032525C29C87278568A8C67DEF7F36FC90AA918675E74C3989C71`. DLLs are ignored by Git.
License: NVIDIA RTX SDKs License as shipped with RTXNTC (`LICENSE.txt`; copied into the package as `NTC/NVIDIA-RTX-SDK-LICENSE.txt`). RapidJSON (MIT) notice in `ThirdPartyLicenses.txt`.

`Engine/RendererNtc.cpp` loads the DLL at runtime from `NTC/` next to the executable (Release package) or from `externals/RTXNTC/bin` relative to the working directory (development). It uses the D3D12 non-CoopVec GenericInt8 path only; CUDA is disabled. If the DLL is missing or the GPU cannot run INT8 inference, `Renderer::LoadNtcTextureSet` returns no textures and the caller falls back to DDS/JPEG maps.

Restore the DLL by running `tests/Run-RoadTextureTests.ps1 -AcquireSdk` (downloads and verifies the SDK into `build/ntc`) and copying `build/ntc/sdk/bin/windows-x64/libntc.dll` here. `scripts/Package-Ntc.ps1` refuses any other binary.

Shipped asset: `Resources/Textures/PolyHaven/asphalt_02_material.ntc` (albedo / roughness / AO, requested 12 bpp, 30,000 steps) with the original BC5 normal `asphalt_02_nor_dx_2k.dds`. See `Docs/RoadTextureHybridExperiment.md`.
