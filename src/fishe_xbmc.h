//
// Created by ceco on 03/05/2026.
//

#ifndef FISHE_GLFW_FISHE_XBMC_H
#define FISHE_GLFW_FISHE_XBMC_H

#include "fische.h"
#include <string>
#include <filesystem>
#include <vector>

#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include "glad/include/glad/glad.h"
#include <glm/gtc/type_ptr.hpp>
#include <glfw/glfw3.h>
#include "logger.h"
#include "spout/SpoutSender.h"

struct sPosition
{
  sPosition() : x(0.0f), y(0.0f), z(1.0f), u(1.0f) {}
  sPosition(float* d) : x(d[0]), y(d[1]), z(d[2]), u(1.0f) {}
  sPosition(float x, float y, float z = 0.0f) : x(x), y(y), z(z), u(1.0f) {}
  float x, y, z, u;
};

struct sCoord
{
  sCoord() : s(0.0f), t(0.0f) {}
  sCoord(float s, float t) : s(s), t(t) {}
  float s, t;
};

class CFishBMC
{
public:
  CFishBMC(uint32_t _w, uint32_t _h, int quality = 2, bool nervous = false, bool filePersistence = false);
  ~CFishBMC();

  bool Start(int channels,
             int samplesPerSec,
             int bitsPerSample,
             const std::string& songName);
  void Stop();
  void Render();
  void AudioData(const char* audioData, size_t audioDataLength);
  void SetNervousMode(bool enabled);
  void OnCompiledAndLinked();
  void SendFrame(SpoutSender* sender);
  void SendFixedFrame(SpoutSender* sender);
  void SetupFixedFbo(int w, int h);
  void DestroyFixedFbo();

  SpoutSender *Sender;
  bool m_useFixedSpout = false;

  GLFWwindow *window;
  GLuint m_texture = 0;
  bool OnEnabled();
private:
  void start_render();
  void finish_render();
  void textured_quad(float center_x,
                     float center_y,
                     float angle,
                     float axis,
                     float width,
                     float height,
                     float tex_left,
                     float tex_right,
                     float tex_top,
                     float tex_bottom);
  static void on_beat(void* handler, double frames_per_beat);
  static void write_vectors(void* handler, const void* data, size_t bytes);
  static size_t read_vectors(void* handler, void** data);
  void delete_vectors();
  std::filesystem::path vector_cache_path() const;
  GLuint programid = 0;


  bool CompileAndLink();

  bool m_startOK = false;
  bool m_shaderLoaded = false;

  glm::mat4 m_projMatrix;
  glm::mat4 m_modelMatrix;

  sPosition m_vertex[4];
  sCoord m_coord[4];
  GLuint m_indexer[4] = {0, 1, 3, 2};

  GLint m_uProjMatrixLoc = -1;
  GLint m_uModelViewMatrixLoc = -1;
  GLint m_aVertexLoc = -1;
  GLint m_aCoordLoc = -1;

  GLuint m_vertexVBO[2] = {0};
  GLuint m_indexVBO = 0;

  GLuint m_fixedFbo = 0;
  GLuint m_fixedTexture = 0;
  GLuint m_fixedRbo = 0;

  FISCHE* m_fische = nullptr;
  float m_aspect;
  bool m_isrotating;
  float m_angle;
  float m_lastangle;
  bool m_errorstate;
  int m_framedivisor;
  float m_angleincrement;
  float m_texright;
  float m_texleft;
  bool m_filemode;
  int m_size;
  int m_fixedWidth       = 0;
  int m_fixedHeight      = 0;
  bool m_fixedFboReady   = false;
  std::vector<uint8_t> m_axis;


};


#endif //FISHE_GLFW_FISHE_XBMC_H
