/* SPDX-License-Identifier: GPL-3.0-only */

/* The gl.h entries the host's table lacks, on the entries it has. GL ES 1.1
 * defines them as the same operations with converted arguments: a byte
 * colour maps to 0..1, an integer colour component to (2 i + 1) / (2^32 - 1),
 * an enum passes through, and every texture parameter is a scalar, so a
 * vector form reads its first element. glPointSizePointerOES has no host
 * entry at all. */

#include <GLES/gl.h>

void glColor4ub(GLubyte red, GLubyte green, GLubyte blue, GLubyte alpha)
{
    glColor4f(red / 255.0f, green / 255.0f, blue / 255.0f, alpha / 255.0f);
}

void glTexEnvi(GLenum target, GLenum pname, GLint param)
{
    glTexEnvf(target, pname, (GLfloat)param);
}

void glTexEnviv(GLenum target, GLenum pname, const GLint *params)
{
    if (pname != GL_TEXTURE_ENV_COLOR) {
        glTexEnvf(target, pname, (GLfloat)params[0]);
        return;
    }
    GLfloat color[4];
    for (unsigned i = 0; i < 4; ++i)
        color[i] = (2.0f * (GLfloat)params[i] + 1.0f) / 4294967295.0f;
    glTexEnvfv(target, pname, color);
}

void glTexParameterfv(GLenum target, GLenum pname, const GLfloat *params)
{
    glTexParameterf(target, pname, params[0]);
}

void glTexParameteriv(GLenum target, GLenum pname, const GLint *params)
{
    glTexParameteri(target, pname, params[0]);
}

void glTexParameterxv(GLenum target, GLenum pname, const GLfixed *params)
{
    glTexParameterx(target, pname, params[0]);
}
