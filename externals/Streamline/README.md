# Optional NVIDIA Streamline / DLSS SR

Headers: NVIDIA Streamline 2.14.1, MIT (LICENSE.txt).
DLSS binaries: NVIDIA RTX SDK license, accepted by the user in this task on 2026-10-06; see bin/nvngx_dlss.license.txt and Docs/NVIDIA-RTX-SDK-LICENSE.txt.

The game dynamically loads signed release DLLs from `externals/Streamline/bin` relative to its working directory. DLLs are ignored by Git. Missing/unsupported DLLs preserve the native renderer. Only Super Resolution is requested; Frame Generation and Ray Reconstruction are not loaded.

The initial experimental mode is Quality at 1920x1080 output, with input size queried from DLSS. The UI setting is applied on the next game launch. The scene renders at the queried input size, DLSS runs before HDR bloom/tonemapping, and UI is composited afterwards.

Upstream release: https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.14.1
SDK zip SHA256: 92C4D954631A1710DA86CA3FA8D5034F2B9503838C95FC4AE977AE149319781B

Restore the optional DLLs using `scripts/Setup-Streamline.ps1 -SdkArchive <downloaded-sdk.zip>` after accepting the NVIDIA license. Do not replace DLLs with unsigned development binaries. Runtime verifies the secondary Streamline signatures and the standard NVIDIA NGX Authenticode signature.
