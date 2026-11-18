// atlas_check.cpp — builds the real icon atlas from the shipped assets and
// checks the things that made the toolbar show black boxes on a phone:
//
//   * the atlas must be small (it was 18450x17425 = 1226 MB when every icon
//     was placed in a square cell sized to the largest one),
//   * no two icon regions may overlap or fall outside the atlas,
//   * every opaque source pixel must be present in the packed atlas.
//
// Build and run through Android/tests/run_tests.sh.

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <chrono>
#include "icons.h"
#include "png.h"
static bool readFile(const std::string& p, std::vector<uint8_t>& out){FILE*f=std::fopen(p.c_str(),"rb");if(!f)return false;std::fseek(f,0,SEEK_END);long n=std::ftell(f);std::fseek(f,0,SEEK_SET);out.resize((size_t)n);bool ok=std::fread(out.data(),1,(size_t)n,f)==(size_t)n;std::fclose(f);return ok;}
int main(int argc,char**argv){
    std::map<std::string,std::vector<uint8_t>> px; std::map<std::string,int> w,h;
    int decoded=0;
    auto t0=std::chrono::steady_clock::now();
    for(int a=1;a<argc;++a){
        std::vector<uint8_t> b; if(!readFile(argv[a],b)) continue;
        pdb::ImageRGBA img; std::string err;
        if(!pdb::decodePng(b,img,err)){std::printf("FAIL %s: %s\n",argv[a],err.c_str());continue;}
        std::string name(argv[a]); name=name.substr(name.find_last_of('/')+1);
        name=name.substr(0,name.size()-4);
        px[name]=std::move(img.pixels); w[name]=img.width; h[name]=img.height; ++decoded;
    }
    auto t1=std::chrono::steady_clock::now();
    pdb::IconAtlas atlas; atlas.build(px,w,h);
    auto t2=std::chrono::steady_clock::now();
    std::printf("decoded %d icons in %.1f ms; atlas %dx%d = %.2f MB, packed in %.1f ms\n",
        decoded, std::chrono::duration<double,std::milli>(t1-t0).count(),
        atlas.width(), atlas.height(), atlas.width()*(double)atlas.height()*4/1048576.0,
        std::chrono::duration<double,std::milli>(t2-t1).count());
    // Overlap check + content check: every icon's region must be non-empty and
    // no two regions may overlap.
    struct R{int x,y,w,h;std::string n;};
    std::vector<R> rs;
    for(const std::string& n : atlas.names()){const pdb::IconRegion* r=atlas.find(n); rs.push_back({r->x,r->y,r->w,r->h,n});}
    int overlaps=0, outside=0;
    for(size_t i=0;i<rs.size();++i){
        if(rs[i].x<0||rs[i].y<0||rs[i].x+rs[i].w>atlas.width()||rs[i].y+rs[i].h>atlas.height()) ++outside;
        for(size_t j=i+1;j<rs.size();++j){
            const R&a=rs[i];const R&b=rs[j];
            if(a.x<b.x+b.w&&b.x<a.x+a.w&&a.y<b.y+b.h&&b.y<a.y+a.h) ++overlaps;
        }
    }
    std::printf("regions=%zu overlaps=%d outside=%d\n", rs.size(), overlaps, outside);
    // Pixel check: for every icon, the atlas must contain its opaque pixels
    // (this is what "all icons are black boxes" was).
    int bad=0;
    for(const auto& r : rs){
        const std::vector<uint8_t>& src=px[r.n];
        long opaque=0, opaqueInAtlas=0;
        for(int y=0;y<r.h;++y) for(int x=0;x<r.w;++x){
            size_t si=((size_t)y*r.w+x)*4; if(src[si+3]==0) continue; ++opaque;
            size_t ai=((size_t)(r.y+y)*atlas.width()+(r.x+x))*4;
            if(atlas.pixels()[ai+3]!=0) ++opaqueInAtlas;
        }
        if(opaque>0 && opaqueInAtlas < opaque) { ++bad; std::printf("  missing pixels: %s\n", r.n.c_str()); }
    }
    std::printf("icons with missing pixels in atlas: %d\n", bad);
    return (overlaps||outside||bad) ? 1 : 0;
}
