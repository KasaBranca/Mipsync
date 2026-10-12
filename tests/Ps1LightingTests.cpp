#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
extern "C" {
#include "lighting.h"
}

static void check(bool value, const char* message) {
    if (!value) { std::fprintf(stderr, "%s\n", message); std::exit(1); }
}
static double reference(const ps1_lighting_light* lights, unsigned count,
    const int32_t* p, const int16_t* n, unsigned channel, uint8_t base) {
    double sum = .15 * .12;
    for (unsigned i = 0; i < count; ++i) {
        const auto& l = lights[i];
        double dir[3], atten = 1;
        if (l.type == 0) {
            for (int a = 0; a < 3; ++a) dir[a] = l.direction[a] / 4096.;
        } else {
            double dist2 = 0;
            for (int a = 0; a < 3; ++a) { dir[a] = double(l.position[a]) - p[a]; dist2 += dir[a]*dir[a]; }
            const double dist = std::sqrt(dist2);
            for (int a = 0; a < 3; ++a) dir[a] = dist > 0 ? dir[a]/dist : (a == 1 ? 1 : 0);
            if (l.range > 0) { const double t = std::clamp(1-dist/l.range, 0., 1.); atten = t*t; }
            if (l.type == 2) {
                double cosine = 0;
                for (int a = 0; a < 3; ++a) cosine += l.direction[a]/4096. * dir[a];
                const double outer = l.cos_outer/4096., inner = l.cos_inner/4096.;
                const double t = std::clamp((cosine-outer)/(inner-outer), 0., 1.);
                atten *= t*t*(3-2*t);
            }
        }
        double diffuse = 0;
        for (int a = 0; a < 3; ++a) diffuse += n[a]/4096. * dir[a];
        sum += std::max(diffuse, 0.) * atten * l.color_q12[channel]/4096.;
    }
    return std::clamp(base * sum, 0., 255.);
}
int main() {
    ps1_lighting_light lights[8] = {};
    lights[0].direction[1] = 4096;
    lights[0].color_q12[0] = 2048;
    lights[1].type = 1; lights[1].position[1] = 640; lights[1].range = 1280;
    lights[1].color_q12[1] = 8192;
    int32_t p[3] = {}; int16_t n[3] = {0,4096,0};
    uint8_t base[3] = {200,200,200}, out[3];
    ps1_lighting_evaluate(lights,2,p,n,base,out);
    check(std::abs(int(out[0])-104)<=1 && std::abs(int(out[1])-104)<=1 && out[2]==4,
        "Two lights, HDR intensity and squared point falloff must accumulate independently");
    p[1] = -640;
    ps1_lighting_evaluate(lights,2,p,n,base,out);
    check(out[1]==4 && out[0]>100,"Point range boundary must leave other lights active");
    p[1] = 640;
    ps1_lighting_evaluate(lights,2,p,n,base,out);
    check(out[1]==255,"Coincident point and vertex must use the editor's up direction");
    lights[2] = lights[1]; lights[2].type=2; lights[2].color_q12[2]=6144;
    lights[2].direction[1]=4096; lights[2].cos_outer=2896; lights[2].cos_inner=3547;
    lights[3] = lights[1]; lights[3].range=0; lights[3].color_q12[0]=819;
    ps1_lighting_light prepared[8] = {};
    for(int x=-800;x<=800;x+=40) for(int z=-800;z<=800;z+=80) {
        p[0]=x; p[1]=0; p[2]=z;
        ps1_lighting_evaluate(lights,4,p,n,base,out);
        for(unsigned c=0;c<3;++c)
            check(std::abs(out[c]-reference(lights,4,p,n,c,base[c]))<=2.5,
                "Fixed point lighting differs from desktop shader reference");
        const unsigned count = ps1_lighting_prepare(lights,4,p,prepared);
        uint8_t reused[3];
        ps1_lighting_evaluate(prepared,count,p,n,base,reused);
        for(unsigned c=0;c<3;++c)
            check(std::abs(int(reused[c])-out[c])<=1,
                "Reusing an animated light sample must preserve attenuation and colour");
    }
    p[0]=1000000; p[1]=0; p[2]=-1000000;
    ps1_lighting_evaluate(lights,4,p,n,base,out);
    for(unsigned c=0;c<3;++c)
        check(std::abs(out[c]-reference(lights,4,p,n,c,base[c]))<=2.5,
            "Large world coordinates and unlimited range must remain normalized without overflow");
    n[1]=-4096; p[0]=p[1]=p[2]=0;
    ps1_lighting_evaluate(lights,4,p,n,base,out);
    check(out[0]==4 && out[1]==4 && out[2]==4,"Back-facing normals receive only ambient");
    // Eight lights and late saturation: a dark material can still receive HDR light.
    for(auto& l:lights) { l={}; l.direction[1]=4096; l.color_q12[0]=4096; }
    n[1]=4096; base[0]=20;
    ps1_lighting_evaluate(lights,8,p,n,base,out);
    check(out[0]==160,"All eight lights must add before material multiplication and saturation");
    std::puts("PS1 multiple directional/point/spot lighting matches desktop falloff and accumulation");
}
