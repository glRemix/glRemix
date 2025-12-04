#include "gl_loader.h"

#include <GL/gl.h>

// This .inl marks the functions for export
#include "generated/gl_export_aliases.inl"
#include "export_macros.h"

// This .inl defines the wrappers for each OpenGL function using the macros in export_macros.h
// The wrapper tries to call the hook if it exists, otherwise reports missing function
#include "generated/gl_wrappers.inl"
#include "undef_export_macros.h"
