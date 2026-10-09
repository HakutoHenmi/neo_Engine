#include <ufbx.h>
#include <cstdio>
#include <algorithm>
#include <vector>
int main(int argc,char**argv){
    for(int i=1;i<argc;++i){ufbx_load_opts opts{};opts.ignore_missing_external_files=true;
        opts.target_axes=ufbx_axes_right_handed_y_up;opts.target_unit_meters=1;
        ufbx_error error{};auto*s=ufbx_load_file(argv[i],&opts,&error);if(!s)return 2;
        printf("%s\n",argv[i]);FILE* csv=nullptr;if(i==argc-1)fopen_s(&csv,"tests/out/scenery-uv.csv","w");
        FILE* triangles=nullptr;if(i==argc-1)fopen_s(&triangles,"tests/out/scenery-triangles.csv","w");
        for(size_t j=0;j<s->meshes.count;++j){auto*m=s->meshes.data[j];
            printf("mesh %s triangles %zu\n",m->name.data,m->num_triangles);
            if(triangles&&m->vertex_uv.exists){std::vector<uint32_t> indices(m->max_face_triangles*3);for(size_t f=0;f<m->faces.count;++f){uint32_t count=ufbx_triangulate_face(indices.data(),indices.size(),m,m->faces.data[f]);for(size_t t=0;t<count*3;++t){auto uv=ufbx_get_vertex_vec2(&m->vertex_uv,indices[t]);fprintf(triangles,"%g,%g\n",uv.x,uv.y);}}}
            for(size_t n=0;n<m->instances.count;++n){auto*p=m->instances.data[n];
                ufbx_vec3 lo{1e9,1e9,1e9},hi{-1e9,-1e9,-1e9};
                for(size_t v=0;v<m->num_indices;++v){auto pos=ufbx_transform_position(&p->geometry_to_world,ufbx_get_vertex_vec3(&m->vertex_position,v));
                    lo.x=std::min(lo.x,pos.x);lo.y=std::min(lo.y,pos.y);lo.z=std::min(lo.z,pos.z);
                    hi.x=std::max(hi.x,pos.x);hi.y=std::max(hi.y,pos.y);hi.z=std::max(hi.z,pos.z);
                    if(csv&&m->vertex_uv.exists){auto uv=ufbx_get_vertex_vec2(&m->vertex_uv,v);fprintf(csv,"%g,%g,%g,%g,%g\n",pos.x,pos.y,pos.z,uv.x,uv.y);}}
                printf(" instance %s visible %d bounds (%g,%g,%g)..(%g,%g,%g)\n",p->name.data,p->visible,lo.x,lo.y,lo.z,hi.x,hi.y,hi.z);
            }
        }
        if(csv)fclose(csv);if(triangles)fclose(triangles);ufbx_free_scene(s);
    }
}
