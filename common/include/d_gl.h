/*
 * This file is part of the DXX-Rebirth project <https://www.dxx-rebirth.com/>.
 * It is copyright by its individual contributors, as recorded in the
 * project's Git history.  See COPYING.txt at the top level for license
 * terms and a link to the Git history.
 */

#include "dxxsconf.h"
#if !DXX_USE_OGL
#error "This file can only be included in OpenGL enabled builds."
#endif

#if DXX_USE_VULKAN
// Vulkan mode: no GL headers needed; provide stub GL types for ogl_texture
typedef unsigned int GLuint;
typedef int GLint;
typedef unsigned int GLenum;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef int GLsizei;
typedef unsigned char GLubyte;
typedef signed char GLbyte;
#define GL_REPEAT 0x2901
#define GL_CLAMP_TO_EDGE 0x812F
#else
#ifdef _WIN32
#include "loadgl.h"
#else
#	define GL_GLEXT_LEGACY
#	if defined(__APPLE__) && defined(__MACH__)
#		include <OpenGL/gl.h>
#		include <OpenGL/glu.h>
#	else
#		define GL_GLEXT_PROTOTYPES
#		if DXX_USE_OGLES
#			include <GLES/gl.h>
#		else
#			include <GL/gl.h>
#			include <GL/glext.h>
#		endif
#	endif
#endif
#endif  // DXX_USE_VULKAN
