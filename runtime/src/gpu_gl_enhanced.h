/* Included after the present helpers; private host presentation never feeds VRAM. */
#define ENHANCED_SURFACES 8
typedef struct PlusShadowState PlusShadowState;
typedef struct {
    GLuint tex, fbo, rb;
    int x, y, native_w, native_h, offset, width, height, wide;
    GLuint scene_tex, scene_fbo, scene_rb;
    GLuint normal_tex, shadow_tex, shadow_fbo;
    float world_eye[3];
    int world_valid;
    uint32_t world_epoch;
    PlusShadowState *plus_shadow;
} EnhancedSurface;
static EnhancedSurface s_enh[ENHANCED_SURFACES];
static EnhancedSurface *s_enh_active;
static GLuint s_plus_tex_prog, s_plus_geo_prog, s_plus_present_prog, s_plus_camera_vbo;
static void plus_surface_release(EnhancedSurface *s);
static void plus_clear_scene(EnhancedSurface *s, int full);
static void plus_shutdown(void);
static void plus_present(EnhancedSurface *s, int lx, int ly, int lw, int lh);
static GLuint s_enh_stencil_tex, s_enh_stencil_fbo;
static int s_enh_stencil_w, s_enh_stencil_h;
typedef struct {
    GLuint program;
    GLint xoff,xhalf,yprojection,shift,xscale,xcenter,enhanced;
} EnhancedUniforms;
static EnhancedUniforms s_enh_uniforms[4];
static EnhancedUniforms *enhanced_uniforms(GLuint program) {
    int index = program == s_tex_prog ? 1 : program == s_plus_geo_prog ? 2 :
                program == s_plus_tex_prog ? 3 : 0;
    EnhancedUniforms *u=&s_enh_uniforms[index];
    if(u->program!=program){
        u->program=program;
        u->xoff=p_glGetUniformLocation(program,"u_xoff");u->xhalf=p_glGetUniformLocation(program,"u_xhalf");
        u->yprojection=p_glGetUniformLocation(program,"u_yprojection");u->shift=p_glGetUniformLocation(program,"u_shift");
        u->xscale=p_glGetUniformLocation(program,"u_xscale");u->xcenter=p_glGetUniformLocation(program,"u_xcenter");
        u->enhanced=p_glGetUniformLocation(program,"u_enhanced");
    }
    return u;
}
static void enhanced_shutdown(void) {
    plus_shutdown();
    if(s_enh_stencil_fbo)p_glDeleteFramebuffers(1,&s_enh_stencil_fbo);
    if(s_enh_stencil_tex)glDeleteTextures(1,&s_enh_stencil_tex);
    s_enh_stencil_fbo=s_enh_stencil_tex=0;s_enh_stencil_w=s_enh_stencil_h=0;
    memset(s_enh_uniforms,0,sizeof(s_enh_uniforms));
}
static void enhanced_rebuild_stencil(EnhancedSurface *s) {
        if(!s->fbo)return;
        if(s_enh_stencil_w!=s->width||s_enh_stencil_h!=s->height) {
            if(s_enh_stencil_fbo)p_glDeleteFramebuffers(1,&s_enh_stencil_fbo);
            if(s_enh_stencil_tex)glDeleteTextures(1,&s_enh_stencil_tex);
            s_enh_stencil_tex=make_tex(GL_RGBA8,s->width,s->height,GL_RGBA,GL_UNSIGNED_BYTE);
            if(!make_fbo(&s_enh_stencil_fbo,s_enh_stencil_tex,0))abort();
            s_enh_stencil_w=s->width;s_enh_stencil_h=s->height;
        }
        glDisable(GL_SCISSOR_TEST);
        p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,s->fbo);
        p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER,s_enh_stencil_fbo);
        p_glBlitFramebuffer(0,0,s->width,s->height,0,0,s->width,s->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s->fbo);glViewport(0,0,s->width,s->height);
        glDisable(GL_BLEND);glEnable(GL_STENCIL_TEST);glStencilMask(1);glClearStencil(0);glClear(GL_STENCIL_BUFFER_BIT);
        glStencilFunc(GL_ALWAYS,1,1);glStencilOp(GL_KEEP,GL_KEEP,GL_REPLACE);glColorMask(GL_FALSE,GL_FALSE,GL_FALSE,GL_FALSE);
        p_glUseProgram(s_stencil_prog);p_glActiveTexture(PSXGL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,s_enh_stencil_tex);
        p_glUniform1i(s_uStencilSrc,0);p_glBindVertexArray(s_empty_vao);glDrawArrays(GL_TRIANGLES,0,3);
        glColorMask(GL_TRUE,GL_TRUE,GL_TRUE,GL_TRUE);hr_end();
}
static void enhanced_rebuild_stencils(void) {
    for(int i=0;i<ENHANCED_SURFACES;i++) enhanced_rebuild_stencil(&s_enh[i]);
}

static void enhanced_reset(void) {
    if (!s_ctx) { memset(s_enh, 0, sizeof(s_enh)); s_enh_active=NULL; return; }
    for(int i=0;i<ENHANCED_SURFACES;i++) {
        plus_surface_release(&s_enh[i]);
        if(s_enh[i].fbo)p_glDeleteFramebuffers(1,&s_enh[i].fbo);
        if(s_enh[i].tex)glDeleteTextures(1,&s_enh[i].tex);
        if(s_enh[i].rb)p_glDeleteRenderbuffers(1,&s_enh[i].rb);
    }
    memset(s_enh,0,sizeof(s_enh)); s_enh_active=NULL;
}
void gl_renderer_set_graphics_mode(int mode) {
    if((mode!=GL_GRAPHICS_ORIGINAL&&mode!=GL_GRAPHICS_ACCURATE&&
        mode!=GL_GRAPHICS_PBR_PLUS)||mode==s_graphics_mode)return;
    if(s_raster_ok){flush_flat_batch();flush_tex_batch();flush_cpu_upload();}
    enhanced_reset();
    if(s_ctx)enhanced_shutdown();
    s_graphics_mode=mode;
    /* Enhanced presents bypass interp_capture(), so any history captured under
     * Original stays valid and the presenter thread would keep drawing and
     * swapping the same drawable concurrently. Drop it on entry. */
    if(mode!=GL_GRAPHICS_ORIGINAL)interp_reset_history();
    /* Enhanced modes present on the main context; off-thread presentation is
     * not used, as cross-context history sampling is unreliable on Metal. */
    if(s_raster_ok)gl_renderer_invalidate_present();
}
int gl_renderer_get_graphics_mode(void){return s_graphics_mode;}
void gl_renderer_graphics_diag(int *mode,int *width,int *height){
    if(mode)*mode=s_graphics_mode;
    if(width)*width=s_enh_active?s_enh_active->width:0;
    if(height)*height=s_enh_active?s_enh_active->height:0;
}
static int enhanced_overlap(int a,int n,int b,int m){return a<b+m&&b<a+n;}
/* First covered host pixel: ceil(projected edge - 0.5), matching raster
 * pixel centers even when the drawable/native ratio is fractional. */
static int enhanced_pixel_edge(int coordinate,int pixels,int native) {
    return (int)(((int64_t)2*coordinate*pixels+native-1)/((int64_t)2*native));
}
static void enhanced_invalidate_rect(int x,int y,int w,int h){
    if(!s_graphics_mode)return;
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);
    GLint old_scissor[4];glGetIntegerv(GL_SCISSOR_BOX,old_scissor);
    /* Apply exact intersecting authoritative mutations, keeping all unrelated
     * enhanced pixels (and the other guest buffer) intact. */
    for(int i=0;i<ENHANCED_SURFACES;i++){
        EnhancedSurface *s=&s_enh[i];
        if(s->fbo&&enhanced_overlap(x,w,s->x,s->native_w-2*s->offset)&&enhanced_overlap(y,h,s->y,s->native_h)){
            int x0=x>s->x?x:s->x,y0=y>s->y?y:s->y;
            int x1=x+w<s->x+s->native_w-2*s->offset?x+w:s->x+s->native_w-2*s->offset;
            int y1=y+h<s->y+s->native_h?y+h:s->y+s->native_h;
            int dx0=enhanced_pixel_edge(x0-s->x+s->offset,s->width,s->native_w);
            int dx1=enhanced_pixel_edge(x1-s->x+s->offset,s->width,s->native_w);
            int dy0=enhanced_pixel_edge(y0-s->y,s->height,s->native_h);
            int dy1=enhanced_pixel_edge(y1-s->y,s->height,s->native_h);
            glEnable(GL_SCISSOR_TEST);glScissor(dx0,dy0,dx1-dx0,dy1-dy0);
            p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,s_hr_fbo);p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER,s->fbo);
            /* Keep the full projection's sampling phase; only coverage is
             * clipped. Rounding and rescaling each source subrect shifts rows. */
            p_glBlitFramebuffer((s->x-s->offset)*s_scale,s->y*s_scale,
                (s->x-s->offset+s->native_w)*s_scale,(s->y+s->native_h)*s_scale,
                0,0,s->width,s->height,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            plus_clear_scene(s, x0 == s->x && x1 == s->x + s->native_w - 2*s->offset &&
                                y0 == s->y && y1 == s->y + s->native_h);
            if(s_mask_check)enhanced_rebuild_stencil(s);
            else s_stencil_valid=0;
        }
    }
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_hr_fbo);
    glScissor(old_scissor[0],old_scissor[1],old_scissor[2],old_scissor[3]);
    if(scissor)glEnable(GL_SCISSOR_TEST);else glDisable(GL_SCISSOR_TEST);
}
static EnhancedSurface *enhanced_surface(int x,int y,int nw,int nh,int offset,int wide,int force43){
    int ww=0,wh=0,lx,ly,pw,ph;
    if(!s_graphics_mode||!s_win||nw<=0||nh<=0)return NULL;
    SDL_GL_GetDrawableSize(s_win,&ww,&wh);if(ww<=0||wh<=0)return NULL;
    if(force43)letterbox_rect_aspect(ww,wh,4,3,&lx,&ly,&pw,&ph);
    else letterbox_rect(ww,wh,&lx,&ly,&pw,&ph);
    if(pw>s_max_texture||ph>s_max_texture||pw>s_max_viewport[0]||ph>s_max_viewport[1]){
        fprintf(stderr,"psxrecomp: drawable exceeds enhanced GL target capacity: %dx%d\n",pw,ph);abort();
    }
    EnhancedSurface *slot=NULL;
    for(int i=0;i<ENHANCED_SURFACES;i++){
        EnhancedSurface *s=&s_enh[i];
        if(s->fbo&&s->x==x&&s->y==y&&s->wide==wide){slot=s;break;}
        if(!s->fbo&&!slot)slot=s;
    }
    if(!slot){fprintf(stderr,"psxrecomp: enhanced framebuffer target capacity exhausted\n");abort();}
    if(slot->fbo&&slot->native_w==nw&&slot->native_h==nh&&slot->offset==offset&&slot->width==pw&&slot->height==ph)return slot;
    EnhancedSurface old=*slot; memset(slot,0,sizeof(*slot));
    slot->x=x;slot->y=y;slot->native_w=nw;slot->native_h=nh;slot->offset=offset;slot->width=pw;slot->height=ph;slot->wide=wide;
    slot->tex=make_tex(GL_RGBA8,pw,ph,GL_RGBA,GL_UNSIGNED_BYTE);
    p_glGenRenderbuffers(1,&slot->rb);p_glBindRenderbuffer(PSXGL_RENDERBUFFER,slot->rb);
    p_glRenderbufferStorage(PSXGL_RENDERBUFFER,PSXGL_DEPTH24_STENCIL8,pw,ph);
    if(!make_fbo(&slot->fbo,slot->tex,slot->rb)){fprintf(stderr,"psxrecomp: enhanced target allocation failed\n");abort();}
    glDisable(GL_SCISSOR_TEST);p_glBindFramebuffer(PSXGL_FRAMEBUFFER,slot->fbo);
    glClearColor(0,0,0,0);glClearStencil(0);glStencilMask(0xff);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
    if(s_graphics_mode==GL_GRAPHICS_PBR_PLUS){
        slot->scene_tex=make_tex(GL_RGBA32F,pw,ph,GL_RGBA,GL_FLOAT);
        slot->normal_tex=make_tex(GL_RGBA32F,pw,ph,GL_RGBA,GL_FLOAT);
        p_glGenRenderbuffers(1,&slot->scene_rb);
        p_glBindRenderbuffer(PSXGL_RENDERBUFFER,slot->scene_rb);
        p_glRenderbufferStorage(PSXGL_RENDERBUFFER,PSXGL_DEPTH24_STENCIL8,pw,ph);
        if(!make_fbo(&slot->scene_fbo,slot->scene_tex,slot->scene_rb)){
            fprintf(stderr,"psxrecomp: PBR+ geometry target allocation failed\n");abort();
        }
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,slot->scene_fbo);
        p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER,PSXGL_COLOR_ATTACHMENT0+1,GL_TEXTURE_2D,slot->normal_tex,0);
        const GLenum targets[2]={PSXGL_COLOR_ATTACHMENT0,PSXGL_COLOR_ATTACHMENT0+1};
        p_glDrawBuffers(2,targets);
        if(p_glCheckFramebufferStatus(PSXGL_FRAMEBUFFER)!=PSXGL_FRAMEBUFFER_COMPLETE){
            fprintf(stderr,"psxrecomp: PBR+ world target allocation failed\n");abort();
        }
        glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
    }
    if(wide && !old.fbo){
        for(int i=0;i<WIDE_MAX_SURF;i++)if(s_wide_fbo[i]&&s_wide_base[i]==x){
            p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,s_wide_fbo[i]);
            p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER,slot->fbo);
            p_glBlitFramebuffer(0,y*s_scale,nw*s_scale,(y+nh)*s_scale,0,0,pw,ph,GL_COLOR_BUFFER_BIT,GL_NEAREST);
            break;
        }
    }
    p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,s_hr_fbo);p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER,slot->fbo);
    int center=nw-2*offset,dx=enhanced_pixel_edge(offset,pw,nw),dx1=enhanced_pixel_edge(offset+center,pw,nw);
    glEnable(GL_SCISSOR_TEST);glScissor(dx,0,dx1-dx,ph);
    p_glBlitFramebuffer((x-offset)*s_scale,y*s_scale,(x-offset+nw)*s_scale,(y+nh)*s_scale,0,0,pw,ph,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    glDisable(GL_SCISSOR_TEST);
    if(old.fbo){
        /* A resized drawable keeps both private bands alive. The transition
         * image is resampled once; subsequent triangles rasterize 1:1. */
        p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,old.fbo);
        p_glBlitFramebuffer(0,0,old.width,old.height,0,0,pw,ph,GL_COLOR_BUFFER_BIT,GL_NEAREST);
        p_glDeleteFramebuffers(1,&old.fbo);glDeleteTextures(1,&old.tex);p_glDeleteRenderbuffers(1,&old.rb);
        plus_surface_release(&old);
    }
    if(s_mask_check)enhanced_rebuild_stencil(slot);
    else s_stencil_valid=0;
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,0);return slot;
}
static void enhanced_prepare(void){
    s_enh_active=NULL;if(!s_graphics_mode||gpu_display_is_depth24())return;
    GpuDisplayInfo di;gpu_get_display_info(&di);
    int nw=(int)di.width,nh=(int)di.height;
    int wide=g_wide_cur!=0,offset=wide?g_wide_off:0;
    /* E5 is geometry translation, not framebuffer identity. The original
     * compositor identifies buffers from E3/E4 draw areas. */
    int x=wide?g_wide_cur_base:s_area_x1,y=s_area_y1;
    for(int i=0;i<ENHANCED_SURFACES;i++){
        EnhancedSurface *s=&s_enh[i];
        if(s->fbo&&s->wide==wide&&s->native_h==nh&&s->native_w==(wide?g_wide_w:nw)&&
           s_area_x1>=s->x&&s_area_x2<s->x+nw&&s_area_y1>=s->y&&s_area_y2<s->y+nh){
            x=s->x;y=s->y;break;
        }
    }
    if(nw<=0||nh<=0||x<0||y<0||x+nw>VRAM_W||y+nh>VRAM_H)return;
    s_enh_active=enhanced_surface(x,y,wide?g_wide_w:nw,nh,offset,wide,gpu_ws_present_native_43());
}
static int enhanced_begin(GLuint program){
    EnhancedSurface *s=s_enh_active;if(!s_graphics_mode||!s)return 0;
    EnhancedUniforms *u=enhanced_uniforms(program);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s->fbo);glViewport(0,0,s->width,s->height);
    /* Native-wide deliberately admits reveal margins horizontally, matching
     * the existing wide mirror. Vertical guest clipping remains unchanged. */
    int gx0=s->wide?s->x-s->offset:s_area_x1;
    int gx1=s->wide?s->x+s->native_w-s->offset:s_area_x2+1;
    int gy0=s_area_y1>s->y?s_area_y1:s->y;
    int gy1=s_area_y2+1<s->y+s->native_h?s_area_y2+1:s->y+s->native_h;
    int sx0=(int)floor((double)(gx0-s->x+s->offset)*s->width/s->native_w);
    int sx1=(int)ceil((double)(gx1-s->x+s->offset)*s->width/s->native_w);
    int sy0=(int)floor((double)(gy0-s->y)*s->height/s->native_h);
    int sy1=(int)ceil((double)(gy1-s->y)*s->height/s->native_h);
    if(sx0<0)sx0=0;if(sx1>s->width)sx1=s->width;
    if(sy0<0)sy0=0;if(sy1>s->height)sy1=s->height;
    glEnable(GL_SCISSOR_TEST);glScissor(sx0,sy0,sx1>sx0?sx1-sx0:0,sy1>sy0?sy1-sy0:0);
    p_glUniform1f(u->xoff,(float)(s->offset-s->x));
    p_glUniform1f(u->xhalf,(float)s->native_w*0.5f);
    p_glUniform2f(u->yprojection,-(float)s->y,(float)s->native_h*0.5f);
    p_glUniform1f(u->shift,0.0f);
    if(program==s_tex_prog){
        p_glUniform1i(u->enhanced,1);
    }
    p_glUniform1f(u->xscale,1.0f);p_glUniform1f(u->xcenter,0.0f);
    if(s->wide){
        if(program==s_tex_prog||program==s_plus_tex_prog)wide_set_bd_scale(u->xscale,u->xcenter);
        else if(s_wide_suppress){
            p_glUniform1f(u->xscale,(float)s->native_w/(s->native_w-2*s->offset));
            p_glUniform1f(u->xcenter,(float)s->x+(s->native_w-2*s->offset)*0.5f);
        }else wide_set_bd_scale(u->xscale,u->xcenter);
    }
    return 1;
}
static void enhanced_end(GLuint program){
    EnhancedUniforms *u=enhanced_uniforms(program);
    p_glUniform1f(u->xoff,0);p_glUniform1f(u->xhalf,512);
    p_glUniform2f(u->yprojection,0,0);
    p_glUniform1f(u->shift,0.5f/s_scale-1.0f/64.0f);
    p_glUniform1f(u->xscale,1);p_glUniform1f(u->xcenter,0);
    if(program==s_tex_prog)p_glUniform1i(u->enhanced,0);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s_hr_fbo);
}
#include "gpu_gl_pbr_plus.h"

static void enhanced_wide_clear(int base,int y,int h,uint16_t color,int sides) {
    if(!s_graphics_mode)return;
    for(int i=0;i<ENHANCED_SURFACES;i++){
        EnhancedSurface *s=&s_enh[i];if(!s->fbo||!s->wide||s->x!=base)continue;
        int y0=y>s->y?y:s->y,y1=y+h<s->y+s->native_h?y+h:s->y+s->native_h;if(y1<=y0)continue;
        int dy0=enhanced_pixel_edge(y0-s->y,s->height,s->native_h),dy1=enhanced_pixel_edge(y1-s->y,s->height,s->native_h);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER,s->fbo);glEnable(GL_SCISSOR_TEST);
        glClearColor((color&31)/31.0f,((color>>5)&31)/31.0f,((color>>10)&31)/31.0f,(color>>15)&1);
        glClearStencil((color>>15)&1);glStencilMask(0xff);
        int left=enhanced_pixel_edge(s->offset,s->width,s->native_w);
        int right=enhanced_pixel_edge(s->native_w-s->offset,s->width,s->native_w);
        if(!sides){glScissor(0,dy0,s->width,dy1-dy0);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);
            plus_clear_scene(s,y0==s->y&&y1==s->y+s->native_h);}
        else {
            if(sides&1){glScissor(0,dy0,left,dy1-dy0);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);plus_clear_scene(s,0);}
            if(sides&2){glScissor(right,dy0,s->width-right,dy1-dy0);glClear(GL_COLOR_BUFFER_BIT|GL_STENCIL_BUFFER_BIT);plus_clear_scene(s,0);}
        }
    }
    glDisable(GL_SCISSOR_TEST);p_glBindFramebuffer(PSXGL_FRAMEBUFFER,0);
}
static int enhanced_present(int x,int y,int w,int h,int wide,int force43){
    if(!s_graphics_mode)return 0;
    EnhancedSurface *s=enhanced_surface(x,y,w,h,wide?g_wide_off:0,wide,force43);if(!s)return 0;
    int ww,wh,lx,ly,lw,lh;SDL_GL_GetDrawableSize(s_win,&ww,&wh);
    if(force43)letterbox_rect_aspect(ww,wh,4,3,&lx,&ly,&lw,&lh);else letterbox_rect(ww,wh,&lx,&ly,&lw,&lh);
    glDisable(GL_SCISSOR_TEST);glDisable(GL_STENCIL_TEST);glDisable(GL_BLEND);
    p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER,0);glViewport(0,0,ww,wh);glClearColor(0,0,0,1);glClear(GL_COLOR_BUFFER_BIT);
    present_bezel(ww,wh,lx,ly,lw,lh);
    /* Exact equal-size blit, including vertical orientation. No ceil scale,
     * reconstruction quad, half-texel inset or output downsampling. */
    if(s_graphics_mode==GL_GRAPHICS_PBR_PLUS)plus_present(s,lx,ly,lw,lh);
    else {
        p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER,s->fbo);p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER,0);
        p_glBlitFramebuffer(0,0,s->width,s->height,lx,ly+lh,lx+lw,ly,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    }
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER,0);
    s_enh_active=s;pres_record(wide?GL_PRES_WIDE:GL_PRES_VRAM,x,y,w,h,lx,ly,lw,lh);
    hold_capture_drawable();latency_ring_mark(LAT_SWAP_BEGIN);gl_swap_with_osd();latency_ring_mark(LAT_SWAP_END);s_probe_swap++;
    present_force_consumed();return 1;
}
