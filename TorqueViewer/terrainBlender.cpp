#include "terrainBlender.h"
#include "CommonData.h"

static bool smUseComputePipeline = false;

#if 0
TerrainBlender::TerrainBlender(TerrainBlock* block) : mCurrentBlock(block)
{
}

TerrainBlender::~TerrainBlender()
{
   
}

TerrainGpuBlender::TerrainGpuBlender(TerrainBlock* block) : TerrainBlender(block), mGpuReady(false), mFrameReady(false)
{
   mTexturesToBlend = 0;
   for (U32 i=0; i<TerrainBlock::MaterialGroups; i++) {
      mGpuTextures[i] = NULL;
      mGpuAlphaTextures[i] = NULL;
   }
}

TerrainGpuBlender::~TerrainGpuBlender()
{
   purge();
}

void TerrainGpuBlender::newFrame()
{
   mFrameReady = false;
}

void TerrainGpuBlender::purge()
{
   if (!mGpuReady)
      return;
   
   mGpuReady = false;
}

void TerrainGpuBlender::reset()
{
   //-------------------------
   mGpuReady = true;
}

void TerrainGpuBlender::setSourceBitmap( int idx, GBitmap* bmp, const U8 *alpha )
{
   char name[256];
   dSprintf(name, sizeof(name), "$terMat%u", idx);
   mGpuTextures[idx] = TextureHandle(name, bmp, false);
   mTexAlpha[idx] = alpha;
   mTexturesToBlend = idx >= mTexturesToBlend ? idx+1 : mTexturesToBlend;
}

void TerrainGpuBlender::blend( int x, int y, int level, const TextureHandle& lightmapTex, TextureHandle &destTexture )
{
   const int squaresPerTargetEdge(1 << level);
   
   U32 dm = dglPushGroupMarker("TerrainBlend");
   
   U32 squaresInBlockEdge = 1<<level;
   U32 lightmapPixelsPerBlock = TerrainBlock::LightmapSize;
   
   if (!mGpuReady)
   {
      reset();
   }
   
   if (!mFrameReady)
   {
      // Update alpha textures
      /*for (int i=0; i<mTexturesToBlend; i++)
      {
         GBitmap* bmp = new GBitmap(256, 256, false, GBitmap::Alpha);
         memcpy(bmp->getWritableBits(), mTexAlpha[i], 256*256);
         mGpuAlphaTextures[i].setFilterNearest();
         mGpuAlphaTextures[i].set(NULL, bmp, BitmapKeepTexture, false);
      }*/
      
      mFrameReady = true;
   }
   
   // Fallback: raster pipeline
   DGLTerrainRasterBlendPipeline* rasterPipeline = (DGLTerrainRasterBlendPipeline*)dglGetMainDevice()->bindCustomPipeline(DGL_CUSTOM_TERRAIN_RASTER_BLEND_PIPELINE, false);
   if (rasterPipeline)
   {
      rasterPipeline->setLightmapGroup(0, lightmapTex.getGUITextureGroupIDX());
      rasterPipeline->setTerrain(0, mCurrentBlock);
      
      TextureGroupObject tgo = {};
      tgo.mKlass = DGL_TG_TEX8;
      
      for ( int i = 0; i < mTexturesToBlend; i++ )
      {
         tgo.mTexs[i] = mGpuTextures[i].value;
         rasterPipeline->setAlphaMap(0, i, mTexAlpha[i]);
      }
      
      // Blank everything else
      for ( int i = mTexturesToBlend; i<8; i++ )
      {
         tgo.mTexs[i] = TextureManager::smBlankTexture.value;
      }
      
      mTextureGroup.setObject(&tgo);
      
      rasterPipeline->setTextureGroup(0, mTextureGroup.getGroupIDX());
      
      rasterPipeline->blend(x, y, level, destTexture.getGLName());
   }

   
   dglPopGroupMarker(dm);
}

#endif

