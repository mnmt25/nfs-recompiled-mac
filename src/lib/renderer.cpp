#include <lib/renderer.h>
#include <lib/window.h>
#include <lib/gamepad.h>
#include <lib/glfunctions.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>
#ifndef __APPLE__
# include <GL/gl.h>
#endif

namespace win32
{

void GLAPIENTRY errorCallback(GLenum /*source*/, GLenum type, GLuint /*id*/, GLenum severity, GLsizei /*length*/, const GLchar* message, const void* /*userParam*/)
{
    if (severity != GL_DEBUG_SEVERITY_NOTIFICATION)
    SDL_LogError(SDL_LOG_CATEGORY_RENDER, "OpenGL: %s type = 0x%x, severity = 0x%x, \"%s\"\n",
            (type == GL_DEBUG_TYPE_ERROR ? "** GL ERROR **" : ""),
            type, severity, message);
}
PFNGLDEBUGMESSAGECALLBACKPROC glDebugMessageCallback;

static const char g_blitVertexShader[] =
"#version 410 core\n"
"layout (location=0) in vec2 g_position;"
"layout (location=1) in vec2 g_texCoord;"
"out vec2 v_texCoord;"
"void main()"
"{"
"    v_texCoord = g_texCoord;"
"    gl_Position = vec4(g_position, 0.0, 1.0);"
"}";

static const char g_blitFragmentShader[] =
"#version 410 core\n"
"in vec2 v_texCoord;"
"layout (location=0) out vec4 o_color;"
"uniform sampler2D u_texture;"
"void main()"
"{"
"    o_color = vec4(texture(u_texture, v_texCoord).rgb, 1.0);"
"}";

// Full-screen quad: x, y in clip space, then u, v (v = 0 is the top row)
static const GLfloat g_blitQuad[] =
{
    -1.f,  1.f, 0.f, 0.f,
     1.f,  1.f, 1.f, 0.f,
    -1.f, -1.f, 0.f, 1.f,
     1.f,  1.f, 1.f, 0.f,
     1.f, -1.f, 1.f, 1.f,
    -1.f, -1.f, 0.f, 1.f,
};

static SDL_GLContext createContext(SDL_Window* window)
{
    // 4.1 core is the newest profile macOS offers; both renderers stick to it
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GLContext context = SDL_GL_CreateContext(window);
    if (!context)
    {
        SDL_LogWarn(SDL_LOG_CATEGORY_RENDER, "No OpenGL 4.1 core context (%s), using the default one", SDL_GetError());
        SDL_GL_ResetAttributes();
        context = SDL_GL_CreateContext(window);
    }
    return context;
}

Renderer::Renderer(WinApplication* application, Window *window)
    :   m_application(application)
    ,   m_window(window)
    ,   m_renderer(createContext(m_window->m_window))
    ,   m_videoMemory(new MemMap(800*600*2*2)) // double buffer 16 bits 800x600
    ,   m_currentBuffer(0)
    ,   m_depth(16)
    ,   m_colorPalette()
{
    setCurrent();
    SDL_GL_SetSwapInterval(1);
    loadGLFunctions();
    glDebugMessageCallback = (PFNGLDEBUGMESSAGECALLBACKPROC)SDL_GL_GetProcAddress("glDebugMessageCallback");
    if (glDebugMessageCallback)
    {
        glEnable(GL_DEBUG_OUTPUT);
        glDebugMessageCallback(errorCallback, 0);
    }

    glGenTextures(1, &m_texture);
    glBindTexture(GL_TEXTURE_2D, m_texture);

    m_program = createShaderProgram(g_blitVertexShader, g_blitFragmentShader);
    glGenVertexArrays(1, &m_vertexArray);
    glBindVertexArray(m_vertexArray);
    glGenBuffers(1, &m_vertexBuffer);
    glBindBuffer(GL_ARRAY_BUFFER, m_vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(g_blitQuad), g_blitQuad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), 0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4*sizeof(GLfloat), (const void*)(2*sizeof(GLfloat)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    clearCurrent();
}

Renderer::~Renderer()
{
    SDL_GL_DestroyContext(static_cast<SDL_GLContext>(m_renderer));
}

void Renderer::setCurrent()
{
    SDL_GL_MakeCurrent(m_window->m_window, static_cast<SDL_GLContext>(m_renderer));
}

void Renderer::clearCurrent()
{
    SDL_GL_MakeCurrent(m_window->m_window, nullptr);
}

void Renderer::setVideoMode(x86::reg32 w, x86::reg32 h, x86::reg32 bpp)
{
    m_width = w;
    m_height = h;
    m_depth = bpp;
    setCurrent();
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, nullptr);
    clearCurrent();
    //glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, nullptr);
}

void Renderer::updatePalette(x86::reg32 colorCount, const x86::reg32* colors)
{
    NFS2_ASSERT(colorCount <= 256);
    memcpy(m_colorPalette, colors, colorCount*4);
}

x86::reg32 Renderer::getFrontBuffer() const
{
    return m_videoMemory->getBlockStart() + m_currentBuffer * m_width * m_height * 2;
}

x86::reg32 Renderer::getBackBuffer() const
{
    return m_videoMemory->getBlockStart() + (1 - m_currentBuffer) * m_width * m_height * 2;
}

void Renderer::present()
{
    Gamepad::updateKeys();
    glBindTexture(GL_TEXTURE_2D, m_texture);
    int w, h;
    SDL_GetWindowSizeInPixels(m_window->m_window, &w, &h);

    float gameAspect = float(m_width) / float(m_height);
    float windowAspect = float(w) / float(h);
    int vpX, vpY, vpW, vpH;
    if (windowAspect > gameAspect)
    {
        vpH = h;
        vpW = int(h * gameAspect + 0.5f);
        vpX = (w - vpW) / 2;
        vpY = 0;
    }
    else
    {
        vpW = w;
        vpH = int(w / gameAspect + 0.5f);
        vpX = 0;
        vpY = (h - vpH) / 2;
    }

    glViewport(0, 0, w, h);
    glClearColor(0.f, 0.f, 0.f, 1.f);
    glClear(GL_COLOR_BUFFER_BIT);

    // Render the game texture into the aspect-ratio-correct viewport
    glViewport(vpX, vpY, vpW, vpH);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glUseProgram(m_program);
    glBindVertexArray(m_vertexArray);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glUseProgram(0);
    glFlush();
    SDL_GL_SwapWindow(m_window->m_window);
}

void Renderer::update()
{
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    if (m_depth == 16)
    {
        //MemMap::fillDebugGraph(&m_application->getMemory<x86::reg16>(getFrontBuffer()) + m_width*2);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_SHORT_5_6_5, &m_application->getMemory<x86::reg16>(getFrontBuffer()));
    }
    else
    {
        x86::reg8* srcData = &m_application->getMemory<x86::reg8>(getFrontBuffer());
        x86::reg32* screenData = reinterpret_cast<x86::reg32*>(malloc(m_width * m_height * 4));
        for (x86::reg32 i = 0; i < m_width*m_height; ++i)
        {
            screenData[i] = m_colorPalette[srcData[i]];
        }
        //MemMap::fillDebugGraph(screenData + m_width*2);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_INT_8_8_8_8, screenData);
        free(screenData);
    }
    present();
}

x86::reg32 Renderer::lock(x86::reg32 index)
{
    return index == 0 ? getFrontBuffer() : getBackBuffer();
}

void Renderer::unlock(x86::reg32 index)
{
    if (index == 0)
    {
        setCurrent();
        SDL_GL_SetSwapInterval(0);
        update();
        clearCurrent();
    }
}

void Renderer::swap()
{
    setCurrent();
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
    SDL_GL_SetSwapInterval(1);
    m_currentBuffer = 1 - m_currentBuffer;
    for (int i = 0; i < (mode ? (int)(mode->refresh_rate / 30) : 2); ++i)
    {
        // We enabled vsync and now render until we have a refresh rate of 30
        update();
    }
    clearCurrent();
}

}
