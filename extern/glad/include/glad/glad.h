/*
 * GLAD - OpenGL 3.3 Core Profile Loader
 * Generated for Nostalty Engine
 * 
 * This is a minimal GLAD-compatible header for OpenGL 3.3 Core Profile.
 * It provides all the function pointers and constants needed by the engine.
 */

#ifndef GLAD_GL_H_
#define GLAD_GL_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ---------- Platform detection ---------- */
#ifdef _WIN32
  #define APIENTRY __stdcall
  #define WINGDIAPI __declspec(dllimport)
#else
  #define APIENTRY
#endif

#ifndef APIENTRYP
  #define APIENTRYP APIENTRY *
#endif

#ifndef GLAPI
  #define GLAPI extern
#endif

/* ---------- Basic types ---------- */
#include <stddef.h>
#include <stdint.h>

typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef int8_t GLbyte;
typedef uint8_t GLubyte;
typedef int16_t GLshort;
typedef uint16_t GLushort;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef double GLdouble;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

/* ---------- OpenGL 3.3 Constants ---------- */

/* Boolean */
#define GL_TRUE                   1
#define GL_FALSE                  0

/* Errors */
#define GL_NO_ERROR               0
#define GL_INVALID_ENUM           0x0500
#define GL_INVALID_VALUE          0x0501
#define GL_INVALID_OPERATION      0x0502
#define GL_OUT_OF_MEMORY          0x0505

/* Data types */
#define GL_BYTE                   0x1400
#define GL_UNSIGNED_BYTE          0x1401
#define GL_SHORT                  0x1402
#define GL_UNSIGNED_SHORT         0x1403
#define GL_INT                    0x1404
#define GL_UNSIGNED_INT           0x1405
#define GL_FLOAT                  0x1406

/* Primitives */
#define GL_POINTS                 0x0000
#define GL_LINES                  0x0001
#define GL_LINE_STRIP             0x0003
#define GL_TRIANGLES              0x0004
#define GL_TRIANGLE_STRIP         0x0005
#define GL_TRIANGLE_FAN           0x0006

/* Buffers */
#define GL_ARRAY_BUFFER           0x8892
#define GL_UNIFORM_BUFFER         0x8A11
#define GL_INVALID_INDEX          0xFFFFFFFFu
#define GL_UNIFORM_BLOCK_DATA_SIZE 0x8A40
#define GL_ELEMENT_ARRAY_BUFFER   0x8893
#define GL_STATIC_DRAW            0x88E4
#define GL_DYNAMIC_DRAW           0x88E8
#define GL_STREAM_DRAW            0x88E0

/* Enable caps */
#define GL_DEPTH_TEST             0x0B71
#define GL_BLEND                  0x0BE2
#define GL_CULL_FACE              0x0B44
#define GL_SCISSOR_TEST           0x0C11
#define GL_STENCIL_TEST           0x0B90
#define GL_MULTISAMPLE            0x809D

/* Blend functions */
#define GL_ZERO                   0
#define GL_ONE                    1
#define GL_SRC_ALPHA              0x0302
#define GL_ONE_MINUS_SRC_ALPHA    0x0303
#define GL_DST_ALPHA              0x0304
#define GL_ONE_MINUS_DST_ALPHA    0x0305
#define GL_SRC_COLOR              0x0300
#define GL_DST_COLOR              0x0306

/* Blend equations */
#define GL_FUNC_ADD               0x8006
#define GL_FUNC_SUBTRACT          0x800A
#define GL_FUNC_REVERSE_SUBTRACT  0x800B
#define GL_MIN                    0x8007
#define GL_MAX                    0x8008

/* Depth functions */
#define GL_NEVER                  0x0200
#define GL_LESS                   0x0201
#define GL_EQUAL                  0x0202
#define GL_LEQUAL                 0x0203
#define GL_GREATER                0x0204
#define GL_NOTEQUAL               0x0205
#define GL_GEQUAL                 0x0206
#define GL_ALWAYS                 0x0207

/* Face culling */
#define GL_FRONT                  0x0404
#define GL_BACK                   0x0405
#define GL_FRONT_AND_BACK         0x0408
#define GL_CW                     0x0900
#define GL_CCW                    0x0901

/* Clear bits */
#define GL_COLOR_BUFFER_BIT       0x00004000
#define GL_DEPTH_BUFFER_BIT       0x00000100
#define GL_STENCIL_BUFFER_BIT     0x00000400

/* Polygon mode */
#define GL_POINT                  0x1B00
#define GL_LINE                   0x1B01
#define GL_FILL                   0x1B02

/* Textures */
#define GL_TEXTURE_2D             0x0DE1
#define GL_TEXTURE0               0x84C0
#define GL_TEXTURE1               0x84C1
#define GL_TEXTURE2               0x84C2
#define GL_TEXTURE3               0x84C3
#define GL_TEXTURE4               0x84C4
#define GL_TEXTURE5               0x84C5
#define GL_TEXTURE6               0x84C6
#define GL_TEXTURE7               0x84C7
#define GL_TEXTURE_MIN_FILTER     0x2801
#define GL_TEXTURE_MAG_FILTER     0x2800
#define GL_TEXTURE_WRAP_S         0x2802
#define GL_TEXTURE_WRAP_T         0x2803
#define GL_NEAREST                0x2600
#define GL_LINEAR                 0x2601
#define GL_NEAREST_MIPMAP_NEAREST 0x2700
#define GL_LINEAR_MIPMAP_LINEAR   0x2703
#define GL_REPEAT                 0x2901
#define GL_CLAMP_TO_EDGE          0x812F
#define GL_CLAMP_TO_BORDER        0x812D
#define GL_MIRRORED_REPEAT        0x8370

/* Pixel formats */
#define GL_RED                    0x1903
#define GL_RG                     0x8227
#define GL_RGB                    0x1907
#define GL_RGBA                   0x1908
#define GL_BGR                    0x80E0
#define GL_BGRA                   0x80E1
#define GL_R8                     0x8229
#define GL_RG8                    0x822B
#define GL_RGB8                   0x8051
#define GL_RGBA8                  0x8058
#define GL_SRGB8                  0x8C41
#define GL_SRGB8_ALPHA8           0x8C43
#define GL_DEPTH_COMPONENT        0x1902
#define GL_DEPTH_COMPONENT16      0x81A5
#define GL_DEPTH_COMPONENT24      0x81A6
#define GL_DEPTH_COMPONENT32F     0x8CAC
#define GL_DEPTH24_STENCIL8       0x88F0
#define GL_DEPTH_STENCIL          0x84F9

/* Shaders */
#define GL_VERTEX_SHADER          0x8B31
#define GL_FRAGMENT_SHADER        0x8B30
#define GL_GEOMETRY_SHADER        0x8DD9
#define GL_COMPILE_STATUS         0x8B81
#define GL_LINK_STATUS            0x8B82
#define GL_VALIDATE_STATUS        0x8B83
#define GL_INFO_LOG_LENGTH        0x8B84
#define GL_ACTIVE_UNIFORMS        0x8B86
#define GL_ACTIVE_ATTRIBUTES      0x8B89

/* Framebuffers */
#define GL_FRAMEBUFFER            0x8D40
#define GL_READ_FRAMEBUFFER       0x8CA8
#define GL_DRAW_FRAMEBUFFER       0x8CA9
#define GL_RENDERBUFFER           0x8D41
#define GL_COLOR_ATTACHMENT0      0x8CE0
#define GL_DEPTH_ATTACHMENT       0x8D00
#define GL_STENCIL_ATTACHMENT     0x8D20
#define GL_DEPTH_STENCIL_ATTACHMENT 0x821A
#define GL_FRAMEBUFFER_COMPLETE   0x8CD5

/* Vertex arrays */
#define GL_ARRAY_BUFFER_BINDING    0x8894
#define GL_ELEMENT_ARRAY_BUFFER_BINDING 0x8895

/* Gets */
#define GL_VIEWPORT               0x0BA2
#define GL_SCISSOR_BOX            0x0C10
#define GL_MAX_TEXTURE_SIZE       0x0D33
#define GL_MAJOR_VERSION          0x821B
#define GL_MINOR_VERSION          0x821C
#define GL_VERSION                0x1F02
#define GL_RENDERER               0x1F01
#define GL_VENDOR                 0x1F00
#define GL_EXTENSIONS             0x1F03
#define GL_NUM_EXTENSIONS         0x821D

/* Uniform types */
#define GL_FLOAT_VEC2             0x8B50
#define GL_FLOAT_VEC3             0x8B51
#define GL_FLOAT_VEC4             0x8B52
#define GL_INT_VEC2               0x8B53
#define GL_INT_VEC3               0x8B54
#define GL_INT_VEC4               0x8B55
#define GL_BOOL                   0x8B56
#define GL_FLOAT_MAT2             0x8B5A
#define GL_FLOAT_MAT3             0x8B5B
#define GL_FLOAT_MAT4             0x8B5C
#define GL_SAMPLER_2D             0x8B5E

/* Misc */
#define GL_TEXTURE_MAX_ANISOTROPY 0x84FE
#define GL_UNPACK_ALIGNMENT       0x0CF5
#define GL_PACK_ALIGNMENT         0x0D05

/* ---------- Function pointer typedefs ---------- */

/* Core 1.0+ */
typedef void   (APIENTRYP PFNGLCLEARPROC)(GLbitfield mask);
typedef void   (APIENTRYP PFNGLCLEARCOLORPROC)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
typedef void   (APIENTRYP PFNGLCLEARDEPTHPROC)(GLdouble depth);
typedef void   (APIENTRYP PFNGLVIEWPORTPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void   (APIENTRYP PFNGLSCISSORPROC)(GLint x, GLint y, GLsizei w, GLsizei h);
typedef void   (APIENTRYP PFNGLENABLEPROC)(GLenum cap);
typedef void   (APIENTRYP PFNGLDISABLEPROC)(GLenum cap);
typedef void   (APIENTRYP PFNGLBLENDFUNCPROC)(GLenum sfactor, GLenum dfactor);
typedef void   (APIENTRYP PFNGLBLENDEQUATIONPROC)(GLenum mode);
typedef void   (APIENTRYP PFNGLDEPTHFUNCPROC)(GLenum func);
typedef void   (APIENTRYP PFNGLDEPTHMASKPROC)(GLboolean flag);
typedef void   (APIENTRYP PFNGLCULLFACEPROC)(GLenum mode);
typedef void   (APIENTRYP PFNGLFRONTFACEPROC)(GLenum mode);
typedef void   (APIENTRYP PFNGLPOLYGONMODEPROC)(GLenum face, GLenum mode);
typedef void   (APIENTRYP PFNGLDRAWELEMENTSPROC)(GLenum mode, GLsizei count, GLenum type, const void* indices);
typedef void   (APIENTRYP PFNGLDRAWARRAYSPROC)(GLenum mode, GLint first, GLsizei count);
typedef void   (APIENTRYP PFNGLPIXELSTOREIPROC)(GLenum pname, GLint param);
typedef void   (APIENTRYP PFNGLREADPIXELSPROC)(GLint x, GLint y, GLsizei width, GLsizei height,
                                                GLenum format, GLenum type, void* pixels);
typedef GLenum (APIENTRYP PFNGLGETERRORPROC)(void);
typedef const GLubyte* (APIENTRYP PFNGLGETSTRINGPROC)(GLenum name);
typedef void   (APIENTRYP PFNGLGETINTEGERVPROC)(GLenum pname, GLint* data);
typedef void   (APIENTRYP PFNGLGETFLOATVPROC)(GLenum pname, GLfloat* data);
typedef GLboolean (APIENTRYP PFNGLISENABLEDPROC)(GLenum cap);
typedef void   (APIENTRYP PFNGLLINEWIDTHPROC)(GLfloat width);
typedef void   (APIENTRYP PFNGLPOINTSIZEPROC)(GLfloat size);

/* Textures */
typedef void   (APIENTRYP PFNGLGENTEXTURESPROC)(GLsizei n, GLuint* textures);
typedef void   (APIENTRYP PFNGLDELETETEXTURESPROC)(GLsizei n, const GLuint* textures);
typedef void   (APIENTRYP PFNGLBINDTEXTUREPROC)(GLenum target, GLuint texture);
typedef void   (APIENTRYP PFNGLTEXIMAGE2DPROC)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void* pixels);
typedef void   (APIENTRYP PFNGLTEXSUBIMAGE2DPROC)(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const void* pixels);
typedef void   (APIENTRYP PFNGLTEXPARAMETERIPROC)(GLenum target, GLenum pname, GLint param);
typedef void   (APIENTRYP PFNGLTEXPARAMETERFPROC)(GLenum target, GLenum pname, GLfloat param);
typedef void   (APIENTRYP PFNGLACTIVETEXTUREPROC)(GLenum texture);
typedef void   (APIENTRYP PFNGLGENERATEMIPMAPPROC)(GLenum target);

/* Buffers */
typedef void   (APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void   (APIENTRYP PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);
typedef void   (APIENTRYP PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void   (APIENTRYP PFNGLBUFFERDATAPROC)(GLenum target, GLsizeiptr size, const void* data, GLenum usage);
typedef void   (APIENTRYP PFNGLBUFFERSUBDATAPROC)(GLenum target, GLintptr offset, GLsizeiptr size, const void* data);

/* VAO */
typedef void   (APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void   (APIENTRYP PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);
typedef void   (APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void   (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void   (APIENTRYP PFNGLDISABLEVERTEXATTRIBARRAYPROC)(GLuint index);
typedef void   (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);

/* Shaders */
typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void   (APIENTRYP PFNGLDELETESHADERPROC)(GLuint shader);
typedef void   (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const GLchar *const* string, const GLint* length);
typedef void   (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void   (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* params);
typedef void   (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void   (APIENTRYP PFNGLDELETEPROGRAMPROC)(GLuint program);
typedef void   (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void   (APIENTRYP PFNGLDETACHSHADERPROC)(GLuint program, GLuint shader);
typedef void   (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void   (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);
typedef void   (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* params);
typedef void   (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog);
typedef GLint  (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const GLchar* name);
typedef GLint  (APIENTRYP PFNGLGETATTRIBLOCATIONPROC)(GLuint program, const GLchar* name);
typedef void   (APIENTRYP PFNGLUNIFORM1IPROC)(GLint location, GLint v0);
typedef void   (APIENTRYP PFNGLUNIFORM1FPROC)(GLint location, GLfloat v0);
typedef void   (APIENTRYP PFNGLUNIFORM2FPROC)(GLint location, GLfloat v0, GLfloat v1);
typedef void   (APIENTRYP PFNGLUNIFORM3FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
typedef void   (APIENTRYP PFNGLUNIFORM4FPROC)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
typedef void   (APIENTRYP PFNGLUNIFORM1IVPROC)(GLint location, GLsizei count, const GLint* value);
typedef void   (APIENTRYP PFNGLUNIFORM2FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void   (APIENTRYP PFNGLUNIFORM3FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void   (APIENTRYP PFNGLUNIFORM4FVPROC)(GLint location, GLsizei count, const GLfloat* value);
typedef void   (APIENTRYP PFNGLUNIFORMMATRIX3FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);
typedef void   (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const GLfloat* value);

/* Framebuffers */
typedef void   (APIENTRYP PFNGLGENFRAMEBUFFERSPROC)(GLsizei n, GLuint* framebuffers);
typedef void   (APIENTRYP PFNGLDELETEFRAMEBUFFERSPROC)(GLsizei n, const GLuint* framebuffers);
typedef void   (APIENTRYP PFNGLBINDFRAMEBUFFERPROC)(GLenum target, GLuint framebuffer);
typedef void   (APIENTRYP PFNGLFRAMEBUFFERTEXTURE2DPROC)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
typedef GLenum (APIENTRYP PFNGLCHECKFRAMEBUFFERSTATUSPROC)(GLenum target);

/* Renderbuffers */
typedef void   (APIENTRYP PFNGLGENRENDERBUFFERSPROC)(GLsizei n, GLuint* renderbuffers);
typedef void   (APIENTRYP PFNGLDELETERENDERBUFFERSPROC)(GLsizei n, const GLuint* renderbuffers);
typedef void   (APIENTRYP PFNGLBINDRENDERBUFFERPROC)(GLenum target, GLuint renderbuffer);
typedef void   (APIENTRYP PFNGLRENDERBUFFERSTORAGEPROC)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
typedef void   (APIENTRYP PFNGLFRAMEBUFFERRENDERBUFFERPROC)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);

/* Blit */
typedef void   (APIENTRYP PFNGLBLITFRAMEBUFFERPROC)(GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1, GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1, GLbitfield mask, GLenum filter);

/* String i */
typedef const GLubyte* (APIENTRYP PFNGLGETSTRINGIPROC)(GLenum name, GLuint index);

/* Blend separate */
typedef void   (APIENTRYP PFNGLBLENDFUNCSEPARATEPROC)(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
typedef void   (APIENTRYP PFNGLBLENDEQUATIONSEPARATEPROC)(GLenum modeRGB, GLenum modeAlpha);

/* ---------- Function declarations ---------- */

GLAPI PFNGLCLEARPROC                    glad_glClear;
GLAPI PFNGLCLEARCOLORPROC               glad_glClearColor;
GLAPI PFNGLCLEARDEPTHPROC               glad_glClearDepth;
GLAPI PFNGLVIEWPORTPROC                 glad_glViewport;
GLAPI PFNGLSCISSORPROC                  glad_glScissor;
GLAPI PFNGLENABLEPROC                   glad_glEnable;
GLAPI PFNGLDISABLEPROC                  glad_glDisable;
GLAPI PFNGLBLENDFUNCPROC                glad_glBlendFunc;
GLAPI PFNGLBLENDEQUATIONPROC            glad_glBlendEquation;
GLAPI PFNGLDEPTHFUNCPROC                glad_glDepthFunc;
GLAPI PFNGLDEPTHMASKPROC                glad_glDepthMask;
GLAPI PFNGLCULLFACEPROC                 glad_glCullFace;
GLAPI PFNGLFRONTFACEPROC                glad_glFrontFace;
GLAPI PFNGLPOLYGONMODEPROC              glad_glPolygonMode;
GLAPI PFNGLDRAWELEMENTSPROC             glad_glDrawElements;
GLAPI PFNGLDRAWARRAYSPROC               glad_glDrawArrays;
GLAPI PFNGLPIXELSTOREIPROC              glad_glPixelStorei;
GLAPI PFNGLREADPIXELSPROC               glad_glReadPixels;
GLAPI PFNGLGETERRORPROC                 glad_glGetError;
GLAPI PFNGLGETSTRINGPROC                glad_glGetString;
GLAPI PFNGLGETINTEGERVPROC              glad_glGetIntegerv;
GLAPI PFNGLGETFLOATVPROC                glad_glGetFloatv;
GLAPI PFNGLISENABLEDPROC                glad_glIsEnabled;
GLAPI PFNGLLINEWIDTHPROC                glad_glLineWidth;
GLAPI PFNGLPOINTSIZEPROC                glad_glPointSize;

GLAPI PFNGLGENTEXTURESPROC              glad_glGenTextures;
GLAPI PFNGLDELETETEXTURESPROC           glad_glDeleteTextures;
GLAPI PFNGLBINDTEXTUREPROC              glad_glBindTexture;
GLAPI PFNGLTEXIMAGE2DPROC               glad_glTexImage2D;
GLAPI PFNGLTEXSUBIMAGE2DPROC            glad_glTexSubImage2D;
GLAPI PFNGLTEXPARAMETERIPROC            glad_glTexParameteri;
GLAPI PFNGLTEXPARAMETERFPROC            glad_glTexParameterf;
GLAPI PFNGLACTIVETEXTUREPROC            glad_glActiveTexture;
GLAPI PFNGLGENERATEMIPMAPPROC           glad_glGenerateMipmap;

GLAPI PFNGLGENBUFFERSPROC               glad_glGenBuffers;
GLAPI PFNGLDELETEBUFFERSPROC            glad_glDeleteBuffers;
GLAPI PFNGLBINDBUFFERPROC               glad_glBindBuffer;
GLAPI PFNGLBUFFERDATAPROC               glad_glBufferData;
GLAPI PFNGLBUFFERSUBDATAPROC            glad_glBufferSubData;
typedef void (APIENTRYP PFNGLBINDBUFFERBASEPROC)(GLenum target, GLuint index, GLuint buffer);
GLAPI PFNGLBINDBUFFERBASEPROC           glad_glBindBufferBase;
typedef void (APIENTRYP PFNGLBINDBUFFERRANGEPROC)(GLenum target, GLuint index, GLuint buffer,
                                                  GLintptr offset, GLsizeiptr size);
GLAPI PFNGLBINDBUFFERRANGEPROC          glad_glBindBufferRange;
typedef GLuint (APIENTRYP PFNGLGETUNIFORMBLOCKINDEXPROC)(GLuint program, const GLchar* uniformBlockName);
GLAPI PFNGLGETUNIFORMBLOCKINDEXPROC     glad_glGetUniformBlockIndex;
typedef void (APIENTRYP PFNGLUNIFORMBLOCKBINDINGPROC)(GLuint program, GLuint uniformBlockIndex,
                                                      GLuint uniformBlockBinding);
GLAPI PFNGLUNIFORMBLOCKBINDINGPROC      glad_glUniformBlockBinding;
typedef void (APIENTRYP PFNGLGETACTIVEUNIFORMBLOCKIVPROC)(GLuint program, GLuint uniformBlockIndex,
                                                          GLenum pname, GLint* params);
GLAPI PFNGLGETACTIVEUNIFORMBLOCKIVPROC  glad_glGetActiveUniformBlockiv;

GLAPI PFNGLGENVERTEXARRAYSPROC          glad_glGenVertexArrays;
GLAPI PFNGLDELETEVERTEXARRAYSPROC       glad_glDeleteVertexArrays;
GLAPI PFNGLBINDVERTEXARRAYPROC          glad_glBindVertexArray;
GLAPI PFNGLENABLEVERTEXATTRIBARRAYPROC  glad_glEnableVertexAttribArray;
GLAPI PFNGLDISABLEVERTEXATTRIBARRAYPROC glad_glDisableVertexAttribArray;
GLAPI PFNGLVERTEXATTRIBPOINTERPROC     glad_glVertexAttribPointer;

GLAPI PFNGLCREATESHADERPROC             glad_glCreateShader;
GLAPI PFNGLDELETESHADERPROC             glad_glDeleteShader;
GLAPI PFNGLSHADERSOURCEPROC             glad_glShaderSource;
GLAPI PFNGLCOMPILESHADERPROC            glad_glCompileShader;
GLAPI PFNGLGETSHADERIVPROC              glad_glGetShaderiv;
GLAPI PFNGLGETSHADERINFOLOGPROC         glad_glGetShaderInfoLog;
GLAPI PFNGLCREATEPROGRAMPROC            glad_glCreateProgram;
GLAPI PFNGLDELETEPROGRAMPROC            glad_glDeleteProgram;
GLAPI PFNGLATTACHSHADERPROC             glad_glAttachShader;
GLAPI PFNGLDETACHSHADERPROC             glad_glDetachShader;
GLAPI PFNGLLINKPROGRAMPROC              glad_glLinkProgram;
GLAPI PFNGLUSEPROGRAMPROC               glad_glUseProgram;
GLAPI PFNGLGETPROGRAMIVPROC             glad_glGetProgramiv;
GLAPI PFNGLGETPROGRAMINFOLOGPROC        glad_glGetProgramInfoLog;
GLAPI PFNGLGETUNIFORMLOCATIONPROC       glad_glGetUniformLocation;
GLAPI PFNGLGETATTRIBLOCATIONPROC        glad_glGetAttribLocation;
GLAPI PFNGLUNIFORM1IPROC                glad_glUniform1i;
GLAPI PFNGLUNIFORM1FPROC                glad_glUniform1f;
GLAPI PFNGLUNIFORM2FPROC                glad_glUniform2f;
GLAPI PFNGLUNIFORM3FPROC                glad_glUniform3f;
GLAPI PFNGLUNIFORM4FPROC                glad_glUniform4f;
GLAPI PFNGLUNIFORM1IVPROC               glad_glUniform1iv;
GLAPI PFNGLUNIFORM2FVPROC               glad_glUniform2fv;
GLAPI PFNGLUNIFORM3FVPROC               glad_glUniform3fv;
GLAPI PFNGLUNIFORM4FVPROC               glad_glUniform4fv;
GLAPI PFNGLUNIFORMMATRIX3FVPROC         glad_glUniformMatrix3fv;
GLAPI PFNGLUNIFORMMATRIX4FVPROC         glad_glUniformMatrix4fv;

GLAPI PFNGLGENFRAMEBUFFERSPROC          glad_glGenFramebuffers;
GLAPI PFNGLDELETEFRAMEBUFFERSPROC       glad_glDeleteFramebuffers;
GLAPI PFNGLBINDFRAMEBUFFERPROC          glad_glBindFramebuffer;
GLAPI PFNGLFRAMEBUFFERTEXTURE2DPROC     glad_glFramebufferTexture2D;
GLAPI PFNGLCHECKFRAMEBUFFERSTATUSPROC   glad_glCheckFramebufferStatus;

GLAPI PFNGLGENRENDERBUFFERSPROC         glad_glGenRenderbuffers;
GLAPI PFNGLDELETERENDERBUFFERSPROC      glad_glDeleteRenderbuffers;
GLAPI PFNGLBINDRENDERBUFFERPROC         glad_glBindRenderbuffer;
GLAPI PFNGLRENDERBUFFERSTORAGEPROC      glad_glRenderbufferStorage;
GLAPI PFNGLFRAMEBUFFERRENDERBUFFERPROC  glad_glFramebufferRenderbuffer;

GLAPI PFNGLBLITFRAMEBUFFERPROC          glad_glBlitFramebuffer;
GLAPI PFNGLGETSTRINGIPROC               glad_glGetStringi;
GLAPI PFNGLBLENDFUNCSEPARATEPROC        glad_glBlendFuncSeparate;
GLAPI PFNGLBLENDEQUATIONSEPARATEPROC    glad_glBlendEquationSeparate;

/* ---------- Macros mapping glXxx to glad_glXxx ---------- */
#define glClear                     glad_glClear
#define glClearColor                glad_glClearColor
#define glClearDepth                glad_glClearDepth
#define glViewport                  glad_glViewport
#define glScissor                   glad_glScissor
#define glEnable                    glad_glEnable
#define glDisable                   glad_glDisable
#define glBlendFunc                 glad_glBlendFunc
#define glBlendEquation             glad_glBlendEquation
#define glDepthFunc                 glad_glDepthFunc
#define glDepthMask                 glad_glDepthMask
#define glCullFace                  glad_glCullFace
#define glFrontFace                 glad_glFrontFace
#define glPolygonMode               glad_glPolygonMode
#define glDrawElements              glad_glDrawElements
#define glDrawArrays                glad_glDrawArrays
#define glPixelStorei               glad_glPixelStorei
#define glReadPixels                glad_glReadPixels
#define glGetError                  glad_glGetError
#define glGetString                 glad_glGetString
#define glGetIntegerv               glad_glGetIntegerv
#define glGetFloatv                 glad_glGetFloatv
#define glIsEnabled                 glad_glIsEnabled
#define glLineWidth                 glad_glLineWidth
#define glPointSize                 glad_glPointSize

#define glGenTextures               glad_glGenTextures
#define glDeleteTextures            glad_glDeleteTextures
#define glBindTexture               glad_glBindTexture
#define glTexImage2D                glad_glTexImage2D
#define glTexSubImage2D             glad_glTexSubImage2D
#define glTexParameteri             glad_glTexParameteri
#define glTexParameterf             glad_glTexParameterf
#define glActiveTexture             glad_glActiveTexture
#define glGenerateMipmap            glad_glGenerateMipmap

#define glGenBuffers                glad_glGenBuffers
#define glDeleteBuffers             glad_glDeleteBuffers
#define glBindBuffer                glad_glBindBuffer
#define glBufferData                glad_glBufferData
#define glBufferSubData             glad_glBufferSubData
#define glBindBufferBase            glad_glBindBufferBase
#define glBindBufferRange           glad_glBindBufferRange
#define glGetUniformBlockIndex      glad_glGetUniformBlockIndex
#define glUniformBlockBinding       glad_glUniformBlockBinding
#define glGetActiveUniformBlockiv   glad_glGetActiveUniformBlockiv

#define glGenVertexArrays           glad_glGenVertexArrays
#define glDeleteVertexArrays        glad_glDeleteVertexArrays
#define glBindVertexArray           glad_glBindVertexArray
#define glEnableVertexAttribArray   glad_glEnableVertexAttribArray
#define glDisableVertexAttribArray  glad_glDisableVertexAttribArray
#define glVertexAttribPointer       glad_glVertexAttribPointer

#define glCreateShader              glad_glCreateShader
#define glDeleteShader              glad_glDeleteShader
#define glShaderSource              glad_glShaderSource
#define glCompileShader             glad_glCompileShader
#define glGetShaderiv               glad_glGetShaderiv
#define glGetShaderInfoLog          glad_glGetShaderInfoLog
#define glCreateProgram             glad_glCreateProgram
#define glDeleteProgram             glad_glDeleteProgram
#define glAttachShader              glad_glAttachShader
#define glDetachShader              glad_glDetachShader
#define glLinkProgram               glad_glLinkProgram
#define glUseProgram                glad_glUseProgram
#define glGetProgramiv              glad_glGetProgramiv
#define glGetProgramInfoLog         glad_glGetProgramInfoLog
#define glGetUniformLocation        glad_glGetUniformLocation
#define glGetAttribLocation         glad_glGetAttribLocation
#define glUniform1i                 glad_glUniform1i
#define glUniform1f                 glad_glUniform1f
#define glUniform2f                 glad_glUniform2f
#define glUniform3f                 glad_glUniform3f
#define glUniform4f                 glad_glUniform4f
#define glUniform1iv                glad_glUniform1iv
#define glUniform2fv                glad_glUniform2fv
#define glUniform3fv                glad_glUniform3fv
#define glUniform4fv                glad_glUniform4fv
#define glUniformMatrix3fv          glad_glUniformMatrix3fv
#define glUniformMatrix4fv          glad_glUniformMatrix4fv

#define glGenFramebuffers           glad_glGenFramebuffers
#define glDeleteFramebuffers        glad_glDeleteFramebuffers
#define glBindFramebuffer           glad_glBindFramebuffer
#define glFramebufferTexture2D      glad_glFramebufferTexture2D
#define glCheckFramebufferStatus    glad_glCheckFramebufferStatus

#define glGenRenderbuffers          glad_glGenRenderbuffers
#define glDeleteRenderbuffers       glad_glDeleteRenderbuffers
#define glBindRenderbuffer          glad_glBindRenderbuffer
#define glRenderbufferStorage       glad_glRenderbufferStorage
#define glFramebufferRenderbuffer   glad_glFramebufferRenderbuffer

#define glBlitFramebuffer           glad_glBlitFramebuffer
#define glGetStringi                glad_glGetStringi
#define glBlendFuncSeparate         glad_glBlendFuncSeparate
#define glBlendEquationSeparate     glad_glBlendEquationSeparate

/* ---------- Loader function ---------- */
typedef void* (*GLADloadfunc)(const char* name);
int gladLoadGL(GLADloadfunc load);

#ifdef __cplusplus
}
#endif

#endif /* GLAD_GL_H_ */
