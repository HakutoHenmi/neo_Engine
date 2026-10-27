// Offline material inspection: uses the same ufbx base-color lookup as Engine/Model.cpp.
// Output: model filename, relative texture filename, absolute filename, embedded bytes (TSV).
#include "ufbx.h"
#include <filesystem>
#include <cstdio>

int main(int argc, char** argv) {
    for (int arg = 1; arg < argc; ++arg) {
        for (const auto& item : std::filesystem::recursive_directory_iterator(argv[arg])) {
            if (item.path().extension() != ".fbx") continue;
            ufbx_error error{};
            ufbx_scene* scene = ufbx_load_file(item.path().string().c_str(), nullptr, &error);
            if (!scene) {
                std::fprintf(stderr, "FBX parse failed: %s\n", item.path().string().c_str());
                return 1;
            }
            for (size_t index = 0; index < scene->materials.count; ++index) {
                auto material = scene->materials.data[index];
                auto texture = material->pbr.base_color.texture;
                if (!texture) texture = material->fbx.diffuse_color.texture;
                if (texture) std::printf("%s\t%s\t%s\t%zu\n", item.path().filename().string().c_str(),
                    texture->relative_filename.data, texture->filename.data, texture->content.size);
            }
            ufbx_free_scene(scene);
        }
    }
    return 0;
}
