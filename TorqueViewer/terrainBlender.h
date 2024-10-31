#ifndef _TERRAIN_BLENDER_H_
#define _TERRAIN_BLENDER_H_

#include "CommonData.h"

class TerrainBlock;

struct TerrainBlock
{
   static const uint32_t MaterialGroups = 8;
};

// mango - this covers the core metrics a blender should require
template<uint32_t BlendMipLevel> struct BlenderMetrics
{
   // Primary Manifest Values
   static const uint32_t kPRIMARY_BLEND_LEVEL = 7;
   static const uint32_t kTEXELS_PER_SOURCE_EDGE_LOG2 =       8;
   static const uint32_t kTEXELS_PER_TARGET_EDGE_LOG2 =       kPRIMARY_BLEND_LEVEL;
   static const uint32_t kLUMELS_PER_TILE_EDGE_LOG2 =         9;
   static const uint32_t kSQUARES_PER_TILE_EDGE_LOG2 =        8;
   static const uint32_t kMAXIMUM_TEXTURES =                  4;
   static const uint32_t kSQUARES_PER_MIPMAP_EDGE_LOG2 =      (10 - kTEXELS_PER_TARGET_EDGE_LOG2);

   // Derived Manifest Values
   static const uint32_t kMAX_TEXELS_PER_SQUARE_EDGE_LOG2 =   (kTEXELS_PER_SOURCE_EDGE_LOG2 - kSQUARES_PER_MIPMAP_EDGE_LOG2);
   static const uint32_t kLUMELS_PER_SQUARE_EDGE_LOG2 =       (kLUMELS_PER_TILE_EDGE_LOG2 - kSQUARES_PER_TILE_EDGE_LOG2);

   // Texels
   static const uint32_t kMAX_TEXELS_PER_SQUARE_EDGE =        (1 << kMAX_TEXELS_PER_SQUARE_EDGE_LOG2);
   static const uint32_t kMAX_TEXELS_PER_SQUARE_LOG2 =        (kMAX_TEXELS_PER_SQUARE_EDGE_LOG2 << 1);
   static const uint32_t kMAX_TEXELS_PER_SQUARE =             (kMAX_TEXELS_PER_SQUARE_EDGE*kMAX_TEXELS_PER_SQUARE_EDGE);
   static const uint32_t _kTEXELS_PER_SOURCE_EDGE =            (1 << kTEXELS_PER_SOURCE_EDGE_LOG2);
   static const uint32_t kTEXELS_PER_TARGET_EDGE =            (1 << kTEXELS_PER_TARGET_EDGE_LOG2);
   static const uint32_t kTEXELS_PER_SOURCE_BMP =             (_kTEXELS_PER_SOURCE_EDGE * _kTEXELS_PER_SOURCE_EDGE);
   
   // Lumels
   static const uint32_t kLUMELS_PER_SQUARE_EDGE =            (1 << kLUMELS_PER_SQUARE_EDGE_LOG2);
   static const uint32_t kLUMELS_PER_TILE_EDGE =              (1 << kLUMELS_PER_TILE_EDGE_LOG2);
   static const uint32_t kLUMELS_PER_TILE_EDGE_MASK =         (kLUMELS_PER_TILE_EDGE - 1);
   
   // Squares
   static const uint32_t _kSQUARES_PER_TILE_EDGE =             (1 << kSQUARES_PER_TILE_EDGE_LOG2);
   static const uint32_t kSQUARES_PER_TILE_EDGE_MASK =        (_kSQUARES_PER_TILE_EDGE - 1);
   static const uint32_t kSQUARES_PER_MIPMAP_EDGE =           (1 << kSQUARES_PER_MIPMAP_EDGE_LOG2);
   static const uint32_t kSQUARES_PER_MIPMAP_EDGE_MASK =      (kSQUARES_PER_MIPMAP_EDGE-1);
};

/**
 The terrain blender generates textures for each generated terrain patch, using an alpha map combined with
 a set of textures, finally blended with the main lightmap.
 
 Each terrain patch will contain 4 or more squares, depending on the level of detail selected.
 Every patch is 128x128 texels in size, meaning at most a square will be 32x32 texels in size and
 at least 2x2 texels.
 
 Each square is blended such that the alpha values of each texture used smoothly transitions
 between the corresponding values in the neighbouring forward squares.
**/
class TerrainBlender
{
public:
   //inline uint32_t getGridFlags(uint32_t x, uint32_t y) { return mCurrentBlock->findSquare(0, Point2I(x,y))->flags; }
   typedef BlenderMetrics<7> Metrics;

   /// Constructor
   ///
   TerrainBlender(TerrainBlock* block);
   virtual ~TerrainBlender();
   
   TerrainBlock* mCurrentBlock;
   
   virtual void newFrame() = 0;
   
   virtual void purge() = 0; ///< Unloads GPU elements
   virtual void reset() = 0; ///< Resets GPU state

   virtual void blend( int x, int y, int level, const uint32_t lightmapTex, uint32_t destTexture ) = 0;
   virtual void setSourceBitmap( int idx, Bitmap* bmp, const uint8_t *alpha ) = 0;
};

/**
 OpenGL variant of the basic Blender
 **/
class TerrainGpuBlender : public TerrainBlender
{
public:
   /// @name GPU State
   /// {
   bool mGpuReady;
   bool mFrameReady;

   uint32_t mTexturesToBlend;
   //TextureGroupHandle mTextureGroup;
   uint32_t mGpuTextures[TerrainBlock::MaterialGroups];
   uint32_t mGpuAlphaTextures[TerrainBlock::MaterialGroups];
   /// }
   
   TerrainGpuBlender(TerrainBlock* block);
   virtual ~TerrainGpuBlender();
   
   const uint8_t* mTexAlpha[TerrainBlock::MaterialGroups];
   
   virtual void newFrame();
   
   virtual void purge(); ///< Unloads GPU elements
   virtual void reset(); ///< Resets GPU state
   
   virtual void blend(int x, int y, int level, const uint32_t lightmapTex, uint32_t destTexture);
   virtual void setSourceBitmap(int idx, Bitmap* bmp, const uint8_t *alpha);
};

#endif // _TERRAIN_BLENDER_H_
