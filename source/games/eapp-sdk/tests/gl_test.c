/* SPDX-License-Identifier: GPL-3.0-only */

/* The gl.h entries gl.c supplies on the host's, with stand-ins for the
 * host's: byte and integer colours scale, enums pass through, vector texture
 * parameters reach the scalar entry. On its own because gl.h's buffer types
 * differ from the symbols header's on a 64-bit host. */

#include <GLES/gl.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* The host's GL entries gl.c builds on: each records its arguments. */
static GLenum gl_target, gl_pname;
static GLfloat gl_floats[4];
static GLint gl_int;
static GLfixed gl_fixed;

void glColor4f(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    gl_floats[0] = red;
    gl_floats[1] = green;
    gl_floats[2] = blue;
    gl_floats[3] = alpha;
}

void glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
    gl_target = target;
    gl_pname = pname;
    gl_floats[0] = param;
}

void glTexEnvfv(GLenum target, GLenum pname, const GLfloat *params)
{
    gl_target = target;
    gl_pname = pname;
    memcpy(gl_floats, params, sizeof gl_floats);
}

void glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
    gl_target = target;
    gl_pname = pname;
    gl_floats[0] = param;
}

void glTexParameteri(GLenum target, GLenum pname, GLint param)
{
    gl_target = target;
    gl_pname = pname;
    gl_int = param;
}

void glTexParameterx(GLenum target, GLenum pname, GLfixed param)
{
    gl_target = target;
    gl_pname = pname;
    gl_fixed = param;
}

static void check_gl(void)
{
    glColor4ub(255, 0, 51, 128);
    assert(gl_floats[0] == 1.0f && gl_floats[1] == 0.0f && gl_floats[2] == 0.2f &&
           gl_floats[3] > 0.50f && gl_floats[3] < 0.51f);
    glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    assert(gl_target == GL_TEXTURE_ENV && gl_pname == GL_TEXTURE_ENV_MODE &&
           gl_floats[0] == (GLfloat)GL_MODULATE);
    static const GLint mode[1] = {GL_DECAL};
    glTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, mode);
    assert(gl_floats[0] == (GLfloat)GL_DECAL);
    static const GLint color[4] = {0x7fffffff, 0, -0x7fffffff - 1, 0x3fffffff};
    glTexEnviv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);
    assert(gl_pname == GL_TEXTURE_ENV_COLOR && gl_floats[0] == 1.0f &&
           gl_floats[1] > 0.0f && gl_floats[1] < 1e-9f && gl_floats[2] == -1.0f &&
           gl_floats[3] > 0.49f && gl_floats[3] < 0.51f);
    static const GLfloat filter_f[2] = {GL_LINEAR, 0};
    static const GLint filter_i[2] = {GL_NEAREST, 0};
    static const GLfixed wrap_x[2] = {GL_REPEAT, 0};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter_f);
    assert(gl_target == GL_TEXTURE_2D && gl_pname == GL_TEXTURE_MIN_FILTER &&
           gl_floats[0] == (GLfloat)GL_LINEAR);
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter_i);
    assert(gl_pname == GL_TEXTURE_MAG_FILTER && gl_int == GL_NEAREST);
    glTexParameterxv(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap_x);
    assert(gl_pname == GL_TEXTURE_WRAP_S && gl_fixed == GL_REPEAT);
}

int main(void)
{
    check_gl();
    printf("GL forwarders pass\n");
    return 0;
}
