// FFG v0.1 reference compute kernel. Bounded inverse-warp candidate search.
// Not optical flow, neural FG, or a production disocclusion reconstruction.
Texture2D<float4> color0:register(t0);
Texture2D<float> depth0:register(t1);
Texture2D<float2> motion0:register(t2);
Texture2D<uint> id0:register(t3);
Texture2D<float4> color1:register(t4);
Texture2D<float> depth1:register(t5);
Texture2D<float2> motion1:register(t6);
Texture2D<uint> id1:register(t7);
RWTexture2D<float4> outputFrame:register(u0);
cbuffer Params:register(b0){uint width;uint height;float alpha;uint reset;}
bool inside(int2 p){return all(p>=0)&&p.x<int(width)&&p.y<int(height);}
float2 mv(int side,int2 p){return side==0?motion0.Load(int3(p,0)):motion1.Load(int3(p,0));}
float depth(int side,int2 p){return side==0?depth0.Load(int3(p,0)):depth1.Load(int3(p,0));}
uint oid(int side,int2 p){return side==0?id0.Load(int3(p,0)):id1.Load(int3(p,0));}
float4 col(int side,int2 p){return side==0?color0.Load(int3(p,0)):color1.Load(int3(p,0));}
struct Candidate{float4 color;float z;uint id;bool valid;};
Candidate gather(int side,int2 dst,float phase){
    Candidate best;best.color=0;best.z=1e30;best.id=0;best.valid=false;
    // Multiple seeds allow foreground to arrive where the destination held background.
    // Bounded to +-32 px: large movement/thin geometry remains a known limitation.
    [loop] for(int sy=-1;sy<=1;++sy)[loop]for(int sx=-2;sx<=2;++sx){
        float2 q=float2(dst)+float2(sx*16,sy*16);
        [unroll]for(int k=0;k<3;++k){int2 p=int2(floor(q+0.5));if(!inside(p))break;float2 m=mv(side,p);if(!all(isfinite(m)))break;q=float2(dst)-phase*m;}
        int2 p=int2(floor(q+0.5));if(!inside(p))continue;
        float2 m=mv(side,p);float z=depth(side,p);uint id=oid(side,p);
        if(id==0||!isfinite(z)||z<=0||!all(isfinite(m)))continue;
        float2 residual=float2(p)+phase*m-float2(dst);
        if(any(abs(residual)>0.76))continue;
        // Check identity + depth/motion consistency if visible in both endpoints.
        // If hidden behind nearer geometry in the other endpoint, retain candidate.
        int2 other=int2(floor(float2(p)+m+0.5));
        if(inside(other)){
            uint otherId=oid(1-side,other);float otherDepth=depth(1-side,other);
            if(otherId==id){
                if(abs(z-otherDepth)>max(0.01,z*0.02)||any(abs(m+mv(1-side,other))>1.5))continue;
            }else if(otherDepth>=z-0.01)continue;
        }
        if(z<best.z){best.color=col(side,p);best.z=z;best.id=id;best.valid=true;}
    }
    return best;
}
[numthreads(8,8,1)]
void main(uint3 tid:SV_DispatchThreadID){
    if(tid.x>=width||tid.y>=height)return;int2 p=int2(tid.xy);
    if(reset!=0||alpha>=1){outputFrame[p]=color1.Load(int3(p,0));return;}
    if(alpha<=0){outputFrame[p]=color0.Load(int3(p,0));return;}
    Candidate a=gather(0,p,alpha),b=gather(1,p,1-alpha);
    float4 result=color1.Load(int3(p,0)); // unresolved hole: explicit latest-frame fallback
    if(a.valid&&b.valid){
        if(a.id==b.id&&abs(a.z-b.z)<=max(0.01,a.z*0.02))result=lerp(a.color,b.color,alpha);
        else result=a.z<b.z?a.color:b.color;
    }else if(a.valid)result=a.color;else if(b.valid)result=b.color;
    outputFrame[p]=result;
}
