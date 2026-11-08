/* phe_gl_compat.h -- force-included when compiling GL users against the Symbian GLES/EGL headers (Khronos 2008 revisions): they say __declspec(dllimport) for the export marker and lack a few later tokens. BSD-2-Clause (Phorepoc). */
#define __declspec(x)
#define PHE_GL_COMPAT 1
#ifndef GL_BGRA_EXT
#define GL_BGRA_EXT 0x80E1
#endif
#ifndef EGL_SYNC_FENCE_KHR
#define EGL_SYNC_FENCE_KHR 0x30F9
#endif
