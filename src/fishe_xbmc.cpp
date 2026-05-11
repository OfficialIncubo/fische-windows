#include "fishe_xbmc.h"


/*
 *  Copyright (C) 2005-2022 Team Kodi (https://kodi.tv)
 *  Copyright (C) 2012 Marcel Ebmer
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "fische_internal.h"
#include "fishe_xbmc.h"
#include "glad/include/glad/glad.h"
#include "app_settings.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <glfw/glfw3.h>
#include <iostream>
#include <logger.h>
#include <sstream>
#include <string>
#include <sys/stat.h>

const char* vertSource = R"(
#version 150

// Uniforms
uniform mat4 u_projectionMatrix;
uniform mat4 u_modelViewMatrix;

// Attributes
in vec4 a_pos;
in vec2 a_coord;

// Varyings
out vec2 v_coord;

void main ()
{
  gl_Position = u_projectionMatrix * u_modelViewMatrix * a_pos;
  v_coord = a_coord;
}

)";

const char* fragSource = R"(
#version 150

// Uniforms
uniform sampler2D u_texture;

// Varyings
in vec2 v_coord;
in vec4 v_color;

out vec4 fragColor;

void main ()
{
  fragColor = texture(u_texture, v_coord);
}

)";

CFishBMC::CFishBMC(uint32_t _w, uint32_t _h, int quality, bool nervous, bool filePersistence)
{
  m_fische = fische_new();
  m_fische->on_beat = &on_beat;
  m_fische->pixel_format = FISCHE_PIXELFORMAT_0xAABBGGRR;
  m_fische->line_style = FISCHE_LINESTYLE_THICK;
  m_aspect = double(_w) / double(_h);
  m_texleft = (2 - m_aspect) / 4;
  m_texright = 1 - m_texleft;
  m_filemode = filePersistence;
  m_fische->nervous_mode = nervous ? 1 : 0;
  m_fische->handler = this;
  m_fische->height = _h;
  m_fische->width = _w;
  if (m_filemode)
  {
    m_fische->read_vectors = &read_vectors;
    m_fische->write_vectors = &write_vectors;
  }


  int detail = std::clamp(quality, 0, 3);
  m_size = 128;
  while (detail--)
  {
    m_size *= 2;
  }

/*
  int divisor = 3 - std::clamp(quality, 0, 3);
  m_framedivisor = 8;
  while (divisor--)
  {
    m_framedivisor /= 2;
  }
*/

  // Always update fische every rendered frame for smooth animation.
  // The original Kodi addon used a framedivisor (up to 8) to throttle
  // fische computation relative to a high-frequency host render loop.
  // In the standalone Windows app we control the frame rate ourselves,
  // so there is no reason to skip fische updates.

  m_framedivisor = 1;
  if (m_framedivisor < 1)
    m_framedivisor = 1;
  FISHE_LOG_DEBUG("Creating");
  // coordinate system:
  //     screen top left: (-1, -1)
  //     screen bottom right: (1, 1)
  //     screen depth clipping: 3 to 15
  m_projMatrix = glm::frustum(-1.0f, 1.0f, 1.0f, -1.0f, 3.0f, 15.0f);
}

CFishBMC::~CFishBMC()
{
  Stop();
  fische_free(m_fische);
  m_fische = nullptr;
}

bool CFishBMC::Start(int channels,
                                  int samplesPerSec,
                                  int bitsPerSample,
                                  const std::string& songName)
{
  m_errorstate = false;
  FISHE_LOG_DEBUG("STARTING");

  m_fische->audio_format = FISCHE_AUDIOFORMAT_FLOAT;
  m_fische->used_cpus = 4;

  if (fische_start(m_fische) != 0)
  {
    FISHE_LOG_ERROR("fische failed to start: %s", m_fische->error_text);
    m_errorstate = true;
    return false;
  }

  uint32_t* pixels = fische_render(m_fische);


    CompileAndLink();


#ifdef HAS_GL
  glGenBuffers(2, m_vertexVBO);
  glGenBuffers(1, &m_indexVBO);
#endif

  FISHE_LOG_DEBUG("CREATED BUFFERS %dx%d",m_fische->width,m_fische->height);
  // generate a texture for drawing into
  glGenTextures(1, &m_texture);
  glBindTexture(GL_TEXTURE_2D, m_texture);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, m_fische->width, m_fische->height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, pixels);

  m_isrotating = false;
  m_angle = 0;
  m_lastangle = 0;
  m_angleincrement = 0;
  m_startOK = true;
  return true;
}

void CFishBMC::Stop()
{
  if (!m_startOK)
    return;

  glDeleteTextures(1, &m_texture);


  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDeleteBuffers(2, m_vertexVBO);
  m_vertexVBO[0] = 0;
  m_vertexVBO[1] = 0;

  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
  glDeleteBuffers(1, &m_indexVBO);
  m_indexVBO = 0;

  if (programid)
  {
    glDeleteProgram(programid);
    programid = 0;
  }

  m_startOK = false;
}

void CFishBMC::AudioData(const char* pAudioData, size_t iAudioDataLength)
{
  if (!m_startOK)
    return;
  //FISHE_LOG_DEBUG("AUDIO DATA");
  fische_audiodata(m_fische, pAudioData, iAudioDataLength);
}

void CFishBMC::SetNervousMode(bool enabled)
{
  if (m_fische)
    m_fische->nervous_mode = enabled ? 1 : 0;
}

void CFishBMC::Render()
{
  //
  static int frame = 0;

  if (!m_startOK)
  {
    FISHE_LOG_ERROR("NOT STARTED");
    return;
  }
  // check if this frame is to be skipped
  //if (++frame % m_framedivisor == 0)
  //{
    uint32_t* pixels = fische_render(m_fische);
    glBindTexture(GL_TEXTURE_2D, m_texture);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_fische->width, m_fische->height, GL_RGBA,
                    GL_UNSIGNED_BYTE, pixels);
    if (m_isrotating)
      m_angle += m_angleincrement;
  //}

  // stop rotation if required
  if (m_isrotating)
  {
    if (m_angle - m_lastangle > 180)
    {
      m_lastangle = m_lastangle ? 0 : 180;
      m_angle = m_lastangle;
      m_isrotating = false;
    }
  }

  // how many quads will there be?
  int n_Y = 8;
  int n_X = (m_aspect * 8 + 0.5);

  // one-time initialization of rotation axis array
  if (m_axis.empty())
  {
    m_axis.resize(n_X * n_Y);
    for (int i = 0; i < n_X * n_Y; ++i)
    {
      m_axis[i] = rand() % 2;
    }
  }

  start_render();


  // loop over and draw all quads
  int quad_count = 0;
  double quad_width = 4.0 / n_X;
  double quad_height = 4.0 / n_Y;
  double tex_width = (m_texright - m_texleft);

  for (double X = 0; X < n_X; X += 1)
  {
    for (double Y = 0; Y < n_Y; Y += 1)
    {
      double center_x = -2 + (X + 0.5) * 4 / n_X;
      double center_y = -2 + (Y + 0.5) * 4 / n_Y;
      double tex_left = m_texleft + tex_width * X / n_X;
      double tex_right = m_texleft + tex_width * (X + 1) / n_X;
      double tex_top = Y / n_Y;
      double tex_bottom = (Y + 1) / n_Y;
      double angle = (m_angle - m_lastangle) * 4 - (X + Y * n_X) / (n_X * n_Y) * 360;
      if (angle < 0)
        angle = 0;
      if (angle > 360)
        angle = 360;
      textured_quad(center_x, center_y, angle, m_axis[quad_count++], quad_width, quad_height,
                    tex_left, tex_right, tex_top, tex_bottom);
    }
  }
  unsigned char pixel[4]; // Conterrà R, G, B, A
  int width, height;
  glfwGetFramebufferSize(window, &width, &height);
  glReadPixels(m_fische->width / 2, m_fische->height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel);
//  FISHE_LOG_DEBUG("Pixel: R=%d G=%d B=%d A=%d",pixel[0], pixel[1], pixel[2], pixel[4]);

  finish_render();
  // Make sure all drawing commands are complete before Spout copies the frame.
  glFinish();
  SendFrame(Sender);

}

bool CFishBMC::CompileAndLink() {
  // 1. Creazione e Compilazione Vertex Shader
  GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertex, 1, &vertSource, NULL);
  glCompileShader(vertex);
  //if (!checkErrors(vertex, "COMPILATION")) return false;

  // 2. Creazione e Compilazione Fragment Shader
  GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragment, 1, &fragSource, NULL);
  glCompileShader(fragment);
  //if (!checkErrors(fragment, "COMPILATION")) return false;

  // 3. Linking del Programma
  programid = glCreateProgram();
  glAttachShader(programid, vertex);
  glAttachShader(programid, fragment);
  glLinkProgram(programid);
  FISHE_LOG_DEBUG("PROGRAMID: %d",programid);
  //if (!checkErrors(ID, "LINKING")) return false;


  glValidateProgram(programid);

  GLint status;
  glGetProgramiv(programid, GL_VALIDATE_STATUS, &status);
  if (status == GL_FALSE) {
    // Qui dovresti leggere il log di errore con glGetProgramInfoLog
    FISHE_LOG_ERROR("Program validation failed!");
    return false;
  }

  // 4. Pulizia (gli shader possono essere eliminati dopo il link)
  glDeleteShader(vertex);
  glDeleteShader(fragment);

  OnCompiledAndLinked();
  return true;
}

void CFishBMC::OnCompiledAndLinked()
{

  // Variables passed directly to the Vertex shader
  m_uProjMatrixLoc = glGetUniformLocation(programid, "u_projectionMatrix");
  m_uModelViewMatrixLoc = glGetUniformLocation(programid, "u_modelViewMatrix");

  m_aVertexLoc = glGetAttribLocation(programid, "a_pos");
  m_aCoordLoc = glGetAttribLocation(programid, "a_coord");

}

bool CFishBMC::OnEnabled()
{
  glUniformMatrix4fv(m_uProjMatrixLoc, 1, GL_FALSE, glm::value_ptr(m_projMatrix));
  glUniformMatrix4fv(m_uModelViewMatrixLoc, 1, GL_FALSE, glm::value_ptr(m_modelMatrix));
  return true;
}

// OpenGL: paint a textured quad
void CFishBMC::textured_quad(float center_x,
                                          float center_y,
                                          float angle,
                                          float axis,
                                          float width,
                                          float height,
                                          float tex_left,
                                          float tex_right,
                                          float tex_top,
                                          float tex_bottom)
{
  float scale = 1 - sin(angle / 360 * M_PI) / 3;

  glm::mat4 modelMatrixOld = m_modelMatrix;
  m_modelMatrix = glm::translate(m_modelMatrix, glm::vec3(center_x, center_y, 0));
  m_modelMatrix = glm::rotate(m_modelMatrix, glm::radians(angle), glm::vec3(axis, 1 - axis, 0.0f));
  m_modelMatrix = glm::scale(m_modelMatrix, glm::vec3(scale, scale, scale));

  m_coord[0] = sCoord(tex_left, tex_top);
  m_vertex[0] = sPosition(-width / 2, -height / 2, 0);

  m_coord[1] = sCoord(tex_right, tex_top);
  m_vertex[1] = sPosition(width / 2, -height / 2, 0);

  m_coord[2] = sCoord(tex_right, tex_bottom);
  m_vertex[2] = sPosition(width / 2, height / 2, 0);

  m_coord[3] = sCoord(tex_left, tex_bottom);
  m_vertex[3] = sPosition(-width / 2, height / 2, 0);

  //EnableShader();
  glUseProgram(programid);
  OnEnabled();

  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[0]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(sPosition) * 4, m_vertex, GL_STATIC_DRAW);
  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[1]);
  glBufferData(GL_ARRAY_BUFFER, sizeof(sCoord) * 4, m_coord, GL_STATIC_DRAW);
  glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(GLuint) * 4, m_indexer, GL_STATIC_DRAW);
  glDrawElements(GL_TRIANGLE_STRIP, 4, GL_UNSIGNED_INT, 0);

  glUseProgram(0);
  //DisableShader();

  m_modelMatrix = modelMatrixOld;
}

// OpenGL: setup to start rendering
void CFishBMC::start_render()
{
#ifdef HAS_GL
  glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_indexVBO);

  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[0]);
  glVertexAttribPointer(m_aVertexLoc, 4, GL_FLOAT, GL_TRUE, sizeof(sPosition), nullptr);
  glEnableVertexAttribArray(m_aVertexLoc);

  glBindBuffer(GL_ARRAY_BUFFER, m_vertexVBO[1]);
  glVertexAttribPointer(m_aCoordLoc, 2, GL_FLOAT, GL_TRUE, sizeof(sCoord), nullptr);
  glEnableVertexAttribArray(m_aCoordLoc);
#else
  glVertexAttribPointer(m_aVertexLoc, 4, GL_FLOAT, GL_FALSE, 0, m_vertex);
  glEnableVertexAttribArray(m_aVertexLoc);

  glVertexAttribPointer(m_aCoordLoc, 2, GL_FLOAT, GL_FALSE, 0, m_coord);
  glEnableVertexAttribArray(m_aCoordLoc);
#endif

  // enable blending
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  // Keep the framebuffer alpha channel opaque so Spout does not
  // capture a translucent frame and darken it on the receiver side.
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);

  // disable depth testing
  glDisable(GL_DEPTH_TEST);

#ifdef HAS_GL
  // paint both sides of polygons
  glPolygonMode(GL_FRONT, GL_FILL);
#endif

  // bind global texture
  glBindTexture(GL_TEXTURE_2D, m_texture);

  m_modelMatrix =
      glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, 0.0f, -6.0f)); // move 6 units into the screen
  m_modelMatrix = glm::rotate(m_modelMatrix, glm::radians(m_angle), glm::vec3(0.0f, 1.0f, 0.0f)); // rotate
}

// OpenGL: done rendering
void CFishBMC::finish_render()
{
  glDisableVertexAttribArray(m_aCoordLoc);
  glDisableVertexAttribArray(m_aVertexLoc);
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void CFishBMC::on_beat(void* handler, double frames_per_beat)
{
  if (!handler)
    return;

  CFishBMC* thisClass = static_cast<CFishBMC*>(handler);
  if (!thisClass->m_isrotating)
  {
    thisClass->m_isrotating = true;
    if (frames_per_beat < 1)
      frames_per_beat = 12;
    thisClass->m_angleincrement = 180 / 4 / frames_per_beat;
  }
}


void CFishBMC::SendFrame(SpoutSender* sender)
{
  if (!sender)
    return;
 
  // Send the full rendered framebuffer (default FBO = 0) so that tile
  // rotations/flips driven by beat detection are included in the Spout
  // output, not just the raw fische pixel buffer.
  // bInvert=true corrects OpenGL's bottom-left origin to the top-left
  // convention that Spout consumers expect.
  int fbWidth = 0, fbHeight = 0;
  glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
  if (fbWidth > 0 && fbHeight > 0)
    sender->SendFbo(0, static_cast<unsigned>(fbWidth), static_cast<unsigned>(fbHeight), true);
}

std::filesystem::path CFishBMC::vector_cache_path() const
{
  std::filesystem::path dir = GetExecutableDirectory() / "vectors";
  std::filesystem::create_directories(dir);
  return dir /
         ("fische-vectors-" + std::to_string(m_fische->width) + "x" +
          std::to_string(m_fische->height) + ".bin");
}

void CFishBMC::write_vectors(void* handler, const void* data, size_t bytes)
{
  auto* self = static_cast<CFishBMC*>(handler);
  if (!self || !data || bytes == 0)
    return;

  std::ofstream file(self->vector_cache_path(), std::ios::binary | std::ios::trunc);
  if (file)
    file.write(static_cast<const char*>(data), static_cast<std::streamsize>(bytes));
}

size_t CFishBMC::read_vectors(void* handler, void** data)
{
  auto* self = static_cast<CFishBMC*>(handler);
  if (!self || !data)
    return 0;

  std::ifstream file(self->vector_cache_path(), std::ios::binary | std::ios::ate);
  if (!file)
    return 0;

  std::streamsize size = file.tellg();
  if (size <= 0)
    return 0;

  file.seekg(0, std::ios::beg);
  void* buffer = malloc(static_cast<size_t>(size));
  if (!buffer)
    return 0;

  if (!file.read(static_cast<char*>(buffer), size))
  {
    free(buffer);
    return 0;
  }

  *data = buffer;
  return static_cast<size_t>(size);
}



