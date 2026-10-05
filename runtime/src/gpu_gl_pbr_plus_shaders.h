#ifndef PSX_GPU_GL_PBR_PLUS_SHADERS_H
#define PSX_GPU_GL_PBR_PLUS_SHADERS_H

/* Shared with the colour pass: identical palette, window and coverage rules. */
#define ACPP_TEXTURE_FETCH_GLSL \
"int vram_at(int x,int y){return int(texelFetch(u_vram,ivec2(x&1023,y&511),0).r);}\n" \
"int fetch_texel(int u,int v){\n" \
" u&=255;v&=255;\n" \
" if((u_twin.x|u_twin.y)!=0){\n" \
"  u=(u&~(u_twin.x*8))|((u_twin.z&u_twin.x)*8);\n" \
"  v=(v&~(u_twin.y*8))|((u_twin.w&u_twin.y)*8);\n" \
" }else{u=clamp(u,v_limits.x,v_limits.z);v=clamp(v,v_limits.y,v_limits.w);}\n" \
" if(v_depth==0){int p=vram_at(v_tpage.x+(u>>2),v_tpage.y+v);\n" \
"  return vram_at(v_clut.x+((p>>((u&3)*4))&15),v_clut.y);}\n" \
" if(v_depth==1){int p=vram_at(v_tpage.x+(u>>1),v_tpage.y+v);\n" \
"  return vram_at(v_clut.x+((p>>((u&1)*8))&255),v_clut.y);}\n" \
" return vram_at(v_tpage.x+u,v_tpage.y+v);}\n"

/* Normals are derivatives of this submitted triangle's interpolated world
 * position, never reconstructed from neighbouring screen pixels. */
#define ACPP_PLUS_SURFACE_GLSL \
"smooth in vec3 v_world; flat in int v_world_valid; uniform int u_opaque;\n" \
"uniform vec3 u_eye;\n" \
"layout(location=0) out vec4 position; layout(location=1) out vec4 normal;\n" \
"void surface(vec3 n){\n" \
" float nn=dot(n,n);position=vec4(0);normal=vec4(0);\n" \
" if(u_opaque==0||v_world_valid==0||nn<1e-12)return;\n" \
" n*=inversesqrt(nn);if(dot(n,u_eye-v_world)<0.0)n=-n;\n" \
" position=vec4(v_world,1);normal=vec4(n,1);}\n"

static const char *PLUS_GEO_FS =
"#version 330\n"
ACPP_PLUS_SURFACE_GLSL
"void main(){surface(cross(dFdx(v_world),dFdy(v_world)));}\n";

static const char *PLUS_TEX_FS =
"#version 330\n"
"noperspective in vec2 v_uv; smooth in vec2 v_uv_p; flat in int v_persp;\n"
"flat in ivec2 v_tpage; flat in ivec2 v_clut; flat in int v_depth;\n"
"flat in ivec4 v_limits;\n"
"uniform ivec4 u_twin; uniform usampler2D u_vram;\n"
ACPP_PLUS_SURFACE_GLSL
ACPP_TEXTURE_FETCH_GLSL
"void main(){vec3 n=cross(dFdx(v_world),dFdy(v_world));vec2 uv=v_persp!=0?v_uv_p:v_uv;\n"
" int raw=fetch_texel(int(floor(uv.x)),int(floor(uv.y)));if(raw==0)discard;\n"
" surface(n);}\n";

/* Submission-time decoded coverage: a compact 256x256 snapshot is shared by
 * page/CLUT/depth and authoritative VRAM generation. Limits/window are applied
 * by the shadow rasterizer, exactly as by the original colour pass. */
static const char *PLUS_ALPHA_FS =
"#version 330\n"
"uniform usampler2D u_vram; uniform ivec4 u_page; out vec4 frag;\n"
"int at(int x,int y){return int(texelFetch(u_vram,ivec2(x&1023,y&511),0).r);}\n"
"void main(){ivec2 uv=ivec2(gl_FragCoord.xy);int raw;\n"
" if(u_page.w==0){int p=at(u_page.x+(uv.x>>2),u_page.y+uv.y);\n"
" raw=at((u_page.z&1023)+((p>>((uv.x&3)*4))&15),u_page.z>>10);}\n"
" else if(u_page.w==1){int p=at(u_page.x+(uv.x>>1),u_page.y+uv.y);\n"
" raw=at((u_page.z&1023)+((p>>((uv.x&1)*8))&255),u_page.z>>10);}\n"
" else raw=at(u_page.x+uv.x,u_page.y+uv.y);\n"
" frag=vec4(raw!=0?1.0:0.0,0,0,1);}\n";

static const char *PLUS_SHADOW_VS =
"#version 330\n"
"layout(location=0) in vec3 a_world; layout(location=1) in vec2 a_uv;\n"
"layout(location=2) in vec4 a_limits; smooth out vec2 v_uv; flat out ivec4 v_limits;\n"
"uniform vec4 u_light_x,u_light_y,u_light_z;\n"
"void main(){vec4 p=vec4(a_world,1);gl_Position=vec4(dot(u_light_x,p),\n"
" dot(u_light_y,p),dot(u_light_z,p),1);v_uv=a_uv;v_limits=ivec4(a_limits);}\n";

static const char *PLUS_SHADOW_FS =
"#version 330\n"
"smooth in vec2 v_uv; flat in ivec4 v_limits; uniform sampler2D u_alpha;\n"
"uniform int u_textured; uniform ivec4 u_twin;\n"
"void main(){if(u_textured==0)return;ivec2 uv=ivec2(floor(v_uv))&ivec2(255);\n"
" if((u_twin.x|u_twin.y)!=0){uv=(uv&~(u_twin.xy*8))|((u_twin.zw&u_twin.xy)*8);}\n"
" else uv=clamp(uv,v_limits.xy,v_limits.zw);\n"
" if(texelFetch(u_alpha,uv,0).r<0.5)discard;}\n";

static const char *PLUS_PRESENT_FS =
"#version 330\n"
"uniform sampler2D u_color,u_scene,u_normal,u_shadow;\n"
"uniform vec3 u_eye,u_light; uniform vec4 u_light_x,u_light_y,u_light_z;\n"
"uniform int u_qualified,u_shadow_valid; uniform ivec2 u_origin; out vec4 frag;\n"
"float visibility(vec3 p,vec3 n,float nl){if(u_shadow_valid==0||nl<=0.0)return 1.0;\n"
" vec4 h=vec4(p,1);vec3 q=vec3(dot(u_light_x,h),dot(u_light_y,h),dot(u_light_z,h))*0.5+0.5;\n"
" if(any(lessThanEqual(q,vec3(0)))||any(greaterThanEqual(q,vec3(1))))return 1.0;\n"
" ivec2 size=textureSize(u_shadow,0);vec2 texel=1.0/vec2(size);float sum=0.0;\n"
/* Transform the receiver plane into shadow coordinates. Every PCF tap compares
 * depth at its actual texel centre, not at the receiver's different position;
 * otherwise even one unoccluded sloping triangle self-shadows by grid phase. */
" vec3 plane=vec3(dot(n,u_light_x.xyz)/dot(u_light_x.xyz,u_light_x.xyz),\n"
" dot(n,u_light_y.xyz)/dot(u_light_y.xyz,u_light_y.xyz),\n"
" dot(n,u_light_z.xyz)/dot(u_light_z.xyz,u_light_z.xyz));\n"
" vec2 slope=-plane.xy/min(plane.z,-1e-6);ivec2 center=ivec2(floor(q.xy*vec2(size)));\n"
" float bias=max(0.00015,0.0007*(1.0-nl));\n"
" for(int y=-1;y<=1;y++)for(int x=-1;x<=1;x++){\n"
" ivec2 tap=clamp(center+ivec2(x,y),ivec2(0),size-ivec2(1));\n"
" vec2 offset=(vec2(tap)+vec2(0.5))*texel-q.xy;\n"
" float receiver=q.z+dot(slope,offset),d=texelFetch(u_shadow,tap,0).r;\n"
" sum+=receiver-bias<=d?1.0:0.0;}\n"
" return sum/9.0;}\n"
"vec3 linearise(vec3 c){return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),\n"
" step(vec3(0.04045),c));}\n"
"vec3 encode(vec3 c){return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-0.055,\n"
" step(vec3(0.0031308),c));}\n"
"void main(){ivec2 ip=ivec2(gl_FragCoord.xy)-u_origin;\n"
" ip.y=textureSize(u_scene,0).y-1-ip.y;vec4 original=texelFetch(u_color,ip,0);frag=original;\n"
" vec4 p=texelFetch(u_scene,ip,0),normal=texelFetch(u_normal,ip,0);\n"
" if(u_qualified==0||p.a<0.5||normal.a<0.5)return;\n"
" vec3 n=normalize(normal.xyz),v=normalize(u_eye-p.xyz),l=normalize(u_light);\n"
" float nv=max(dot(n,v),0.0001),nl=max(dot(n,l),0.0);vec3 base=linearise(original.rgb);\n"
" vec3 f0=vec3(0.04);float roughness=0.52,alpha=roughness*roughness,a2=alpha*alpha;\n"
" vec3 hv=v+l;vec3 h=hv*inversesqrt(max(dot(hv,hv),1e-8));\n"
" float nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);\n"
" vec3 f=f0+(1.0-f0)*pow(1.0-vh,5.0);\n"
" float den=nh*nh*(a2-1.0)+1.0;float d=a2/(3.14159265*den*den);\n"
" float gv=2.0*nv/(nv+sqrt(a2+(1.0-a2)*nv*nv));\n"
" float gl=2.0*nl/max(nl+sqrt(a2+(1.0-a2)*nl*nl),0.0001);\n"
" vec3 brdf=(1.0-f)*base/3.14159265+f*d*gv*gl/max(4.0*nv*nl,0.0001);\n"
" vec3 ambient=(1.0-f0)*base*vec3(0.20,0.24,0.30);\n"
" vec3 direct=brdf*vec3(3.1,2.85,2.5)*nl*visibility(p.xyz,n,nl);\n"
" vec3 radiance=ambient+direct;\n"
/* Analytic shoulder preserves highlight response without bright-value clipping. */
" radiance=radiance/(vec3(1.0)+radiance);frag=vec4(encode(radiance),original.a);}\n";
#endif
