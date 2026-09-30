// The ShaderDLL singleton and CreateInterface exports are supplied by shaderlib.
// This translation unit anchors the dedicated DX12 project without importing the
// legacy stdshader source tree; shader registrations are added by slice fragments.
#include "shaderlib/ShaderDLL.h"
