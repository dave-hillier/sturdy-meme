#pragma once

#include "CatmullClarkMesh.h"
#include <string>

namespace OBJLoader {
    CatmullClarkMesh loadQuadMesh(const std::string& path);
}
