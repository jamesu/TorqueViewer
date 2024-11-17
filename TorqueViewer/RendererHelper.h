// API

#include <stdint.h>
#include <slm/slmath.h>

class Bitmap;
class Palette;
class SDL_Window;
class SDL_Renderer;

enum CustomTextureFormat
{
   CustomTexture_Float,
   CustomTexture_RG8,
   CustomTexture_RGBA8,
   CustomTexture_LM16,
   CustomTexture_TerrainSquare
};

extern int GFXSetup(SDL_Window* window, SDL_Renderer* renderer);
extern void GFXTeardown();
extern void GFXTestRender(slm::vec3 pos);
extern void GFXPollEvents();
extern void GFXResetSwapChain();

extern bool GFXBeginFrame();
extern void GFXEndFrame();
extern void GFXHandleResize();

//
extern int32_t GFXLoadCustomTexture(CustomTextureFormat fmt, uint32_t width, uint32_t height, void* data);
extern void GFXUpdateCustomTextureAligned(int32_t texID, void* texData);
//
extern int32_t GFXLoadTexture(Bitmap* bmp, Palette*pal);
extern int32_t GFXLoadTextureSet(uint32_t numBitmaps, Bitmap** bmps, Palette*pal);
extern void GFXDeleteTexture(int32_t texID);
//
extern void GFXLoadModelData(uint32_t modelId, void* verts, void* texverts, void* inds, void* skin, uint32_t numVerts, uint32_t numTexVerts, uint32_t numInds);
extern void GFXClearModelData(uint32_t modelId);
extern void GFXSetModelViewProjection(slm::mat4 &model, slm::mat4 &view, slm::mat4 &proj, uint32_t flags=0);
extern void GFXSetLightPos(slm::vec3 pos, slm::vec4 ambient);
//
extern void GFXSetTSMaterialResources(uint32_t tsGroupID, int32_t diffuseTexID, int32_t emapAlphaTexID, int32_t emapTexID, int32_t dmapID);
extern void GFXSetITRMaterialResources(uint32_t itrGroupID, int32_t baseTexID, int32_t emapTexID, int32_t lightmapTexID);
extern void GFXBeginTSModelPipelineState(ModelPipelineState state, uint32_t tsGroupID, float testVal, bool depthPeel, bool swapDepth);
extern void GFXBeginITRModelPipelineState(ModelPipelineState state, uint32_t itrGroupID, float testVal, bool depthPeel, bool swapDepth);
extern void GFXSetTSPipelineProps(uint32_t matFrame, uint32_t transformOffset, slm::vec4 texGenS, slm::vec4 texGenT);
//
extern void GFXSetModelVerts(uint32_t modelId, uint32_t vertOffset, uint32_t texOffset, uint32_t indexOffset);
extern void GFXDrawModelVerts(uint32_t numVerts, uint32_t startVerts);
extern void GFXDrawModelPrims(uint32_t numVerts, uint32_t numInds, uint32_t startInds, uint32_t startVerts);
//
extern void GFXBeginLinePipelineState();
extern void GFXDrawLine(slm::vec3 start, slm::vec3 end, slm::vec4 color, float width);
//
extern void GFXSetTerrainResources(uint32_t terrainID, int32_t matTexGroupID, int32_t heightMapTexID, int32_t gridMapTexID, int32_t lightmapTexGroupID);
extern void GFXBeginTerrainPipelineState(TerrainPipelineState state, uint32_t terrainID, float squareSize, float gridX, float gridY, const slm::vec4* matCoords);

