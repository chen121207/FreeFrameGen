// Color-only fallback: these vectors are estimates, not FGDS engine motion.
Texture2D<float4> A : register(t0);
Texture2D<float4> B : register(t1);
Texture2D<float4> Forward : register(t2);
Texture2D<float4> Backward : register(t3);
Texture2D<float4> CoarseA : register(t4);
Texture2D<float4> CoarseB : register(t5);
RWTexture2D<float4> Out : register(u0);
SamplerState LinearClamp : register(s0);
cbuffer Params : register(b0) { uint W; uint H; uint Mode; float Alpha; }
float3 color(Texture2D<float4> t, float2 p) {
    return t.SampleLevel(LinearClamp, (p + 0.5) / float2(W,H), 0).rgb;
}
float errorAt(int2 p, int2 delta) {
    float e = 0;
    [unroll] for(int y=-2; y<=2; ++y)
    [unroll] for(int x=-2; x<=2; ++x) {
        int2 q = p + int2(x,y)*2;
        float3 a = Mode==0 ? color(A,q) : color(B,q);
        float3 b = Mode==0 ? color(B,q+delta) : color(A,q+delta);
        e += dot(abs(a-b), float3(.299,.587,.114));
    }
    return e/25;
}
float coarseError(int2 p, int2 delta) {
    uint cw,ch;CoarseA.GetDimensions(cw,ch);
    float2 uv=(float2(p)+.5)/4/float2(cw,ch);
    float2 duv=float2(delta)/4/float2(cw,ch);
    float e=0;
    [unroll] for(int y=-1;y<=1;++y)
    [unroll] for(int x=-1;x<=1;++x) {
        float2 q=uv+float2(x,y)/float2(cw,ch);
        float3 a=Mode==0?CoarseA.SampleLevel(LinearClamp,q,0).rgb:CoarseB.SampleLevel(LinearClamp,q,0).rgb;
        float3 b=Mode==0?CoarseB.SampleLevel(LinearClamp,q+duv,0).rgb:CoarseA.SampleLevel(LinearClamp,q+duv,0).rgb;
        e+=dot(abs(a-b),float3(.299,.587,.114));
    }
    return e/9;
}
[numthreads(8,8,1)]
void estimate(uint3 tid:SV_DispatchThreadID) {
    uint2 grid = (uint2(W,H)+7)/8;
    if(any(tid.xy>=grid)) return;
    int2 p = min(int2(tid.xy)*8+4, int2(W,H)-1);
    int2 best = 0;
    float score = coarseError(p,0);
    // Coarse search then local refinement. Small-motion prior resolves flat/repeated patches.
    for(int y=-16;y<=16;y+=4) for(int x=-16;x<=16;x+=4) {
        int2 d=int2(x,y);
        float e=coarseError(p,d)+.00002*dot(float2(d),float2(d));
        if(e<score) {score=e;best=d;}
    }
    int2 coarse=best;
    float coarseFullError=errorAt(p,best);
    score=coarseFullError+.00002*dot(float2(best),float2(best));
    // Averaging 4x4 pixels removes the signal of fine repeating/high-frequency
    // textures.  When the pyramid is ambiguous (near-zero displacement) or its
    // full-resolution residual is high, evaluate a small, even-pixel search as
    // a competing seed.  A confident coarse displacement skips this extra work.
    if((abs(coarse.x)<=4 && abs(coarse.y)<=4) || coarseFullError>.04) {
        for(int y=-8;y<=8;y+=2) for(int x=-8;x<=8;x+=2) {
            int2 d=int2(x,y);
            float e=errorAt(p,d)+.00002*dot(float2(d),float2(d));
            if(e<score) {score=e;best=d;}
        }
    }
    coarse=best;
    for(int y=-3;y<=3;++y) for(int x=-3;x<=3;++x) {
        int2 d=coarse+int2(x,y);
        float e=errorAt(p,d)+.00002*dot(float2(d),float2(d));
        if(e<score) {score=e;best=d;}
    }
    Out[tid.xy]=float4(best,errorAt(p,best),1);
}
[numthreads(8,8,1)]
void downsample(uint3 tid:SV_DispatchThreadID) {
    uint ow,oh;Out.GetDimensions(ow,oh);if(any(tid.xy>=uint2(ow,oh)))return;
    float3 sum=0;
    [unroll] for(int y=0;y<4;++y)
    [unroll] for(int x=0;x<4;++x)sum+=A.Load(int3(min(tid.xy*4+uint2(x,y),uint2(W,H)-1),0)).rgb;
    Out[tid.xy]=float4(sum/16,1);
}
float4 flow(Texture2D<float4> t,float2 p) {
    uint gw,gh; t.GetDimensions(gw,gh);
    // Grid samples are centered at block pixel 4, not normalized image borders.
    return t.SampleLevel(LinearClamp, ((p-4)/8+.5)/float2(gw,gh),0);
}
bool inside(float2 p) {return all(p>=0)&&all(p<=float2(W-1,H-1));}
[numthreads(8,8,1)]
void interpolate(uint3 tid:SV_DispatchThreadID) {
    if(any(tid.xy>=uint2(W,H))) return;
    float2 p=tid.xy;
    if(Alpha<=0) {Out[tid.xy]=A[tid.xy];return;}
    if(Alpha>=1) {Out[tid.xy]=B[tid.xy];return;}
    float2 pa=p,pb=p;
    [unroll] for(int i=0;i<3;++i) {
        pa=p-Alpha*flow(Forward,pa).xy;
        pb=p-(1-Alpha)*flow(Backward,pb).xy;
    }
    float4 fa=flow(Forward,pa),fb=flow(Backward,pb);
    bool va=inside(pa)&&inside(pa+fa.xy)&&fa.z<.06&&length(fa.xy+flow(Backward,pa+fa.xy).xy)<2.5;
    bool vb=inside(pb)&&inside(pb+fb.xy)&&fb.z<.06&&length(fb.xy+flow(Forward,pb+fb.xy).xy)<2.5;
    float3 ca=color(A,pa),cb=color(B,pb);
    // No invented depth: disagreement/holes fall back to an endpoint, not depth sorting.
    float3 c=vb?cb:B[tid.xy].rgb;
    if(va&&vb&&length(ca-cb)<.2) c=lerp(ca,cb,Alpha);
    else if(va&&!vb) c=ca;
    Out[tid.xy]=float4(c,1);
}
[numthreads(8,8,1)]
void convert(uint3 tid:SV_DispatchThreadID) {
    if(any(tid.xy>=uint2(W,H))) return;
    // SRGB SRV performs IEC sRGB decode before bilinear resize; no CPU pixels.
    Out[tid.xy]=float4(A.SampleLevel(LinearClamp,(float2(tid.xy)+.5)/float2(W,H),0).rgb,1);
}
