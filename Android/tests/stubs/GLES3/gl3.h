#pragma once
#include <cstdint>
typedef unsigned int GLuint; typedef unsigned int GLenum; typedef int GLint; typedef int GLsizei;
typedef unsigned char GLboolean; typedef unsigned char GLubyte; typedef float GLfloat;
typedef unsigned int GLbitfield; typedef char GLchar; typedef intptr_t GLintptr;
#define GL_NO_ERROR 0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_LINEAR 0x2601
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_SCISSOR_TEST 0x0C11
#define GL_VERSION 0x1F02
#define GL_RENDERER 0x1F01
#define GL_VENDOR 0x1F00
extern "C" void glGenTextures(GLsizei, GLuint*); void glBindTexture(GLenum, GLuint);
extern "C" void glTexParameteri(GLenum, GLenum, GLint); void glPixelStorei(GLenum, GLint);
void glTexImage2D(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
extern "C" void glViewport(GLint, GLint, GLsizei, GLsizei); void glClearColor(GLfloat, GLfloat, GLfloat, GLfloat);
extern "C" void glClear(GLbitfield); void glGetIntegerv(GLenum, GLint*); GLenum glGetError();
extern "C" void glScissor(GLint, GLint, GLsizei, GLsizei); void glEnable(GLenum); void glDisable(GLenum);
extern "C" const GLubyte* glGetString(GLenum);
