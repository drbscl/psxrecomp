#ifndef PSX_GPU_GL_ENHANCED_SHADERS_H
#define PSX_GPU_GL_ENHANCED_SHADERS_H
/* Decoding precedes filtering: palette indices are never interpolated. */
#define ACPP_ENHANCED_TEXTURE_GLSL \
"vec4 decoded_bilinear(vec2 uv){\n" \
"  vec2 p=uv-vec2(0.5); ivec2 b=ivec2(floor(p)); vec2 f=fract(p);\n" \
"  vec4 sum=vec4(0.0);\n" \
"  for(int y=0;y<2;y++)for(int x=0;x<2;x++){\n" \
"    int r=fetch_texel(b.x+x,b.y+y); float w=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y);\n" \
"    if(r!=0)sum+=vec4(col5(r),1.0)*w;\n" \
"  } return sum;\n" \
"}\n" \
"vec3 anisotropic_color(vec2 uv){\n" \
"  vec2 dx=dFdx(uv),dy=dFdy(uv);\n" \
"  float a=dot(dx,dx),b=dot(dx,dy),c=dot(dy,dy);\n" \
"  float root=sqrt(max((a-c)*(a-c)+4.0*b*b,0.0));\n" \
"  float major=sqrt(max(0.5*(a+c+root),0.0));\n" \
"  float minor=sqrt(max(0.5*(a+c-root),0.0));\n" \
"  vec2 axis=abs(b)>0.00001?normalize(vec2(b,0.5*(c-a+root))):(a>=c?vec2(1,0):vec2(0,1));\n" \
"  vec2 direction=dx*axis.x+dy*axis.y;\n" \
"  int along=int(clamp(ceil(major/max(minor,1.0)),1.0,16.0));\n" \
"  int across=int(clamp(ceil(minor),1.0,4.0));\n" \
"  vec2 side=major>0.00001?vec2(-direction.y,direction.x)*minor/major:vec2(0);\n" \
"  vec4 sum=vec4(0);\n" \
"  for(int j=0;j<4;j++){if(j>=across)break;for(int i=0;i<16;i++){if(i>=along)break;\n" \
"    vec2 p=uv+direction*((float(i)+0.5)/float(along)-0.5)+side*((float(j)+0.5)/float(across)-0.5);\n" \
"    sum+=decoded_bilinear(p);\n" \
"  }} return sum.a>0.00001?sum.rgb/sum.a:col5(fetch_texel(int(floor(uv.x)),int(floor(uv.y))));\n" \
"}\n"
#endif
