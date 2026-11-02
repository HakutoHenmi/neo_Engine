// Texture tool for the Release package (Package-SlimeResources.ps1 builds and runs it).
//   refs <model.fbx>...    : print, one per line, the colour images each model reads (same choice as Engine/Model.cpp)
//   dds <in.png> <out.dds> : write a BCn sRGB DDS with a full mip chain (opaque: BC1, with alpha: BC7)
#include <d3d11.h>
#include <wrl/client.h>
#include "../externals/DirectXTex/DirectXTex.h"
#include "../externals/ufbx/ufbx.h"
#include <filesystem>
#include <iostream>
#include <string>
using namespace DirectX;
using Microsoft::WRL::ComPtr;

static bool Check(HRESULT hr, const char* step) {
    if (SUCCEEDED(hr)) return true;
    std::cerr << step << " failed: HRESULT " << std::hex << hr << "\n";
    return false;
}

// Model.cpp reads the base colour texture (or the FBX diffuse one) from the file next to the model
static int Refs(int argc, wchar_t** argv) {
    for (int i = 2; i < argc; ++i) {
        const std::filesystem::path model(argv[i]);
        ufbx_load_opts opts = {0};
        opts.ignore_missing_external_files = true;
        ufbx_error error;
        ufbx_scene* scene = ufbx_load_file(model.string().c_str(), &opts, &error);
        if (!scene) { std::wcerr << L"cannot read " << argv[i] << L"\n"; return 1; }
        for (size_t m = 0; m < scene->materials.count; ++m) {
            const ufbx_material* mat = scene->materials.data[m];
            const ufbx_texture* tex = mat->pbr.base_color.texture;
            if (!tex) tex = mat->fbx.diffuse_color.texture;
            if (!tex || tex->content.size > 0) continue;
            std::string name = tex->relative_filename.data;
            if (name.empty()) name = tex->filename.data;
            if (!name.empty()) std::cout << (model.parent_path() / name).lexically_normal().generic_string() << "\n";
        }
        ufbx_free_scene(scene);
    }
    return 0;
}

static int Dds(const wchar_t* in, const wchar_t* out) {
    // 16-bit PNGs become 8-bit. The values stay sRGB-encoded (no colour conversion) and are labelled sRGB
    ScratchImage source, rgba, mips, compressed;
    if (!Check(LoadFromWICFile(in, WIC_FLAGS_IGNORE_SRGB, nullptr, source), "load")) return 1;
    const Image& image = *source.GetImage(0, 0, 0);
    if (image.format == DXGI_FORMAT_R8G8B8A8_UNORM) { if (!Check(rgba.InitializeFromImage(image), "copy")) return 1; }
    else if (!Check(Convert(image, DXGI_FORMAT_R8G8B8A8_UNORM, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, rgba), "convert")) return 1;
    if (!Check(rgba.OverrideFormat(DXGI_FORMAT_R8G8B8A8_UNORM_SRGB), "srgb")) return 1;
    if (!Check(GenerateMipMaps(*rgba.GetImage(0, 0, 0), TEX_FILTER_DEFAULT, 0, mips), "mips")) return 1;
    const bool alpha = !mips.IsAlphaAllOpaque();
    const DXGI_FORMAT format = alpha ? DXGI_FORMAT_BC7_UNORM_SRGB : DXGI_FORMAT_BC1_UNORM_SRGB;
    // BC7 is encoded on the GPU (slow on the CPU). Without a GPU, use the quick CPU encoder
    ComPtr<ID3D11Device> device;
    bool done = false;
    if (alpha && SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &device, nullptr, nullptr)))
        done = SUCCEEDED(Compress(device.Get(), mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), format, TEX_COMPRESS_DEFAULT, 1.0f, compressed));
    if (!done && !Check(Compress(mips.GetImages(), mips.GetImageCount(), mips.GetMetadata(), format,
        TEX_COMPRESS_PARALLEL | TEX_COMPRESS_BC7_QUICK, TEX_THRESHOLD_DEFAULT, compressed), "compress")) return 1;
    std::filesystem::create_directories(std::filesystem::path(out).parent_path());
    if (!Check(SaveToDDSFile(compressed.GetImages(), compressed.GetImageCount(), compressed.GetMetadata(), DDS_FLAGS_NONE, out), "save")) return 1;
    return 0;
}

int wmain(int argc, wchar_t** argv) {
    if (argc >= 2 && std::wstring(argv[1]) == L"refs") return Refs(argc, argv);
    if (argc == 4 && std::wstring(argv[1]) == L"dds") {
        if (FAILED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) return 1;
        return Dds(argv[2], argv[3]);
    }
    std::cerr << "usage: Compress-Textures refs <model.fbx>... | dds <in> <out.dds>\n";
    return 2;
}
