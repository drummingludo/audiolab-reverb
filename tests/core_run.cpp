// Fait tourner le moteur C++ du plugin sur le même signal que ref_run.js.
// Usage : core_run <fs> <params.txt> <in.f32> <out.f32> [bloc]
// params.txt : une ligne par réglage « échantillon mode predelay decay mix size damp width » (la 1re à l'échantillon 0)
#include "../Source/ReverbCore.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main (int argc, char** argv)
{
    if (argc < 5) return 1;
    const double fs = atof (argv[1]);
    const int block = argc > 5 ? atoi (argv[5]) : 128;
    struct Ch { long at; audiolab::rv::ReverbEngine::Params p; };
    std::vector<Ch> ch;
    FILE* pf = fopen (argv[2], "r");
    Ch c {};
    while (fscanf (pf, "%ld %d %lf %lf %lf %lf %lf %lf", &c.at, &c.p.mode, &c.p.predelay, &c.p.decay, &c.p.mix, &c.p.size, &c.p.damp, &c.p.width) == 8) ch.push_back (c);
    fclose (pf);
    FILE* f = fopen (argv[3], "rb");
    fseek (f, 0, SEEK_END); long bytes = ftell (f); fseek (f, 0, SEEK_SET);
    std::vector<float> in ((size_t) bytes / 4);
    if (fread (in.data(), 4, in.size(), f) != in.size()) return 3;
    fclose (f);
    const long N = (long) in.size() / 2;
    std::vector<float> L ((size_t) N), R ((size_t) N), oL ((size_t) N), oR ((size_t) N), out (in.size());
    for (long i = 0; i < N; ++i) { L[(size_t) i] = in[(size_t) (2 * i)]; R[(size_t) i] = in[(size_t) (2 * i + 1)]; }
    audiolab::rv::ReverbEngine e (fs);
    e.setParams (ch[0].p); e.snap();
    size_t ci = 1;
    for (long s = 0; s < N; s += block)
    {
        while (ci < ch.size() && ch[ci].at <= s) e.setParams (ch[ci++].p);
        const int n = (int) std::min ((long) block, N - s);
        e.process (L.data() + s, R.data() + s, oL.data() + s, oR.data() + s, n);
    }
    for (long i = 0; i < N; ++i) { out[(size_t) (2 * i)] = oL[(size_t) i]; out[(size_t) (2 * i + 1)] = oR[(size_t) i]; }
    FILE* o = fopen (argv[4], "wb");
    fwrite (out.data(), 4, out.size(), o);
    fclose (o);
    return 0;
}
