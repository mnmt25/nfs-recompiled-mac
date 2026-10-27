#include <lib/gliderenderer.h>
#include <lib/renderer.h>
#include <lib/glidetmu.h>
#include <lib/glfunctions.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

namespace win32
{

static const char g_glVertexShader[] = ""
"#version 410 core\n"
"in vec3 g_position;"
"in vec4 g_color;"
"in vec2 g_texCoord;"
"in vec4 g_combine;"
"in vec4 g_atlasInfo;"
"out vec3 v_texCoord;"
"out vec4 v_color;"
"flat out vec4 v_combine;"
"flat out vec4 v_atlasInfo;"
"uniform mat4 u_transform;"
"void main()"
"{"
"    v_texCoord = vec3(g_texCoord.st, g_combine.q);"
"    v_combine = g_combine;"
"    v_color = g_color;"
"    v_atlasInfo = g_atlasInfo;"
"    gl_Position = u_transform * vec4(g_position, 1.0);"
"}";

static const char g_glFragmentShader[] =
"#version 410 core\n"
"in vec3 v_texCoord;"
"in vec4 v_color;"
"flat in vec4 v_combine;"
"flat in vec4 v_atlasInfo;"
"layout (location=0) out vec4 o_color;"
"uniform sampler2D u_texture;"
""
"vec4 getColor(vec2 coords)"
"{"
"    vec2 texCoords = (v_atlasInfo.pq + clamp(coords, 0.0, v_atlasInfo.t-1.0))/v_atlasInfo.s;"
"    return texture(u_texture, texCoords);"
"}"
"void main()"
"{"
"    vec2 texelPos = (v_texCoord.st/v_texCoord.p)*v_atlasInfo.t/(256.0);"
"    vec2 tex1mod = mod(vec2(texelPos.x-0.25, texelPos.y-0.25), v_atlasInfo.t);"
"    vec2 tex2mod = mod(vec2(texelPos.x+0.25, texelPos.y-0.25), v_atlasInfo.t);"
"    vec2 tex3mod = mod(vec2(texelPos.x-0.25, texelPos.y+0.25), v_atlasInfo.t);"
"    vec2 tex4mod = mod(vec2(texelPos.x+0.25, texelPos.y+0.25), v_atlasInfo.t);"
"    vec2 tex1clamp = clamp(vec2(texelPos.x-0.25, texelPos.y-0.25), 0.0, v_atlasInfo.t);"
"    vec2 tex2clamp = clamp(vec2(texelPos.x+0.25, texelPos.y-0.25), 0.0, v_atlasInfo.t);"
"    vec2 tex3clamp = clamp(vec2(texelPos.x-0.25, texelPos.y+0.25), 0.0, v_atlasInfo.t);"
"    vec2 tex4clamp = clamp(vec2(texelPos.x+0.25, texelPos.y+0.25), 0.0, v_atlasInfo.t);"
"    vec2 tex1 = mix(tex1clamp, tex1mod, v_combine.p);"
"    vec2 tex2 = mix(tex2clamp, tex2mod, v_combine.p);"
"    vec2 tex3 = mix(tex3clamp, tex3mod, v_combine.p);"
"    vec2 tex4 = mix(tex4clamp, tex4mod, v_combine.p);"
"    vec4 c1 = mix(getColor(tex1), getColor(tex2), 0.5);"
"    vec4 c2 = mix(getColor(tex3), getColor(tex4), 0.5);"
"    vec4 textureColor = mix(c1, c2, 0.5);"
"    vec4 color = vec4(mix(v_color.rgb, v_color.rgb * textureColor.rgb, v_combine.s),"
"                      mix(v_color.a, v_color.a * textureColor.a, v_combine.t));"
"    if (color.a < 0.0625) discard;"
"    o_color = color;"
"}";

struct GlVertex
{
    float       x, y, z;
    x86::reg32  color;
    float       u, v;
    float       colorCombine, alphaCombine, texWrap, u2;
    float       atlasSize, width, offX, offY;
};

static const x86::reg32 s_maxVertexCount = 32000;
static const x86::reg32 s_atlasSize = 2048;

GlideRenderer::GlideRenderer(Renderer* renderer)
    :   m_renderer(renderer)
    ,   m_vertices(new GlVertex[s_maxVertexCount])
    ,   m_vertexCount(0)
    ,   m_zBuffer(true)
    ,   m_zTest(true)
    ,   m_alphaBlend(true)
{
    m_tmus[0] = new GlideTMU(s_atlasSize);
    //m_tmus[1] = new GlideTMU(s_atlasSize);
    m_renderer->setCurrent();
    compileShaders();
    glGenTextures(1, &m_atlas);
    glBindTexture(GL_TEXTURE_2D, m_atlas);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, s_atlasSize, s_atlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);

    glGenVertexArrays(1, &m_vertexArray);
    glBindVertexArray(m_vertexArray);
    glGenBuffers(1, &m_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);

    glGenRenderbuffers(1, &m_depthBuffer);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthBuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, m_renderer->m_width, m_renderer->m_height);

    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_renderer->m_texture, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_depthBuffer);
    GLenum drawBuffers[1] = { GL_COLOR_ATTACHMENT0 };
    glDrawBuffers(1, drawBuffers);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    m_renderer->clearCurrent();
}

GlideRenderer::~GlideRenderer()
{
    delete[] m_vertices;
    delete m_tmus[0];
    glDeleteFramebuffers(1, &m_framebuffer);
}

void GlideRenderer::compileShaders()
{
    m_shaderProgram = createShaderProgram(g_glVertexShader, g_glFragmentShader);
    m_attributes[0] = glGetAttribLocation(m_shaderProgram, "g_position");
    m_attributes[1] = glGetAttribLocation(m_shaderProgram, "g_color");
    m_attributes[2] = glGetAttribLocation(m_shaderProgram, "g_texCoord");
    m_attributes[3] = glGetAttribLocation(m_shaderProgram, "g_combine");
    m_attributes[4] = glGetAttribLocation(m_shaderProgram, "g_atlasInfo");
    m_transform = glGetUniformLocation(m_shaderProgram, "u_transform");
    //glUniform1i(glGetUniformLocation(m_shaderProgram, "u_texture"), 0);
}

void GlideRenderer::clear(x86::reg32 color)
{
    m_renderer->setCurrent();
    glDepthMask(true);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_renderer->m_texture, 0);
    glClearColor(float(color >> 16 & 0xff)/255.0f, float(color >> 8 & 0xff)/255.0f, float(color >> 0 & 0xff)/255.0f, 0.0f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    m_renderer->clearCurrent();
}

void GlideRenderer::render(x86::reg32 buffer)
{
    m_renderer->m_currentBuffer = buffer;
}

void GlideRenderer::flush()
{
    DrawCall dcall;
    dcall.zTest = m_zTest;
    dcall.zBuffer = m_zBuffer;
    dcall.alphaBlend = m_alphaBlend;
    dcall.lastTriangle = m_vertexCount;
    m_drawCalls.push_back(dcall);
}

void GlideRenderer::swap()
{
    m_renderer->setCurrent();
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_renderer->m_texture, 0);
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    SDL_GL_SetSwapInterval(mode ? (int)(mode->refresh_rate / 60) : 1);
    if (m_vertexCount)
    {
        flush();
        glViewport(0, 0, m_renderer->m_width, m_renderer->m_height);
        // glOrtho(0, width, 0, height, 0, -65536), column-major
        const float w = float(m_renderer->m_width);
        const float h = float(m_renderer->m_height);
        const float matrix[16] =
        {
            2.f / w, 0.f,     0.f,           0.f,
            0.f,     2.f / h, 0.f,           0.f,
            0.f,     0.f,     2.f / 65536.f, 0.f,
            -1.f,    -1.f,    -1.f,          1.f
        };
        glUseProgram(m_shaderProgram);
        glUniformMatrix4fv(m_transform, 1, GL_FALSE, matrix);
        // Renderer::present() disables blending and binds its own vertex array
        glEnable(GL_BLEND);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_atlas);
        glBindVertexArray(m_vertexArray);
        glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
        glBufferData(GL_ARRAY_BUFFER, sizeof(GlVertex)*m_vertexCount, m_vertices, GL_STREAM_DRAW);
        glVertexAttribPointer(m_attributes[0], 3, GL_FLOAT, GL_FALSE, sizeof(GlVertex), 0);
        glEnableVertexAttribArray(m_attributes[0]);
        glVertexAttribPointer(m_attributes[1], 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GlVertex), (const void*)(4*3));
        glEnableVertexAttribArray(m_attributes[1]);
        glVertexAttribPointer(m_attributes[2], 2, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (const void*)(4*4));
        glEnableVertexAttribArray(m_attributes[2]);
        glVertexAttribPointer(m_attributes[3], 4, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (const void*)(4*6));
        glEnableVertexAttribArray(m_attributes[3]);
        glVertexAttribPointer(m_attributes[4], 4, GL_FLOAT, GL_FALSE, sizeof(GlVertex), (const void*)(4*10));
        glEnableVertexAttribArray(m_attributes[4]);
        x86::reg32 first = 0;
        for (std::vector<DrawCall>::const_iterator it = m_drawCalls.begin(); it != m_drawCalls.end(); ++it)
        {
            if (it->zTest)
            {
                glEnable(GL_DEPTH_TEST);
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }
            glDepthMask(it->zBuffer ? GL_TRUE : GL_FALSE);
            if (it->alphaBlend)
            {
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            }
            else
            {
                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ONE, GL_ZERO);
            }
            glDrawArrays(GL_TRIANGLES, first, it->lastTriangle-first);
            first = it->lastTriangle;
        }
        m_drawCalls.clear();
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glUseProgram(0);
    glFlush();
    glBindTexture(GL_TEXTURE_2D, m_renderer->m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    m_renderer->present();
    m_renderer->clearCurrent();
    m_vertexCount = 0;
}

x86::reg32 GlideRenderer::textureMemStart(x86::reg32 tmu)
{
    return m_tmus[tmu]->textureMemStart() + sizeof(GlTextureSlot*);
}

x86::reg32 GlideRenderer::textureMemEnd(x86::reg32 tmu)
{
    return m_tmus[tmu]->textureMemEnd();
}

x86::reg32 GlideRenderer::getTextureMemSize(x86::reg32 /*largeMipmapSize*/, x86::reg32 /*smallMipmapSize*/, TextureFormat /*format*/)
{
    return sizeof(GlTextureSlot*);
}

void GlideRenderer::setZWrite(bool enable)
{
    if (enable != m_zBuffer)
    {
        flush();
        m_zBuffer = enable;
    }
}

void GlideRenderer::setZTest(bool enable)
{
    if (enable != m_zTest)
    {
        flush();
        m_zTest = enable;
    }
}

void GlideRenderer::setAlphaBlendDst(AlphaBlend method)
{
    if (m_alphaBlend != (method == AB_1mSrcAlpha))
    {
        flush();
        m_alphaBlend = method == AB_1mSrcAlpha;
    }
}

void GlideRenderer::setColorFactor(float colorFactor)
{
    m_colorFactor = colorFactor;
}

void GlideRenderer::setAlphaFactor(float alphaFactor)
{
    m_alphaFactor = alphaFactor;
}

void GlideRenderer::setTexture(x86::reg32 tmu, x86::reg32 address)
{
    NFS2_ASSERT(tmu == 0);
    GlTextureSlot** info = m_tmus[tmu]->getTextureInfo(address);
    m_textureOffsetW = 256 >> (*info)->lod;
    m_textureOffsetX = (*info)->x;
    m_textureOffsetY = (*info)->y;
}

void GlideRenderer::setTextureData(x86::reg32 tmu, x86::reg32 address, const void* data,
                                   x86::reg32 largeMipmap, x86::reg32 smallMipmap, TextureFormat format)
{
    NFS2_ASSERT(tmu == 0);
    x86::reg32 largeMipmapSize = 256 >> largeMipmap;
    x86::reg32 smallMipmapSize = 256 >> smallMipmap;

    m_renderer->setCurrent();
    glBindTexture(GL_TEXTURE_2D, m_atlas);
    GlTextureSlot** info = m_tmus[tmu]->getTextureInfo(address);
    if (*info)
    {
        m_tmus[tmu]->returnTextureSlot(*info);
    }
    *info = m_tmus[tmu]->reserveTextureSlot(largeMipmap);

    x86::reg32 x = (*info)->x;
    x86::reg32 y = (*info)->y;
    x86::reg32* textureData = reinterpret_cast<x86::reg32*>(malloc(largeMipmapSize*largeMipmapSize*4));
    x86::reg32 lod = 0;
    for (; largeMipmapSize >= smallMipmapSize; ++lod, largeMipmapSize >>= 1)
    {
        const x86::reg16* pixelData = reinterpret_cast<const x86::reg16*>(data);
        for (x86::reg32 i = 0; i < largeMipmapSize*largeMipmapSize; ++i)
        {
            textureData[i] = 0;
            switch(format)
            {
            case TF_RGB_565:
                textureData[i] = ((pixelData[i] & 0xf800) << 16)
                               | ((pixelData[i] & 0x07e0) << 13)
                               | ((pixelData[i] & 0x001f) << 11)
                               | 0xff;
                break;
            case TF_ARGB_1555:
                textureData[i] = (((pixelData[i] & 0x7c00) >> 10) * 0xff / 0x1f) << 24
                               | (((pixelData[i] & 0x03e0) >> 5) * 0xff / 0x1f)  << 16
                               | (((pixelData[i] & 0x001f) >> 0) * 0xff / 0x1f)  << 8
                               | ((pixelData[i] & 0x8000) ? 0xff : 0x00);
                break;
            case TF_ARGB_4444:
                {
                    x86::reg8 r = pixelData[i] >> 8  & 0xf;
                    x86::reg8 g = pixelData[i] >> 4  & 0xf;
                    x86::reg8 b = pixelData[i]       & 0xf;
                    x86::reg8 a = pixelData[i] >> 12 & 0xf;
                    textureData[i] = r << 28
                                   | r << 24
                                   | g << 20
                                   | g << 16
                                   | b << 12
                                   | b << 8
                                   | a << 4
                                   | a;
                    break;
                    }
            default:
                NFS2_ASSERT(false);
            }
        }
        glTexSubImage2D(GL_TEXTURE_2D, lod, x, y, largeMipmapSize, largeMipmapSize, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, textureData);
        data = pixelData + largeMipmapSize * largeMipmapSize;
        break;
    }
    for (x86::reg32 mmSize = largeMipmapSize; mmSize >= 1; ++lod, mmSize >>= 1)
    {
        /* TODO */
        //glTexSubImage2D(GL_TEXTURE_2D, lod, 0, 0, mmSize, mmSize, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, textureData);
    }
    free(textureData);
    m_renderer->clearCurrent();
}

void GlideRenderer::drawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c)
{
    NFS2_ASSERT(65535.f - a->ooz >= 0.f);
    NFS2_ASSERT(65535.f - b->ooz >= 0.f);
    NFS2_ASSERT(65535.f - c->ooz >= 0.f);
    NFS2_ASSERT(65535.f - a->ooz <= 65535.f);
    NFS2_ASSERT(65535.f - b->ooz <= 65535.f);
    NFS2_ASSERT(65535.f - c->ooz <= 65535.f);
    m_vertices[m_vertexCount].x = a->x;
    m_vertices[m_vertexCount].y = a->y;
    m_vertices[m_vertexCount].z = a->ooz;
    m_vertices[m_vertexCount].color = (x86::reg8)(a->r)
                                    | (x86::reg8)(a->g) << 8
                                    | (x86::reg8)(a->b) << 16
                                    | (x86::reg8)(a->a) << 24;
    m_vertices[m_vertexCount].u = a->tmuvtx[0].sow;
    m_vertices[m_vertexCount].v = a->tmuvtx[0].tow;
    m_vertices[m_vertexCount].colorCombine = m_colorFactor;
    m_vertices[m_vertexCount].alphaCombine = m_alphaFactor;
    m_vertices[m_vertexCount].atlasSize = s_atlasSize;
    m_vertices[m_vertexCount].texWrap = 1.f;
    m_vertices[m_vertexCount].u2 = a->oow;
    m_vertices[m_vertexCount].width = float(m_textureOffsetW);
    m_vertices[m_vertexCount].offX = float(m_textureOffsetX);
    m_vertices[m_vertexCount].offY = float(m_textureOffsetY);
    m_vertexCount++;
    
    m_vertices[m_vertexCount].x = b->x;
    m_vertices[m_vertexCount].y = b->y;
    m_vertices[m_vertexCount].z = b->ooz;
    m_vertices[m_vertexCount].color = (x86::reg8)(b->r)
                                              | (x86::reg8)(b->g) << 8
                                              | (x86::reg8)(b->b) << 16
                                              | (x86::reg8)(b->a) << 24;
    m_vertices[m_vertexCount].u = b->tmuvtx[0].sow;
    m_vertices[m_vertexCount].v = b->tmuvtx[0].tow;
    m_vertices[m_vertexCount].colorCombine = m_colorFactor;
    m_vertices[m_vertexCount].alphaCombine = m_alphaFactor;
    m_vertices[m_vertexCount].atlasSize = s_atlasSize;
    m_vertices[m_vertexCount].texWrap = 1.f;
    m_vertices[m_vertexCount].u2 = b->oow;
    m_vertices[m_vertexCount].width = float(m_textureOffsetW);
    m_vertices[m_vertexCount].offX = float(m_textureOffsetX);
    m_vertices[m_vertexCount].offY = float(m_textureOffsetY);
    m_vertexCount++;

    m_vertices[m_vertexCount].x = c->x;
    m_vertices[m_vertexCount].y = c->y;
    m_vertices[m_vertexCount].z = c->ooz;
    m_vertices[m_vertexCount].color = (x86::reg8)(c->r)
                                              | (x86::reg8)(c->g) << 8
                                              | (x86::reg8)(c->b) << 16
                                              | (x86::reg8)(c->a) << 24;
    m_vertices[m_vertexCount].u = c->tmuvtx[0].sow;
    m_vertices[m_vertexCount].v = c->tmuvtx[0].tow;
    m_vertices[m_vertexCount].colorCombine = m_colorFactor;
    m_vertices[m_vertexCount].alphaCombine = m_alphaFactor;
    m_vertices[m_vertexCount].texWrap = 1.f;
    m_vertices[m_vertexCount].u2 = c->oow;
    m_vertices[m_vertexCount].atlasSize = s_atlasSize;
    m_vertices[m_vertexCount].width = float(m_textureOffsetW);
    m_vertices[m_vertexCount].offX = float(m_textureOffsetX);
    m_vertices[m_vertexCount].offY = float(m_textureOffsetY);
    m_vertexCount++;

    NFS2_ASSERT(m_vertexCount < s_maxVertexCount);
}

}
