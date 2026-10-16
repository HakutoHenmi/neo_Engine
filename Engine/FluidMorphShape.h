#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
namespace Engine::FluidShape {
struct Point {float x=0,y=0,z=0;};
inline uint32_t Spread(uint32_t n){n&=1023;n=(n|(n<<16))&0x030000ff;n=(n|(n<<8))&0x0300f00f;n=(n|(n<<4))&0x030c30c3;n=(n|(n<<2))&0x09249249;return n;}
inline uint32_t Morton(Point p){auto q=[](float v){return uint32_t(std::clamp((v+12)/24,0.f,.99999f)*1024);};return Spread(q(p.x))|(Spread(q(p.y))<<1)|(Spread(q(p.z))<<2);}
inline float Halton(uint32_t n,uint32_t base){float v=0,f=1;while(n){f/=float(base);v+=f*float(n%base);n/=base;}return v;}
inline Point Cross(Point a,Point b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline Point Sub(Point a,Point b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
inline float Dot(Point a,Point b){return a.x*b.x+a.y*b.y+a.z*b.z;}
struct Mesh {
    std::vector<Point> vertices;std::vector<std::array<int,3>> faces;
    Point lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};float scale=1;
    explicit Mesh(const std::string& path){
        std::ifstream input(path);if(!input)throw std::runtime_error("Missing slime shape: "+path);
        std::string line;while(std::getline(input,line)){std::istringstream s(line);std::string type;s>>type;
            if(type=="v"){Point p;s>>p.x>>p.y>>p.z;vertices.push_back(p);lo={std::min(lo.x,p.x),std::min(lo.y,p.y),std::min(lo.z,p.z)};hi={std::max(hi.x,p.x),std::max(hi.y,p.y),std::max(hi.z,p.z)};}
            if(type=="f"){std::array<int,3> f{};for(int& i:f){std::string token;s>>token;i=std::stoi(token)-1;}faces.push_back(f);}}
        if(faces.empty())throw std::runtime_error("Empty slime shape: "+path);
        double volume=0;for(const auto& f:faces)volume+=Dot(vertices[size_t(f[0])],Cross(vertices[size_t(f[1])],vertices[size_t(f[2])]))/6.0;
        scale=float(std::cbrt(24.5/std::max(.01,std::abs(volume))));
    }
    bool Contains(Point p)const {
        // Oblique ray avoids coplanar box edges; meshes are closed, non-overlapping solids.
        Point ray{1,.12347f,.05719f};int crossings=0;
        for(const auto& f:faces){Point a=vertices[size_t(f[0])],e1=Sub(vertices[size_t(f[1])],a),e2=Sub(vertices[size_t(f[2])],a);
            Point h=Cross(ray,e2);float d=Dot(e1,h);if(std::abs(d)<1e-7f)continue;
            Point t=Sub(p,a);float u=Dot(t,h)/d;if(u<0||u>1)continue;Point q=Cross(t,e1);float v=Dot(ray,q)/d;
            if(v<0||u+v>1)continue;if(Dot(e2,q)/d>1e-6f)++crossings;}
        return (crossings&1)!=0;
    }
};
inline std::vector<Point> Sample(const Mesh* mesh,uint32_t count){
    std::vector<Point> points;points.reserve(count);
    for(uint32_t i=1;points.size()<count;++i){
        if(i>count*200+200)throw std::runtime_error("Slime shape volume sampling failed");
        Point p;
        if(mesh){p={mesh->lo.x+(mesh->hi.x-mesh->lo.x)*Halton(i,2),mesh->lo.y+(mesh->hi.y-mesh->lo.y)*Halton(i,3),mesh->lo.z+(mesh->hi.z-mesh->lo.z)*Halton(i,5)};if(!mesh->Contains(p))continue;p={p.x*mesh->scale,p.y*mesh->scale,p.z*mesh->scale};}
        else{p={(Halton(i,2)*2-1)*2.55f,Halton(i,3)*1.8f,(Halton(i,5)*2-1)*2.55f};if(p.x*p.x/(2.55f*2.55f)+p.y*p.y/(1.8f*1.8f)+p.z*p.z/(2.55f*2.55f)>1)continue;}
        points.push_back(p);
    }
    std::stable_sort(points.begin(),points.end(),[](Point a,Point b){return Morton(a)<Morton(b);});return points;
}
}
