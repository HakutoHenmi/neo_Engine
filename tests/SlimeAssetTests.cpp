#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <cassert>
#include <cmath>
#include <iostream>
int main(){
    for(const char* path:{"Resources/Models/Ink/slime-beam.obj","Resources/Models/Ink/slime-ring.obj"}){
        Assimp::Importer importer;
        const auto* scene=importer.ReadFile(path,aiProcess_FlipWindingOrder|aiProcess_FlipUVs|aiProcess_Triangulate|aiProcess_LimitBoneWeights|aiProcess_PreTransformVertices);
        if(!scene){std::cerr<<path<<": "<<importer.GetErrorString()<<'\n';return 1;}
        assert(scene->mNumMeshes>0);
        for(unsigned m=0;m<scene->mNumMeshes;++m){const auto* mesh=scene->mMeshes[m];
            assert(mesh->mNumVertices>0&&mesh->mNumFaces>0&&mesh->HasNormals()&&mesh->HasTextureCoords(0));
            for(unsigned i=0;i<mesh->mNumVertices;++i){const auto& v=mesh->mVertices[i];assert(std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z));}
        }
    }
    std::cout<<"PASS: beam and ring load with the production Assimp flags, normals and UVs\n";
}
