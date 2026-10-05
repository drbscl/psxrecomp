/* Included by gpu_gl_enhanced.h after its projection helpers. */
#define PLUS_SHADOW_SIZE 2048
#define PLUS_CAMERA_STRIDE 9

typedef struct {
    float world[3], uv[2], limits[4];
} PlusShadowVertex;
typedef struct {
    int first, count, alpha, twin[4];
} PlusShadowDraw;
typedef struct {
    GLuint tex;
    uint64_t epoch;
    int page[5]; /* page X/Y, CLUT X/Y, texture depth */
} PlusAlphaSnapshot;
struct PlusShadowState {
    PlusShadowVertex *vertices;
    PlusShadowDraw *draws;
    PlusAlphaSnapshot *alpha;
    size_t vertex_count, vertex_capacity, draw_count, draw_capacity;
    size_t alpha_count, alpha_capacity;
    float rows[3][4];
    int dirty, ready;
};
static GLuint s_plus_shadow_prog, s_plus_alpha_prog, s_plus_shadow_vao, s_plus_shadow_vbo;
static GLuint s_plus_alpha_fbo;
static GLint s_plus_opaque[2], s_plus_eye[2], s_plus_camera_pass, s_plus_twin;
static GLint s_plus_present_eye, s_plus_present_origin, s_plus_present_qualified;
static GLint s_plus_shadow_rows[3], s_plus_present_rows[3];
static GLint s_plus_shadow_textured, s_plus_shadow_twin, s_plus_present_shadow_valid;
static GLint s_plus_alpha_page;
/* Artistic directional sun, constant in the validated title's world axes. */
static float s_plus_light[3], s_plus_light_basis[3][3];

static void *plus_reserve(void *p, size_t *capacity, size_t count, size_t size) {
    if (count <= *capacity) return p;
    size_t next = *capacity ? *capacity : 32;
    while (next < count) next *= 2;
    void *result = realloc(p, next * size);
    if (!result) { fprintf(stderr, "psxrecomp: PBR+ shadow allocation failed\n"); abort(); }
    *capacity = next;
    return result;
}
static void plus_init(void) {
    if (s_plus_present_prog) return;
    s_plus_geo_prog = build_program(GEO_VS, PLUS_GEO_FS);
    s_plus_tex_prog = build_program(TEX_VS, PLUS_TEX_FS);
    s_plus_present_prog = build_program(PACK_VS, PLUS_PRESENT_FS);
    s_plus_shadow_prog = build_program(PLUS_SHADOW_VS, PLUS_SHADOW_FS);
    s_plus_alpha_prog = build_program(PACK_VS, PLUS_ALPHA_FS);
    if (!s_plus_geo_prog || !s_plus_tex_prog || !s_plus_present_prog ||
        !s_plus_shadow_prog || !s_plus_alpha_prog) {
        fprintf(stderr, "psxrecomp: PBR+ shader initialization failed\n"); abort();
    }
    p_glGenBuffers(1, &s_plus_camera_vbo);
    p_glGenBuffers(1, &s_plus_shadow_vbo);
    p_glGenVertexArrays(1, &s_plus_shadow_vao);
    p_glGenFramebuffers(1, &s_plus_alpha_fbo);
    p_glBindVertexArray(s_plus_shadow_vao);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_plus_shadow_vbo);
    p_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PlusShadowVertex), (void *)0);
    p_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(PlusShadowVertex), (void *)(3*sizeof(float)));
    p_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(PlusShadowVertex), (void *)(5*sizeof(float)));
    for (int i = 0; i < 3; ++i) p_glEnableVertexAttribArray((GLuint)i);
    p_glBindVertexArray(0);
    s_plus_light[0] = -0.35f; s_plus_light[1] = -0.8f; s_plus_light[2] = 0.45f;
    float len = sqrtf(0.35f*0.35f + 0.8f*0.8f + 0.45f*0.45f);
    for (int i = 0; i < 3; ++i) s_plus_light[i] /= len;
    len = sqrtf(s_plus_light[0]*s_plus_light[0] + s_plus_light[2]*s_plus_light[2]);
    float *x = s_plus_light_basis[0], *y = s_plus_light_basis[1], *z = s_plus_light_basis[2];
    x[0] = s_plus_light[2]/len; x[1] = 0; x[2] = -s_plus_light[0]/len;
    for (int i = 0; i < 3; ++i) z[i] = -s_plus_light[i];
    y[0] = z[1]*x[2]-z[2]*x[1]; y[1] = z[2]*x[0]-z[0]*x[2]; y[2] = z[0]*x[1]-z[1]*x[0];
    s_plus_opaque[0] = p_glGetUniformLocation(s_plus_geo_prog, "u_opaque");
    s_plus_opaque[1] = p_glGetUniformLocation(s_plus_tex_prog, "u_opaque");
    s_plus_eye[0] = p_glGetUniformLocation(s_plus_geo_prog, "u_eye");
    s_plus_eye[1] = p_glGetUniformLocation(s_plus_tex_prog, "u_eye");
    s_plus_camera_pass = p_glGetUniformLocation(s_plus_geo_prog, "u_camera_pass");
    s_plus_twin = p_glGetUniformLocation(s_plus_tex_prog, "u_twin");
    p_glUseProgram(s_plus_tex_prog);
    p_glUniform1i(p_glGetUniformLocation(s_plus_tex_prog, "u_vram"), 0);
    p_glUseProgram(s_plus_alpha_prog);
    p_glUniform1i(p_glGetUniformLocation(s_plus_alpha_prog, "u_vram"), 0);
    s_plus_alpha_page = p_glGetUniformLocation(s_plus_alpha_prog, "u_page");
    p_glUseProgram(s_plus_shadow_prog);
    p_glUniform1i(p_glGetUniformLocation(s_plus_shadow_prog, "u_alpha"), 0);
    s_plus_shadow_textured = p_glGetUniformLocation(s_plus_shadow_prog, "u_textured");
    s_plus_shadow_twin = p_glGetUniformLocation(s_plus_shadow_prog, "u_twin");
    p_glUseProgram(s_plus_present_prog);
    const char *samplers[] = {"u_color", "u_scene", "u_normal", "u_shadow"};
    for (int i = 0; i < 4; ++i) p_glUniform1i(p_glGetUniformLocation(s_plus_present_prog, samplers[i]), i);
    p_glUniform3f(p_glGetUniformLocation(s_plus_present_prog, "u_light"), s_plus_light[0], s_plus_light[1], s_plus_light[2]);
    s_plus_present_eye = p_glGetUniformLocation(s_plus_present_prog, "u_eye");
    s_plus_present_origin = p_glGetUniformLocation(s_plus_present_prog, "u_origin");
    s_plus_present_qualified = p_glGetUniformLocation(s_plus_present_prog, "u_qualified");
    s_plus_present_shadow_valid = p_glGetUniformLocation(s_plus_present_prog, "u_shadow_valid");
    const char *rows[] = {"u_light_x", "u_light_y", "u_light_z"};
    for (int i = 0; i < 3; ++i) {
        s_plus_shadow_rows[i] = p_glGetUniformLocation(s_plus_shadow_prog, rows[i]);
        s_plus_present_rows[i] = p_glGetUniformLocation(s_plus_present_prog, rows[i]);
    }
}
static void plus_surface_release(EnhancedSurface *s) {
    if (s->scene_fbo) p_glDeleteFramebuffers(1, &s->scene_fbo);
    if (s->scene_tex) glDeleteTextures(1, &s->scene_tex);
    if (s->normal_tex) glDeleteTextures(1, &s->normal_tex);
    if (s->scene_rb) p_glDeleteRenderbuffers(1, &s->scene_rb);
    if (s->shadow_fbo) p_glDeleteFramebuffers(1, &s->shadow_fbo);
    if (s->shadow_tex) glDeleteTextures(1, &s->shadow_tex);
    if (s->plus_shadow) {
        PlusShadowState *q = s->plus_shadow;
        for (size_t i = 0; i < q->alpha_capacity; ++i)
            if (q->alpha[i].tex) glDeleteTextures(1, &q->alpha[i].tex);
        free(q->vertices); free(q->draws); free(q->alpha); free(q);
    }
    s->plus_shadow = NULL;
    s->scene_fbo = s->scene_tex = s->scene_rb = s->normal_tex = 0;
    s->shadow_fbo = s->shadow_tex = 0;
    s->world_valid = 0;
}
static void plus_clear_scene(EnhancedSurface *s, int full) {
    if (!s->scene_fbo) return;
    GLint draw; GLfloat color[4];
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &draw);
    glGetFloatv(GL_COLOR_CLEAR_VALUE, color);
    p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER, s->scene_fbo);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT); /* Every MRT, same exact authoritative scissor. */
    p_glBindFramebuffer(PSXGL_DRAW_FRAMEBUFFER, (GLuint)draw);
    glClearColor(color[0], color[1], color[2], color[3]);
    if (full) {
        s->world_valid = 0;
        if (s->plus_shadow) {
            PlusShadowState *q = s->plus_shadow;
            q->vertex_count = q->draw_count = q->alpha_count = 0;
            q->ready = 0; q->dirty = 1;
        }
    }
}
static void plus_shutdown(void) {
    if (s_plus_camera_vbo) {
        const GLuint vaos[2] = {s_geo_vao, s_tex_vao};
        const GLuint buffers[2] = {s_geo_vbo, s_tex_vbo};
        for (int i = 0; i < 2; ++i) {
            GLuint location = i ? 10 : 2;
            p_glBindVertexArray(vaos[i]); p_glBindBuffer(PSXGL_ARRAY_BUFFER, buffers[i]);
            for (GLuint j = 0; j < 3; ++j) {
                p_glDisableVertexAttribArray(location + j);
                p_glVertexAttribPointer(location + j, j == 1 ? 1 : 4, GL_FLOAT, GL_FALSE, 0, (void *)0);
            }
        }
        p_glDeleteBuffers(1, &s_plus_camera_vbo); p_glBindVertexArray(0);
    }
    if (s_plus_shadow_vbo) p_glDeleteBuffers(1, &s_plus_shadow_vbo);
    if (s_plus_shadow_vao) p_glDeleteVertexArrays(1, &s_plus_shadow_vao);
    if (s_plus_alpha_fbo) p_glDeleteFramebuffers(1, &s_plus_alpha_fbo);
    GLuint programs[] = {s_plus_geo_prog, s_plus_tex_prog, s_plus_present_prog, s_plus_shadow_prog, s_plus_alpha_prog};
    for (int i = 0; i < 5; ++i) if (programs[i]) p_glDeleteProgram(programs[i]);
    s_plus_camera_vbo = s_plus_geo_prog = s_plus_tex_prog = s_plus_present_prog = 0;
    s_plus_shadow_vbo = s_plus_shadow_vao = s_plus_alpha_fbo = s_plus_shadow_prog = s_plus_alpha_prog = 0;
}
static void plus_upload_camera(int textured, int nverts, const float *camera) {
    if (s_graphics_mode != GL_GRAPHICS_PBR_PLUS) return;
    plus_init();
    /* Initialization creates a private VAO; explicitly restore the caller's. */
    p_glBindVertexArray(textured ? s_tex_vao : s_geo_vao);
    p_glUseProgram(textured ? s_tex_prog : s_geo_prog);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_plus_camera_vbo);
    p_glBufferData(PSXGL_ARRAY_BUFFER, (ptrdiff_t)nverts * PLUS_CAMERA_STRIDE * sizeof(float), camera, PSXGL_STREAM_DRAW);
    GLuint location = textured ? 10 : 2;
    p_glVertexAttribPointer(location, 4, GL_FLOAT, GL_FALSE, PLUS_CAMERA_STRIDE*sizeof(float), (void *)0);
    p_glVertexAttribPointer(location + 1, 1, GL_FLOAT, GL_FALSE, PLUS_CAMERA_STRIDE*sizeof(float), (void *)(4*sizeof(float)));
    p_glVertexAttribPointer(location + 2, 4, GL_FLOAT, GL_FALSE, PLUS_CAMERA_STRIDE*sizeof(float), (void *)(5*sizeof(float)));
    for (GLuint i = 0; i < 3; ++i) p_glEnableVertexAttribArray(location + i);
    p_glBindBuffer(PSXGL_ARRAY_BUFFER, textured ? s_tex_vbo : s_geo_vbo);
}
static int plus_alpha_snapshot(PlusShadowState *q, const float *v) {
    int page[5] = {(int)v[8], (int)v[9], (int)v[10], (int)v[11], (int)v[12]};
    for (size_t i = 0; i < q->alpha_count; ++i)
        if (q->alpha[i].epoch == s_plus_vram_epoch && !memcmp(q->alpha[i].page, page, sizeof(page))) return (int)i;
    size_t old_capacity = q->alpha_capacity;
    q->alpha = plus_reserve(q->alpha, &q->alpha_capacity, q->alpha_count + 1, sizeof(*q->alpha));
    if (q->alpha_capacity != old_capacity)
        memset(q->alpha + old_capacity, 0, (q->alpha_capacity-old_capacity)*sizeof(*q->alpha));
    int index = (int)q->alpha_count++;
    PlusAlphaSnapshot *a = &q->alpha[index];
    if (!a->tex) a->tex = make_tex(GL_R8, 256, 256, GL_RED, GL_UNSIGNED_BYTE);
    a->epoch = s_plus_vram_epoch; memcpy(a->page, page, sizeof(page));
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s_plus_alpha_fbo);
    p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER, PSXGL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, a->tex, 0);
    if (p_glCheckFramebufferStatus(PSXGL_FRAMEBUFFER) != PSXGL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "psxrecomp: PBR+ alpha target incomplete\n"); abort();
    }
    glViewport(0, 0, 256, 256); glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    p_glUseProgram(s_plus_alpha_prog); p_glActiveTexture(PSXGL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, s_raw_tex);
    p_glUniform4i(s_plus_alpha_page, page[0], page[1], page[2] | (page[3]<<10), page[4]);
    p_glBindVertexArray(s_empty_vao); glDrawArrays(GL_TRIANGLES, 0, 3);
    return index;
}
static void plus_queue_geometry(EnhancedSurface *s, int textured, int nverts, const float *camera) {
    if (!s->plus_shadow) {
        s->plus_shadow = calloc(1, sizeof(*s->plus_shadow));
        if (!s->plus_shadow) { fprintf(stderr, "psxrecomp: PBR+ scene allocation failed\n"); abort(); }
    }
    PlusShadowState *q = s->plus_shadow;
    for (int first = 0; first + 2 < nverts; first += 3) {
        int valid = 1;
        for (int i = 0; i < 3; ++i) {
            const float *c = camera + (first+i)*PLUS_CAMERA_STRIDE;
            if (c[8] <= 0 || !isfinite(c[5]) || !isfinite(c[6]) || !isfinite(c[7])) valid = 0;
        }
        if (!valid) continue;
        int alpha = textured ? plus_alpha_snapshot(q, s_tb + first*TEXV) : -1;
        q->vertices = plus_reserve(q->vertices, &q->vertex_capacity, q->vertex_count + 3, sizeof(*q->vertices));
        q->draws = plus_reserve(q->draws, &q->draw_capacity, q->draw_count + 1, sizeof(*q->draws));
        PlusShadowDraw *d = q->draw_count ? &q->draws[q->draw_count-1] : NULL;
        if (!d || d->alpha != alpha || (textured && memcmp(d->twin, s_tb_twin, sizeof(d->twin)))) {
            d = &q->draws[q->draw_count++]; d->first = (int)q->vertex_count; d->count = 0; d->alpha = alpha;
            if (textured) memcpy(d->twin, s_tb_twin, sizeof(d->twin)); else memset(d->twin, 0, sizeof(d->twin));
        }
        for (int i = 0; i < 3; ++i) {
            PlusShadowVertex *v = &q->vertices[q->vertex_count++];
            memcpy(v->world, camera + (first+i)*PLUS_CAMERA_STRIDE + 5, sizeof(v->world));
            if (textured) {
                const float *src = s_tb + (first+i)*TEXV;
                memcpy(v->uv, src+2, sizeof(v->uv)); memcpy(v->limits, src+14, sizeof(v->limits));
            } else { memset(v->uv, 0, sizeof(v->uv)); memset(v->limits, 0, sizeof(v->limits)); }
        }
        d->count += 3; q->dirty = 1;
    }
}
static void plus_geometry_draw(int textured, GLenum mode, int nverts, int semi,
                               int write_mask, const float *camera) {
    EnhancedSurface *s = s_enh_active;
    if (s_graphics_mode != GL_GRAPHICS_PBR_PLUS || !s || !s->scene_fbo) return;
    static const float empty_camera[6 * PLUS_CAMERA_STRIDE];
    if (camera && semi < 0 && mode == GL_TRIANGLES) {
        for (int i = 0; i < nverts; ++i) if (camera[i*PLUS_CAMERA_STRIDE+8] > 0) {
            if (!s->world_valid) {
                s->world_epoch = s_world_epoch;
                memcpy(s->world_eye, s_world_eye, sizeof(s->world_eye));
                s->world_valid = 1;
            } else if (s->world_epoch != s_world_epoch) s->world_valid = 2;
            break;
        }
    }
    if (!camera) plus_upload_camera(textured, nverts, empty_camera);
    if (camera && semi < 0 && s->world_valid == 1 && mode == GL_TRIANGLES)
        plus_queue_geometry(s, textured, nverts, camera);
    /* Alpha capture uses private full-screen draws; restore original vertex
     * layout and exact viewport/scissor before replaying ordering coverage. */
    p_glBindVertexArray(textured ? s_tex_vao : s_geo_vao);
    GLuint program = textured ? s_plus_tex_prog : s_plus_geo_prog;
    p_glUseProgram(program); enhanced_begin(program);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s->scene_fbo);
    glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
    if (s_mask_check) {
        GLint scissor[4]; glGetIntegerv(GL_SCISSOR_BOX, scissor);
        glDisable(GL_SCISSOR_TEST);
        p_glBindFramebuffer(PSXGL_READ_FRAMEBUFFER, s->fbo);
        p_glBlitFramebuffer(0,0,s->width,s->height,0,0,s->width,s->height, GL_STENCIL_BUFFER_BIT,GL_NEAREST);
        glEnable(GL_SCISSOR_TEST); glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
        mask_stencil(write_mask);
    } else glDisable(GL_STENCIL_TEST);
    p_glActiveTexture(PSXGL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, s_raw_tex);
    p_glUniform1i(s_plus_opaque[textured != 0], camera && semi < 0 && s->world_valid == 1);
    p_glUniform3f(s_plus_eye[textured != 0], s->world_eye[0], s->world_eye[1], s->world_eye[2]);
    if (textured) p_glUniform4i(s_plus_twin, s_tb_twin[0], s_tb_twin[1], s_tb_twin[2], s_tb_twin[3]);
    else p_glUniform1i(s_plus_camera_pass, camera && semi < 0);
    glDrawArrays(mode, 0, nverts);
    p_glUseProgram(textured ? s_tex_prog : s_geo_prog); enhanced_begin(textured ? s_tex_prog : s_geo_prog);
}
static void plus_shadow_render(EnhancedSurface *s) {
    PlusShadowState *q = s->plus_shadow;
    if (!q || !q->vertex_count || s->world_valid != 1 || !q->dirty) return;
    if (!s->shadow_tex) {
        s->shadow_tex = make_tex(GL_DEPTH_COMPONENT24, PLUS_SHADOW_SIZE, PLUS_SHADOW_SIZE, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
        glBindTexture(GL_TEXTURE_2D, s->shadow_tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        p_glGenFramebuffers(1, &s->shadow_fbo);
        p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s->shadow_fbo);
        p_glFramebufferTexture2D(PSXGL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, s->shadow_tex, 0);
        glDrawBuffer(GL_NONE); glReadBuffer(GL_NONE);
        if (p_glCheckFramebufferStatus(PSXGL_FRAMEBUFFER) != PSXGL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "psxrecomp: PBR+ depth shadow target incomplete\n"); abort();
        }
    }
    float low[3], high[3];
    for (int axis = 0; axis < 3; ++axis) {
        low[axis] = high[axis] = 0;
        for (size_t i = 0; i < q->vertex_count; ++i) {
            float value = 0;
            for (int j = 0; j < 3; ++j) value += s_plus_light_basis[axis][j]*q->vertices[i].world[j];
            if (!i || value < low[axis]) low[axis] = value;
            if (!i || value > high[axis]) high[axis] = value;
        }
        /* Quantized power-of-two extent and a world-anchored texel grid. Bounds
         * include every submitted vertex, not just visible camera fragments. */
        float span = fmaxf(high[axis]-low[axis], 1.0f);
        float extent = exp2f(ceilf(log2f(span*(1.0f+4.0f/PLUS_SHADOW_SIZE))));
        float texel = extent/PLUS_SHADOW_SIZE;
        float origin = floorf((low[axis]-texel)/texel)*texel;
        for (int j = 0; j < 3; ++j) q->rows[axis][j] = s_plus_light_basis[axis][j]*2.0f/extent;
        q->rows[axis][3] = -1.0f-origin*2.0f/extent;
    }
    GLboolean depth = glIsEnabled(GL_DEPTH_TEST), cull = glIsEnabled(GL_CULL_FACE), depth_write;
    GLint depth_func; GLfloat clear_depth;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_write); glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
    glGetFloatv(GL_DEPTH_CLEAR_VALUE, &clear_depth);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, s->shadow_fbo); glViewport(0,0,PLUS_SHADOW_SIZE,PLUS_SHADOW_SIZE);
    glDisable(GL_SCISSOR_TEST); glDisable(GL_STENCIL_TEST); glDisable(GL_BLEND); glDisable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glDepthMask(GL_TRUE); glClearDepth(1); glClear(GL_DEPTH_BUFFER_BIT);
    p_glUseProgram(s_plus_shadow_prog);
    for (int i = 0; i < 3; ++i) p_glUniform4f(s_plus_shadow_rows[i], q->rows[i][0],q->rows[i][1],q->rows[i][2],q->rows[i][3]);
    p_glBindVertexArray(s_plus_shadow_vao); p_glBindBuffer(PSXGL_ARRAY_BUFFER, s_plus_shadow_vbo);
    p_glBufferData(PSXGL_ARRAY_BUFFER, (ptrdiff_t)(q->vertex_count*sizeof(*q->vertices)), q->vertices, PSXGL_STREAM_DRAW);
    p_glActiveTexture(PSXGL_TEXTURE0);
    for (size_t i = 0; i < q->draw_count; ++i) {
        PlusShadowDraw *d = &q->draws[i];
        p_glUniform1i(s_plus_shadow_textured, d->alpha >= 0);
        /* Keep every sampler complete even for untextured submissions. */
        glBindTexture(GL_TEXTURE_2D, d->alpha >= 0 ? q->alpha[d->alpha].tex : s->tex);
        p_glUniform4i(s_plus_shadow_twin,d->twin[0],d->twin[1],d->twin[2],d->twin[3]);
        glDrawArrays(GL_TRIANGLES, d->first, d->count);
    }
    glDepthMask(depth_write); glDepthFunc((GLenum)depth_func); glClearDepth(clear_depth);
    if (!depth) glDisable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    q->dirty = 0; q->ready = 1;
}
static void plus_present(EnhancedSurface *s, int lx, int ly, int lw, int lh) {
    plus_init(); plus_shadow_render(s);
    p_glBindFramebuffer(PSXGL_FRAMEBUFFER, 0); glViewport(lx, ly, lw, lh);
    p_glUseProgram(s_plus_present_prog);
    GLuint textures[] = {s->tex, s->scene_tex, s->normal_tex, s->shadow_tex ? s->shadow_tex : s->tex};
    for (int i = 0; i < 4; ++i) { p_glActiveTexture(PSXGL_TEXTURE0+i); glBindTexture(GL_TEXTURE_2D,textures[i]); }
    p_glUniform3f(s_plus_present_eye, s->world_eye[0],s->world_eye[1],s->world_eye[2]);
    p_glUniform2i(s_plus_present_origin, lx, ly);
    p_glUniform1i(s_plus_present_qualified, s->world_valid == 1);
    PlusShadowState *q = s->plus_shadow;
    p_glUniform1i(s_plus_present_shadow_valid, q && q->ready && s->world_valid == 1);
    if (q) for (int i = 0; i < 3; ++i) p_glUniform4f(s_plus_present_rows[i],q->rows[i][0],q->rows[i][1],q->rows[i][2],q->rows[i][3]);
    p_glBindVertexArray(s_empty_vao); glDrawArrays(GL_TRIANGLES, 0, 3);
    p_glActiveTexture(PSXGL_TEXTURE0);
}
