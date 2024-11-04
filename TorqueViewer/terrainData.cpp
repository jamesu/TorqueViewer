#include "terrainData.h"

slm::vec2 TerrainBlock::MaterialMap::sMatCoords[4] = {
  // 0
  slm::vec2(0.0f, 0.0f), // tl
  slm::vec2(1.0f, 0.0f), // tr
  // 1
  slm::vec2(1.0f, 1.0f), // br
  slm::vec2(0.0f, 1.0f), // bl
};
