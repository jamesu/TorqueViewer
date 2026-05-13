//-----------------------------------------------------------------------------
// Copyright (c) 2018-2024 James S Urquhart.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.
//-----------------------------------------------------------------------------

#include <stdint.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <strings.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <numeric>

#include "imgui.h"
#include "imgui_impl_sdl3.h"

#ifndef PATH_MAX
#define PATH_MAX        4096
#endif

#ifndef NO_BOOST
#include <boost/filesystem.hpp>
namespace fs = boost::filesystem;
#else
#include <filesystem>
namespace fs = std::filesystem;
#endif

#include "CommonData.h"
#include "shapeData.h"
#include "CommonShaderTypes.h"
#include "RendererHelper.h"

// The max number of command buffers in flight
static const uint32_t TVMaxBuffersInFlight = 3;

// Run of the mill quaternion interpolator
slm::quat CompatInterpolate( slm::quat const & q1,
                            slm::quat const & q2, float t )
{
   // calculate the cosine of the angle
   double cosOmega = q1.x * q2.x + q1.y * q2.y + q1.z * q2.z + q1.w * q2.w; // i.e. dot
   
   // adjust signs if necessary
   float sign2;
   if ( cosOmega < 0.0 )
   {
      cosOmega = -cosOmega;
      sign2 = -1.0f;
   }
   else
      sign2 = 1.0f;
   
   // calculate interpolating coeffs
   double scale1, scale2;
   if ( (1.0 - cosOmega) > 0.00001 )
   {
      // standard case
      double omega = acos(cosOmega);
      double sinOmega = sin(omega);
      scale1 = sin((1.0 - t) * omega) / sinOmega;
      scale2 = sign2 * sin(t * omega) / sinOmega;
   }
   else
   {
      // if quats are very close, just do linear interpolation
      scale1 = 1.0 - t;
      scale2 = sign2 * t;
   }
   
   // actually do the interpolation
   return slm::quat(float(scale1 * q1.x + scale2 * q2.x),
                    float(scale1 * q1.y + scale2 * q2.y),
                    float(scale1 * q1.z + scale2 * q2.z),
                    float(scale1 * q1.w + scale2 * q2.w));
}

#include "encodedNormals.h"

void CompatQuatSetMatrix(const slm::quat rot, slm::mat4 &outMat)
{
   if( rot.x*rot.x + rot.y*rot.y + rot.z*rot.z < 10E-20f)
   {
      outMat = slm::mat4(1);
      return;
   }
   
   float xs = rot.x * 2.0f;
   float ys = rot.y * 2.0f;
   float zs = rot.z * 2.0f;
   float wx = rot.w * xs;
   float wy = rot.w * ys;
   float wz = rot.w * zs;
   float xx = rot.x * xs;
   float xy = rot.x * ys;
   float xz = rot.x * zs;
   float yy = rot.y * ys;
   float yz = rot.y * zs;
   float zz = rot.z * zs;
   
   // r,c
   outMat[0] = slm::vec4(1.0f - (yy + zz),
                         xy - wz,
                         xz + wy,
                         0.0f);
   
   outMat[1] = slm::vec4(xy + wz,
                         1.0f - (xx + zz),
                         yz - wx,
                         0.0f);
   
   outMat[2] = slm::vec4(xz - wy,
                         yz + wx,
                         1.0f - (xx + yy),
                         0.0f);
   
   //outMat = slm::transpose(outMat);
   outMat[3] = slm::vec4(0.0f,0.0f,0.0f,1.0f);
}


#include "CommonData.h"


void ConsolePersistObject::initStatics()
{
}


class GenericViewer
{
public:
   static bool openTextureStreamWithFallback(ResManager* resourceManager, const std::string& resourceFilename, int32_t resourceMount, const char* filename, MemRStream& outStream, std::string* outResolvedPath = NULL)
   {
      fs::path texturePath(filename);
      fs::path textureStem = texturePath.stem();

      std::vector<std::string> extensionsToTry;
      std::string originalExt = texturePath.extension().generic_string();
      if (!originalExt.empty())
      {
         extensionsToTry.push_back(originalExt);
      }
      else
      {
         extensionsToTry.push_back(".bmp");
         extensionsToTry.push_back(".gif");
         extensionsToTry.push_back(".png");
         extensionsToTry.push_back(".jpg");
      }

      fs::path searchDir = fs::path(resourceFilename).parent_path();
      for (int depth = 0; depth < 2; ++depth)
      {
         for (const std::string& ext : extensionsToTry)
         {
            fs::path candidatePath = searchDir / textureStem;
            candidatePath.replace_extension(ext);
            const std::string candidate = candidatePath.generic_string();
            if (resourceManager->openFile(candidate.c_str(), outStream, resourceMount))
            {
               if (outResolvedPath)
               {
                  *outResolvedPath = candidate;
               }
               return true;
            }
         }

         if (searchDir.empty())
         {
            break;
         }
         searchDir = searchDir.parent_path();
      }

      return false;
   }

   
   struct LoadedTexture
   {
      int32_t texID;
      uint32_t bmpFlags;
      uint16_t width, height;
      
      LoadedTexture() {;}
      LoadedTexture(int32_t tid, uint32_t bf) : texID(tid), bmpFlags(bf) {;}
   };
   
   struct ActiveMaterial
   {
      LoadedTexture tex;
      uint32_t texGroupID;
   };
   
   std::vector<ActiveMaterial> mActiveMaterials;
   std::unordered_map<std::string, LoadedTexture> mLoadedTextures;
   ActiveMaterial mSharedMaterials;
   
   ResManager* mResourceManager;
   Palette* mPalette;
   MaterialList* mMaterialList;
   std::string mResourceFilename;
   int32_t mResourceMount;
   
   bool initVB;
   bool useShared;
   bool mApplyScaleTransforms;
   
   slm::mat4 mProjectionMatrix;
   slm::mat4 mModelMatrix;
   slm::mat4 mViewMatrix;
   
   slm::vec4 mLightColor;
   slm::vec3 mLightPos;
   
   GenericViewer() : mResourceManager(NULL), mPalette(NULL), mMaterialList(NULL)
   {
      useShared = false;
      mResourceMount = -1;
   }

   void setResourcePath(const char* filename, int32_t forceMount = -1)
   {
      mResourceFilename.clear();
      mResourceMount = forceMount;

      if (filename == NULL)
      {
         return;
      }

      std::string resolvedFilename;
      int32_t resolvedMount = forceMount;
      mResourceManager->resolveResourcePath(filename, resolvedFilename, resolvedMount, forceMount);
      mResourceFilename = resolvedFilename;
      mResourceMount = resolvedMount;
   }
   
   void updateMVP()
   {
      GFXSetModelViewProjection(mModelMatrix, mViewMatrix, mProjectionMatrix);
      GFXSetLightPos(mLightPos, mLightColor);
   }
   
   void initMaterials()
   {
      mActiveMaterials.clear();
      
      if (!mMaterialList)
      {
         assert(false);
         return;
      }
      
      if (useShared)
      {
         // Load as single shared layered 2d texture
         if (!loadSharedMaterials())
         {
            assert(false);
         }
      }
      else
      {
         mActiveMaterials.resize(mMaterialList->mMaterials.size());
         for (int i=0; i<mMaterialList->mMaterials.size(); i++)
         {
            MaterialList::Material& mat = mMaterialList->mMaterials[i];
            ActiveMaterial& amat = mActiveMaterials[i];
            if (loadTexture(mat.name.c_str(), amat.tex))
            {
               amat.texGroupID = amat.tex.texID;
            }
         }
      }
   }
   
   bool loadSharedMaterials()
   {
      bool fail = false;
      std::vector<Bitmap*> bitmaps;
      int lastSize[2];
      lastSize[0] = -1;
      lastSize[1] = -1;
      
      int count = 0;
      
      for (MaterialList::Material& mat : mMaterialList->mMaterials)
      {
         std::string fname = mat.name;
         
         // Find in resources
         MemRStream mem(0, NULL);
         std::string resolvedPath;
         if (openTextureStreamWithFallback(mResourceManager, mResourceFilename, mResourceMount, fname.c_str(), mem, &resolvedPath))
         {
            Bitmap* bmp = new Bitmap();
            if (!bmp->readAuto(mem))
            {
               fail = true;
               break;
            }
            else
            {
               if (lastSize[0] >= 0 && lastSize[0] != bmp->mWidth && lastSize[1] != bmp->mHeight)
               {
                  fail = true;
                  break;
               }
               
               lastSize[0] = bmp->mWidth;
               lastSize[1] = bmp->mHeight;
               bitmaps.push_back(bmp);
            }
         }
         else
         {
            fail = true;
            break;
         }
         
         count++;
      }
      
      if (!fail)
      {
         mSharedMaterials.tex.bmpFlags = 0;
         mSharedMaterials.tex.width = lastSize[0];
         mSharedMaterials.tex.height = lastSize[1];
         mSharedMaterials.tex.texID = GFXLoadTextureSet(bitmaps.size(), &bitmaps[0], mPalette);
      }
      
      for (Bitmap* bmp : bitmaps)
      {
         delete bmp;
      }
      
      return !fail;
   }
   
   bool loadTexture(const char *filename, LoadedTexture& outTexInfo, bool force=false)
   {
      bool genTex = true;
      std::string fname = std::string(filename);
      auto itr = mLoadedTextures.find(fname);
      if (itr != mLoadedTextures.end())
      {
         outTexInfo = itr->second;
         genTex = false;
         if (!force) return true;
      }
      
      // Find in resources
      MemRStream mem(0, NULL);
      std::string resolvedPath;
      if (openTextureStreamWithFallback(mResourceManager, mResourceFilename, mResourceMount, filename, mem, &resolvedPath))
      {
         Bitmap* bmp = new Bitmap();
         if (bmp->readAuto(mem))
         {
            int32_t texID = GFXLoadTexture(bmp, mPalette);
            if (texID >= 0)
            {
               printf("Loaded texture %s dimensions %ix%i\n", resolvedPath.c_str(), bmp->mWidth, bmp->mHeight);
               outTexInfo.bmpFlags = 0;// TOFIX bmp->mFlags;
               outTexInfo.texID = texID;
               outTexInfo.width = bmp->mWidth;
               outTexInfo.height = bmp->mHeight;
            }
            
            // Done
            mLoadedTextures[fname] = outTexInfo;
            delete bmp;
            return true;
         }
         delete bmp;
      }
      
      return false;
   }
   
   void clearTextures()
   {
      for (auto itr: mLoadedTextures) { GFXDeleteTexture(itr.second.texID); }
      mLoadedTextures.clear();
      
      if (mSharedMaterials.tex.texID != 0)
      {
         GFXDeleteTexture(mSharedMaterials.tex.texID);
         mSharedMaterials.tex.texID = 0;
      }
   }
   
};

class ViewController
{
public:
   slm::vec3 mViewPos;
   slm::vec3 mCamRot;
   float mViewSpeed;
   
   ViewController() : mViewSpeed(1)
   {
      
   }
   
   virtual void update(float dt) = 0;
   virtual bool isResourceLoaded() = 0;
};

class ShapeViewer : public GenericViewer
{
public:
   
   struct RuntimeMeshInfo
   {
      Dts3::Mesh* mMesh;
      //
      uint32_t mIndexCount;
      uint32_t mVertCount;
      uint32_t mRealVertsPerFrame;
      //
      uint32_t mVertOffset;
      uint32_t mIndexOffset;
      //
      uint32_t mMeshFrame;     // verts frame offset
      uint32_t mMeshTexFrame;  // tverts frame offset
      uint32_t mMeshTransformOffset;
      //
      uint32_t mRenderFlags;
      
      bool mUseSkinData;
      
      RuntimeMeshInfo() { memset(this, 0, sizeof(RuntimeMeshInfo)); }
      ~RuntimeMeshInfo() {;}
   };
   
   struct RuntimeIflMaterialInfo
   {
      int32_t mFrame;
      int32_t mStartFrame;
      uint32_t mIflMaterial;
      float mDuration; // 1.0 / 30 * total
   };
   
   struct RuntimeDecalInfo
   {
      int32_t mFrame;
   };
   
   struct RuntimeObjectInfo
   {
      uint32_t mObjectState;
      bool mDraw;
      
      int32_t mLastMatFrame;
      int32_t mLastMeshframe;
      float mLastVis;
      
      RuntimeObjectInfo() : mLastMatFrame(0), mLastMeshframe(0), mLastVis(1.0), mDraw(true) {;}
      ~RuntimeObjectInfo() {;}
   };
   
   struct RuntimeDetailInfo
   {
      uint32_t startRenderObject;
      uint32_t numRenderObjects;
      uint32_t meshIndex;
      
      RuntimeDetailInfo() {;}
      RuntimeDetailInfo(uint32_t so, uint32_t nro) : startRenderObject(so), numRenderObjects(nro) {;}
   };
   
   struct RuntimeSubShapeInfo
   {
      enum Flags : uint8_t
      {
         TransformDirty = BIT(0),
         VisDirty = BIT(1),
         FrameDirty = BIT(2),
         MatFrameDirty = BIT(3),
         ThreadDirty = BIT(4),
         // NOTE: these are used in TGE shapes only
         IflDirty = BIT(5),
         DecalDirty = BIT(6),
         //
         AllDirty = TransformDirty | VisDirty | FrameDirty | MatFrameDirty | ThreadDirty | IflDirty | DecalDirty
      };
      
      uint8_t dirtyFlags;
      uint32_t subShapeIndex;
      
      RuntimeSubShapeInfo() : dirtyFlags(0) {;}
      inline bool testFlags(uint8_t flag) { return (dirtyFlags & flag) != 0; }
      inline void clearFlags(uint8_t flag) { dirtyFlags &= ~flag; }
   };
   
   typedef void (*SetNodeTransformCallback)(ShapeViewer*, int32_t, slm::mat4&);
   
   struct NodeCallbackInfo
   {
      SetNodeTransformCallback* callback;
      int32_t nodeIndex;
   };
   
   std::vector<Dts3::Thread> mThreads;
   std::vector<uint32_t> mSortedThreads;
   std::vector<uint32_t> mTransitionThreads; // base threads that are transitioning
   
   struct TransitionSets
   {
      IntegerSet rotationNodes;
      IntegerSet translationNodes;
      IntegerSet scaleNodes;
   };
   
   TransitionSets mTransitionSets;
   int32_t mGroundThreadIdx;
   
   Dts3::Shape* mShape;
   
   std::vector<slm::mat4> mLocalNodeTransforms; // Current transform list (minus parent)
   std::vector<slm::mat4> mNodeTransforms; // Current transform list (including parent)
   
   // Working node animation values
   std::vector<slm::quat> mActiveRotations; // non-gl xfms
   std::vector<slm::vec4> mActiveTranslations; // non-gl xfms
   std::vector<float> mActiveUniformScales; // non-gl xfms
   std::vector<slm::vec3> mActiveAlignedScales; // non-gl xfms
   std::vector<Dts3::ArbitraryScale> mActiveArbitraryScales; // non-gl xfms
   
   // Derived states of shape objects
   std::vector<RuntimeMeshInfo> mRuntimeMeshInfos;
   std::vector<RuntimeObjectInfo> mRuntimeObjectInfos;
   std::vector<RuntimeIflMaterialInfo> mRuntimeIflMaterialInfos;
   std::vector<RuntimeDecalInfo> mRuntimeDecalInfos;
   std::vector<RuntimeDetailInfo> mRuntimeDetailInfos;
   std::vector<RuntimeSubShapeInfo> mRuntimeSubShapeInfos;
   std::vector<NodeCallbackInfo> mRuntimeNodeCallbacks;
   std::vector<float> mIflFrameTimes; // this is per ifl
   
   // NOTE: these are thread references used to handle transitions
   std::vector<int32_t> mNodeRotationThreads;
   std::vector<int32_t> mNodeTranslationThreads;
   std::vector<int32_t> mNodeScaleThreads;
   
   int32_t mDefaultMaterials;
   int32_t mAlwaysNode;
   int32_t mCurrentDetail;
   uint32_t mTriggerStateFlags;
   
   std::vector<bool> mThreadActive;
   uint32_t mBaseTextureTransform;
   
   template<typename T> struct FrameTexInfo
   {
      int32_t texID;
      uint32_t memoryUsed;
      uint32_t memorySize;
      T* updateMem;
      
      FrameTexInfo() : texID(-1), memoryUsed(0), memorySize(0), updateMem(NULL)
      {
      }
      
      void reset()
      {
         if (texID >= 0)
         {
            GFXDeleteTexture(texID);
         }
         if (updateMem)
         {
            delete[] updateMem;
            updateMem = NULL;
         }
         texID = -1;
      }
      
      uint32_t getRequiredDim()
      {
         uint32_t requiredTexels = std::max<uint32_t>(memoryUsed * 3, 1);
         uint32_t baseSize = static_cast<uint32_t>(std::pow(2, std::ceil(std::log2(std::sqrt((double)requiredTexels)))));
         return std::min<uint32_t>(baseSize, 256);
      }
      
      uint32_t allocTransforms(uint32_t numTransforms)
      {
         uint32_t offset = memoryUsed;
         memoryUsed += numTransforms;
         return offset;
      }
      
      void ensureValid(uint32_t initialTransformSize, T* initialMem)
      {
         if (memoryUsed > memorySize)
         {
            uint32_t pow2Size = getRequiredDim();
            
            if (updateMem)
            {
               delete[] updateMem;
            }
            
            uint32_t transformCapacity = std::max<uint32_t>((pow2Size * pow2Size) / 3, 1);
            updateMem = new T[transformCapacity];
            memset(updateMem, 0, transformCapacity * sizeof(T));
            if (initialMem)
            {
               memcpy(updateMem, initialMem, initialTransformSize * sizeof(T));
            }
            memorySize = transformCapacity;
            
            if (texID >= 0)
            {
               GFXDeleteTexture(texID);
            }
            
            texID = GFXLoadCustomTexture(CustomTexture_Float, pow2Size, pow2Size, updateMem);
         }
         else
         {
            if (initialMem)
            {
               memcpy(updateMem, initialMem, initialTransformSize * sizeof(T));
            }
            GFXUpdateCustomTextureAligned(texID, updateMem);
         }
      }
   };
   
   struct Matrix43
   {
      slm::vec4 r1;
      slm::vec4 r2;
      slm::vec4 r3;
   };

   static Matrix43 packMatrix43(const slm::mat4& mat)
   {
      Matrix43 packed = {};
      packed.r1 = slm::vec4(mat[0].x, mat[1].x, mat[2].x, mat[3].x);
      packed.r2 = slm::vec4(mat[0].y, mat[1].y, mat[2].y, mat[3].y);
      packed.r3 = slm::vec4(mat[0].z, mat[1].z, mat[2].z, mat[3].z);
      return packed;
   }
   
   struct LookupRegister
   {
      uint32_t vals[4];
   };
   
   typedef FrameTexInfo<Matrix43> TransformTexInfo;
   
   // Transform tex: 4x3 matrix 3 texels per matrix
   TransformTexInfo nodeMeshTransformsTex;
   
   ShapeViewer(ResManager* res)
   {
      mShape = NULL;
      mResourceManager = res;
      initVB = false;
   }
   
   ~ShapeViewer()
   {
      clear();
   }
   
   void clear()
   {
      nodeMeshTransformsTex.reset();
      mThreads.clear();
      mSortedThreads.clear();
      mTransitionThreads.clear();
      mThreadActive.clear();
      mRuntimeMeshInfos.clear();
      mRuntimeObjectInfos.clear();
      mRuntimeIflMaterialInfos.clear();
      mRuntimeDecalInfos.clear();
      mRuntimeDetailInfos.clear();
      mRuntimeSubShapeInfos.clear();
      mRuntimeNodeCallbacks.clear();
      mNodeTransforms.clear();
      mLocalNodeTransforms.clear();
      mActiveRotations.clear();
      mActiveTranslations.clear();
      mActiveUniformScales.clear();
      mActiveAlignedScales.clear();
      mActiveArbitraryScales.clear();
      mGroundThreadIdx = -1;
      mCurrentDetail = -1;
      mTriggerStateFlags = 0;
      
      clearVertexBuffer();
      clearTextures();
      clearRender();
   }
   
   size_t getMaxSkinnedTransforms()
   {
      size_t minMeshTransforms = 1 + mShape->mNodes.size();
      
      // We can have the same node distributed across multiple meshes,
      // so check the mesh counts!
      for (Dts3::SubShape subShape : mShape->mSubshapes)
      {
         size_t subShapeTransforms = 0;
         
         // Consider all objects in the subshape
         for (uint32_t i=0; i<subShape.numObjects; i++)
         {
            Dts3::Object& obj = mShape->mObjects[subShape.firstObject + i];
            
            // Check all the meshes in this object
            for (uint32_t mi=0; mi<obj.numMeshes; mi++)
            {
               Dts3::Mesh& m = mShape->mMeshes[obj.firstMesh + mi];
               if (m.mType == Dts3::Mesh::T_Skin)
               {
                  Dts3::SkinData* skinData = m.getSkinData();
                  subShapeTransforms += skinData->nodeTransforms.size();
               }
            }
         }
         
         minMeshTransforms = std::max<size_t>(minMeshTransforms, subShapeTransforms);
      }
      
      return minMeshTransforms;
   }
   
   void initRender()
   {
      mLightColor = slm::vec4(1,1,1,1);
      mLightPos = slm::vec3(0,2, 2);
      if (mShape == NULL)
         return;
      
      mBaseTextureTransform = 0;
      mRuntimeMeshInfos.resize(mShape->mMeshes.size());
      mRuntimeObjectInfos.resize(mShape->mObjects.size());
      mRuntimeIflMaterialInfos.resize(mShape->mIflMaterials.size());
      mRuntimeDecalInfos.resize(mShape->mDecals.size());
      mRuntimeDetailInfos.resize(mShape->mDetailLevels.size());
      mRuntimeSubShapeInfos.resize(mShape->mSubshapes.size());
      mLocalNodeTransforms.resize(mShape->mNodes.size(), slm::mat4(1.0f));
      mNodeTransforms.resize(mShape->mNodes.size(), slm::mat4(1.0f));
      mActiveRotations.resize(mShape->mNodes.size());
      mActiveTranslations.resize(mShape->mNodes.size());
      mActiveUniformScales.resize(mShape->mNodes.size(), 1.0f);
      mActiveAlignedScales.resize(mShape->mNodes.size(), slm::vec3(1.0f));
      mActiveArbitraryScales.resize(mShape->mNodes.size());
      for (size_t i=0; i<mRuntimeSubShapeInfos.size(); i++)
      {
         mRuntimeSubShapeInfos[i].subShapeIndex = (uint32_t)i;
         mRuntimeSubShapeInfos[i].dirtyFlags = RuntimeSubShapeInfo::AllDirty;
      }
      
      // Load base skin transforms texture
      nodeMeshTransformsTex.reset();
      
      size_t maxTransforms = getMaxSkinnedTransforms();
      if (maxTransforms > 0)
      {
         mBaseTextureTransform = nodeMeshTransformsTex.allocTransforms(maxTransforms);
         nodeMeshTransformsTex.ensureValid(maxTransforms, NULL);
      }

      for (size_t i=0; i<mRuntimeMeshInfos.size(); i++)
      {
         mRuntimeMeshInfos[i].mMesh = &mShape->mMeshes[i];
         mRuntimeMeshInfos[i].mMeshTransformOffset = mBaseTextureTransform;
      }
      
      for (Dts3::Object& obj : mShape->mObjects)
      {
         uint32_t objectTransformOffset = mBaseTextureTransform;
         if (obj.node >= 0)
         {
            objectTransformOffset += 1 + (uint32_t)obj.node;
         }
         
         for (int meshIdx = 0; meshIdx < obj.numMeshes; meshIdx++)
         {
            RuntimeMeshInfo& rm = mRuntimeMeshInfos[obj.firstMesh + meshIdx];
            rm.mMeshTransformOffset = objectTransformOffset;
            
            Dts3::SkinData* sd = rm.mMesh->getSkinData();
            if (sd && !sd->nodeTransforms.empty())
            {
               rm.mMeshTransformOffset = nodeMeshTransformsTex.allocTransforms((uint32_t)sd->nodeTransforms.size());
            }
         }
      }
      
      if (nodeMeshTransformsTex.memoryUsed > maxTransforms)
      {
         nodeMeshTransformsTex.ensureValid(nodeMeshTransformsTex.memoryUsed, NULL);
      }
      
      initRenderMaterials();
   }
   
   void initRenderMaterials()
   {
      bool found = false;
      
      for (Dts3::SubShape& s : mShape->mSubshapes)
      {
         // NOTE: torque has a "bug" here where it goes from
         // firstObject...numObjects instead of firstObject..(firstObject+numObjects)
         // IN ADDITION: to keep things simple, we ignore primitives on decal meshes.
         s.firstTranslucent = s.firstObject + s.numObjects;
         for (uint32_t i=0; i<s.numObjects; i++)
         {
            Dts3::Object& obj = mShape->mObjects[s.firstObject+i];
            for (uint32_t j=0; j<obj.numMeshes; j++)
            {
               Dts3::Mesh& mesh = mShape->mMeshes[obj.firstMesh + j];
               Dts3::BasicData* bd = mesh.getBasicData();
               if (bd == NULL)
                  continue;
               
               for (Dts3::Primitive& prim : bd->primitives)
               {
                  if ((prim.matIndex & Dts3::Primitive::NoMaterial) != 0)
                     continue;
                  uint32_t matIndex = prim.matIndex & Dts3::Primitive::MaterialMask;
                  uint32_t flags = mShape->mMaterials[matIndex].tsProps.flags;
                  if ((flags & MaterialList::AuxiliaryMap) != 0)
                     continue;
                  if ((flags & MaterialList::Translucent) != 0)
                  {
                     mShape->mRuntimeFlags |= Dts3::Shape::HasTranslucency;
                     s.firstTranslucent = i;
                     found = true;
                     break;
                  }
               }
               
               if (found)
                  break;
            }
            
            if (found)
               break;
         }
         
         if (found)
            break;
      }
   }
   
   void clearRender()
   {
      nodeMeshTransformsTex.reset();
   }
   
   // Sequence Handling
   
   template<typename T> void forEachSortedThread(T&& func)
   {
      for (uint32_t num : mSortedThreads)
      {
         if (mThreads[num].index < 0)
            continue;
         if (mThreads[num].sequenceIdx < 0 || mThreads[num].sequenceIdx >= mShape->mSequences.size())
            continue;
         func(mThreads[num]);
      }
   }
   
   template<typename T> void forEachSortedThreadCheck(T&& func)
   {
      for (uint32_t num : mSortedThreads)
      {
         if (mThreads[num].index < 0)
            continue;
         if (mThreads[num].sequenceIdx < 0 || mThreads[num].sequenceIdx >= mShape->mSequences.size())
            continue;
         if (!func(mThreads[num]))
         {
            return;
         }
      }
   }
   
   uint32_t addThread()
   {
      // Re-use existing if possible
      for (size_t i=0; i<mThreads.size(); i++)
      {
         if (mThreads[i].index < 0)
         {
            mThreads[i].index = (int32_t)i;
            return (int32_t)i;
         }
      }
      
      Dts3::Thread thread;
      thread.shape = mShape;
      thread.index = mThreads.size();
      mThreads.push_back(thread);
      mSortedThreads.push_back(thread.index);
      mThreadActive.push_back(false);
      return thread.index;
   }
   
   static constexpr float kSequenceLoopEnd = 0.9999f;
   
   void setThreadSequence(uint32_t idx, int32_t sequenceIdx, float pos)
   {
      Dts3::Thread& thread = mThreads[idx];
      clearTransition(thread);
      
      if (sequenceIdx < 0)
      {
         thread.sequenceIdx = -1;
         thread.pos = 0.0f;
         thread.priority = 0;
         thread.playing = false;
         thread.timeScale = 0.0f;
         thread.makePath = false;
         thread.path = Dts3::ThreadPath();
         if (idx < mThreadActive.size())
            mThreadActive[idx] = false;
      }
      else
      {
         Dts3::Sequence& sequence = mShape->mSequences[sequenceIdx];
         thread.sequenceIdx = sequenceIdx;
         thread.pos = pos;
         thread.priority = sequence.priority;
         //thread.makePath = sequence.MakePath;
         if (sequence.testFlags(Dts3::Sequence::Cyclic) &&
             thread.pos > kSequenceLoopEnd)
         {
            thread.pos = kSequenceLoopEnd;
         }
         
         thread.playing = true;
         thread.timeScale = 1.0f;
         selectKeyFrames(pos, sequence, thread.keyInfo);
         if (idx < mThreadActive.size())
            mThreadActive[idx] = true;
      }

      sortThreads();
      updateScaleAnimatonState();
      setDirty(RuntimeSubShapeInfo::AllDirty);
   }
   
   void transitionToSequence(Dts3::Thread& thread, int32_t sequenceIdx, float pos, float duration, bool playing)
   {
      animateNodeSubtrees(false);
      
      transitionThreadToSequence(thread, sequenceIdx, pos, duration, playing);
      setDirty(RuntimeSubShapeInfo::AllDirty);
      mGroundThreadIdx = -1;
      
      updateScaleAnimatonState();
      
      Dts3::Sequence& seq = mShape->mSequences[thread.sequenceIdx];
      mTransitionSets.rotationNodes |= thread.transitionState.oldRotations;
      mTransitionSets.rotationNodes |= seq.mattersRot;
      mTransitionSets.translationNodes |= thread.transitionState.oldTranslations;
      mTransitionSets.translationNodes |= seq.mattersTranslation;
      mTransitionSets.scaleNodes |= thread.transitionState.oldScales;
      mTransitionSets.scaleNodes |= seq.mattersScale;
      
      auto itr = std::find(mTransitionThreads.begin(), mTransitionThreads.end(), thread.index);
      if (itr == mTransitionThreads.end())
      {
         mTransitionThreads.push_back(thread.index);
      }
      
      updateTransitions();
   }
   
   void transitionThreadToSequence(Dts3::Thread& thread, int32_t sequenceIdx, float pos, float duration, bool playing)
   {
      
   }
   
   void updateTransitions()
   {
      // NOTE: this basically updates the reference transform for the transitioning threads
   }
   
   void selectKeyFrames(float pos, Dts3::Sequence& sequence, Dts3::Thread::KeyFrameInfo& outInfo)
   {
      // NOTE: This is vaguely similar to tribes 1 except the math is greatly simplified.
      
      if (sequence.testFlags(Dts3::Sequence::Cyclic))
      {
         float keyFrameFloat = pos * (float)sequence.numKeyFrames;
         outInfo.keyPos = keyFrameFloat - (int)keyFrameFloat; // leave only fractional part
         
         outInfo.keyA = (int)keyFrameFloat;
         outInfo.keyB = outInfo.keyA == (sequence.numKeyFrames-1) ? 0 : outInfo.keyA+1;
      }
      else
      {
         if (pos == 1.0f)
         {
            // Reached end
            outInfo.keyPos = 0.0f;
            outInfo.keyA = outInfo.keyB = sequence.numKeyFrames-1;
         }
         else
         {
            // Still playing, ends at numKeyFrames-1
            float keyFrameFloat = pos * (float)(sequence.numKeyFrames-1);
            outInfo.keyPos = keyFrameFloat - (int)keyFrameFloat;
            outInfo.keyA = (int)keyFrameFloat;
            outInfo.keyB = outInfo.keyA+1;
         }
      }
   }
   
   void clearTransition(Dts3::Thread& thread)
   {
      if (!thread.transitioning)
      {
         return;
      }
      
      // TODO
      
      setDirty(RuntimeSubShapeInfo::ThreadDirty);
   }
   
   void removeThread(uint32_t idx)
   {
      Dts3::Thread& thread = mThreads[idx];
      clearTransition(thread);
      
      mThreads[idx].index = -1;
      if (idx < mThreadActive.size())
         mThreadActive[idx] = false;
      
      setDirty(RuntimeSubShapeInfo::AllDirty);
      updateScaleAnimatonState();
      sortThreads(); // make sure order is consistent
   }
   
   void animateSubshape(RuntimeSubShapeInfo& runtimeSubShape)
   {
      Dts3::SubShape& subShape = mShape->mSubshapes[runtimeSubShape.subShapeIndex];
      
      if (runtimeSubShape.testFlags(RuntimeSubShapeInfo::ThreadDirty))
      {
         sortThreads();
      }
      
      if (runtimeSubShape.testFlags(RuntimeSubShapeInfo::IflDirty))
      {
         animateIfls();
      }
      
      if (runtimeSubShape.testFlags(RuntimeSubShapeInfo::TransformDirty))
      {
         animateNodes(subShape);
      }
      
      if (runtimeSubShape.testFlags(RuntimeSubShapeInfo::VisDirty))
      {
         animateVisibility(subShape);
      }
      
      if (runtimeSubShape.testFlags(RuntimeSubShapeInfo::FrameDirty))
      {
         animateMeshFrame(subShape);
      }
      
      if (runtimeSubShape.testFlags(RuntimeSubShapeInfo::MatFrameDirty))
      {
         animateMatFrame(subShape);
      }
      
      if (runtimeSubShape.testFlags(RuntimeSubShapeInfo::DecalDirty))
      {
         animateDecals(subShape);
      }
      
      runtimeSubShape.dirtyFlags = 0;
   }
   
   void animate(Dts3::DetailLevel detailLevel)
   {
      if (detailLevel.subshape < 0)
      {
         return;
      }
      
      RuntimeSubShapeInfo& runtimeSubShape = mRuntimeSubShapeInfos[detailLevel.subshape];
      animateSubshape(runtimeSubShape);
   }
   
   
   void animateIfls()
   {
      for (RuntimeIflMaterialInfo& info : mRuntimeIflMaterialInfos)
      {
         Dts3::IflMaterial& iflInfo = mShape->mIflMaterials[info.mIflMaterial];
         info.mFrame = 0;
         forEachSortedThreadCheck([&](Dts3::Thread& thread){
            Dts3::Sequence& seq = mShape->mSequences[thread.sequenceIdx];
            // NOTE: mattersIfl basically applies for the entire sequence
            // ALSO: only one thread can control an IFL at any time; basically think of it like
            //       the thread just plays the ifl offset from toolBegin.
            if (seq.mattersIfl.test(info.mIflMaterial))
            {
               int32_t firstFrame = iflInfo.firstFrame;
               int32_t numFrames = iflInfo.numFrames;
               float duration = info.mDuration;
               
               float time = (thread.pos * seq.duration) + seq.toolBegin;
               if (time > duration && duration > 0.0f)
               {
                  time -= duration * (float)((int32_t) (time / duration)); // loop
               }
               
               // Lookup frame t1 style
               int32_t frameIdx = 0;
               for (; frameIdx < numFrames-1 && time > mIflFrameTimes[firstFrame + frameIdx]; frameIdx++);
               info.mFrame = frameIdx;
               return false;
            }
            return true;
         });
      }
      
      clearDirty(RuntimeSubShapeInfo::IflDirty);
   }
   
   void sortThreads()
   {
      std::sort(mSortedThreads.begin(), mSortedThreads.end(),
                [&](size_t i, size_t j) {
        Dts3::Thread const& a = mThreads[i];
        Dts3::Thread const& b = mThreads[j];
         
         // inactive goes on end
         if (a.index < 0)
         {
            return false;
         }
         if (b.index < 0)
         {
            return true;
         }
         if (a.sequenceIdx < 0 || a.sequenceIdx >= mShape->mSequences.size())
         {
            return false;
         }
         if (b.sequenceIdx < 0 || b.sequenceIdx >= mShape->mSequences.size())
         {
            return true;
         }

         Dts3::Sequence const& aSeq = mShape->mSequences[a.sequenceIdx];
         Dts3::Sequence const& bSeq = mShape->mSequences[b.sequenceIdx];

         const bool aBlend = aSeq.testFlags(Dts3::Sequence::Blend);
         const bool bBlend = bSeq.testFlags(Dts3::Sequence::Blend);

                    if (aBlend != bBlend)
                        return !aBlend && bBlend;

                    return a.priority > b.priority;
                });
      
   }
   
   void setDirty(uint8_t mask)
   {
      for (RuntimeSubShapeInfo& info : mRuntimeSubShapeInfos)
      {
         info.dirtyFlags |= mask;
      }
   }
   
   void clearDirty(uint8_t mask)
   {
      for (RuntimeSubShapeInfo& info : mRuntimeSubShapeInfos)
      {
         info.dirtyFlags &= ~mask;
      }
   }
   
   void updateScaleAnimatonState()
   {
      // NOTE: this adds on a bunch of scale computations if enabled
      mApplyScaleTransforms = false;
      forEachSortedThread([&](Dts3::Thread& thread){
         if (thread.sequenceIdx >= 0)
         {
            Dts3::Sequence& seq = mShape->mSequences[thread.sequenceIdx];
            if (seq.testFlags(Dts3::Sequence::AnyScale))
            {
               mApplyScaleTransforms = true;
               return;
            }
         }
      });
   }
   
   float getThreadTime(Dts3::Thread& thread)
   {
      return thread.transitioning ? thread.transitionState.pos * thread.transitionState.duration :
                                    thread.pos * mShape->mSequences[thread.sequenceIdx].duration;
   }
   
   float getThreadPos(Dts3::Thread& thread)
   {
      return thread.transitioning ? thread.transitionState.pos :
                                    thread.pos;
   }
   
   float getThreadDuration(Dts3::Thread& thread)
   {
      return thread.transitioning ? thread.transitionState.duration : mShape->mSequences[thread.sequenceIdx].duration;
   }
   
   float getThreadDurationScaled(Dts3::Thread& thread)
   {
      return getThreadDuration(thread) / fabs(thread.timeScale);
   }
   
   void advanceThreadTime(Dts3::Thread& thread, float dt)
   {
      advanceThreadPos(thread, thread.timeScale * dt / getThreadDuration(thread));
   }
   
   void advanceThreadPos(Dts3::Thread& thread, float pos)
   {
      if (thread.sequenceIdx < 0 || thread.sequenceIdx >= mShape->mSequences.size())
         return;

      Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
      thread.pos += pos;

      if (sequence.testFlags(Dts3::Sequence::Cyclic))
      {
         while (thread.pos < 0.0f)
            thread.pos += 1.0f;
         while (thread.pos >= 1.0f)
            thread.pos -= 1.0f;
         if (thread.pos > kSequenceLoopEnd)
            thread.pos = kSequenceLoopEnd;
      }
      else
      {
         thread.pos = std::clamp(thread.pos, 0.0f, 1.0f);
         if (thread.pos >= 1.0f || thread.pos <= 0.0f)
            thread.playing = false;
      }

      selectKeyFrames(thread.pos, sequence, thread.keyInfo);
      setDirty(RuntimeSubShapeInfo::AllDirty);
   }
   
   void advanceThreads(float dt)
   {
      // General overview:
      // -
      
      
      for (Dts3::Thread& thread : mThreads)
      {
         if (thread.index >= 0 &&
             thread.index < mThreadActive.size() &&
             mThreadActive[thread.index] &&
             thread.playing)
         {
            advanceThreadTime(thread, dt);
         }
      }

   }
   
   void animateNodeSubtrees(bool force)
   {
      if (force)
      {
         setDirty(RuntimeSubShapeInfo::TransformDirty);
      }
      
      for (RuntimeSubShapeInfo& runtimeShape : mRuntimeSubShapeInfos)
      {
         Dts3::SubShape& subShape = mShape->mSubshapes[runtimeShape.subShapeIndex];
         animateNodes(subShape);
         runtimeShape.clearFlags(RuntimeSubShapeInfo::TransformDirty);
      }
   }
   
   void animateAllSubtrees(bool force)
   {
      // NOTE: torque code has a bug here in that it confuses subshapes with nodes
      if (force)
      {
         setDirty(RuntimeSubShapeInfo::AllDirty);
      }
      
      for (RuntimeSubShapeInfo& runtimeShape : mRuntimeSubShapeInfos)
      {
         animateSubshape(runtimeShape);
         runtimeShape.clearFlags(RuntimeSubShapeInfo::TransformDirty);
      }
   }
   
   void updateTransformTexture()
   {
      if (nodeMeshTransformsTex.texID < 0 || nodeMeshTransformsTex.updateMem == NULL || nodeMeshTransformsTex.memoryUsed == 0)
         return;
      
      memset(nodeMeshTransformsTex.updateMem, 0, nodeMeshTransformsTex.memorySize * sizeof(Matrix43));
      nodeMeshTransformsTex.updateMem[mBaseTextureTransform] = packMatrix43(slm::mat4(1.0f));
      
      for (size_t i=0; i<mNodeTransforms.size(); i++)
      {
         nodeMeshTransformsTex.updateMem[mBaseTextureTransform + 1 + i] = packMatrix43(mNodeTransforms[i]);
      }
      
      for (RuntimeMeshInfo& rm : mRuntimeMeshInfos)
      {
         Dts3::SkinData* sd = rm.mMesh->getSkinData();
         if (!sd)
            continue;
         
         for (size_t i=0; i<sd->nodeTransforms.size(); i++)
         {
            slm::mat4 finalTransform = sd->nodeTransforms[i];
            if (i < sd->nodeIndex.size() && sd->nodeIndex[i] < mNodeTransforms.size())
            {
               finalTransform = mNodeTransforms[sd->nodeIndex[i]] * sd->nodeTransforms[i];
            }
            nodeMeshTransformsTex.updateMem[rm.mMeshTransformOffset + i] = packMatrix43(finalTransform);
         }
      }
      
      GFXUpdateCustomTextureAligned(nodeMeshTransformsTex.texID, nodeMeshTransformsTex.updateMem);
      GFXSetModelTransformTexture(nodeMeshTransformsTex.texID);
   }
   
   void animateNodes(Dts3::SubShape& subShape)
   {
      // Basically we do this in order:
      // - Resize temp storage to match current shape node count
      // - Set all bits in (rot, trans, scale) bitsets
      // - For all threads, clear node bits that are set by the sequences; stop at first blend thread
      // - Overlap bitset with combo overlap of (x,y,z) pos masks
      // - Clear callback and hands-off nodes (these get handled externally)
      // - For the active subshape, extract default rotations and translations
      // - Overlap rotBeenSet with hands-off, callback, and (x,y,z) pos masks
      // - If scale is active, apply default scale
      // - For each active thread, updaye rotation and translation; then scale (if active)
      // - NOTE: interpolation is performed in both cases based on thread pos
      // - Apply basic transforms unless hands-off is set for node
      // - Apply scale transforms unless hands-off is set for node
      // - Invoke node callbacks for nodes set
      // - Apply blend transforms (basically: added on top)
      // - Apply transition transforms to marked transition nodes
      // - Apply final transform (based on parent node)
      const size_t nodeCount = mShape->mNodes.size();
      mLocalNodeTransforms.resize(nodeCount, slm::mat4(1.0f));
      mNodeTransforms.resize(nodeCount, slm::mat4(1.0f));
      std::vector<slm::quat> nodeRotations(nodeCount, slm::quat(0, 0, 0, 1));
      std::vector<slm::vec3> nodeTranslations(nodeCount, slm::vec3(0.0f));
      std::vector<slm::vec3> nodeScales(nodeCount, slm::vec3(1.0f));

      const int32_t firstNode = std::max(subShape.firstNode, 0);
      const int32_t endNode = std::min<int32_t>(subShape.firstNode + subShape.numNodes, (int32_t)nodeCount);
      for (int32_t nodeIdx = firstNode; nodeIdx < endNode; nodeIdx++)
      {
         if (nodeIdx < mShape->mDefaultRotations.size())
            nodeRotations[nodeIdx] = mShape->mDefaultRotations[nodeIdx].toQuat();
         if (nodeIdx < mShape->mDefaultTranslations.size())
            nodeTranslations[nodeIdx] = mShape->mDefaultTranslations[nodeIdx];
      }

      std::vector<bool> rotationSet(nodeCount, false);
      std::vector<bool> translationSet(nodeCount, false);
      std::vector<bool> scaleSet(nodeCount, false);

      forEachSortedThread([&](Dts3::Thread& thread){
         Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
         int32_t rotFrame = 0;
         int32_t transFrame = 0;
         int32_t scaleFrame = 0;

         for (int32_t nodeIdx = sequence.mattersRot.findFirst();
              nodeIdx >= 0;
              nodeIdx = sequence.mattersRot.findNext(nodeIdx))
         {
            if (nodeIdx < firstNode || nodeIdx >= endNode)
               continue;

            if (!rotationSet[nodeIdx])
            {
               slm::quat rotA = mShape->getSequenceRotation(sequence, thread.keyInfo.keyA, rotFrame).toQuat();
               slm::quat rotB = mShape->getSequenceRotation(sequence, thread.keyInfo.keyB, rotFrame).toQuat();
               nodeRotations[nodeIdx] = CompatInterpolate(rotA, rotB, thread.keyInfo.keyPos);
               rotationSet[nodeIdx] = true;
            }
            rotFrame++;
         }

         for (int32_t nodeIdx = sequence.mattersTranslation.findFirst();
              nodeIdx >= 0;
              nodeIdx = sequence.mattersTranslation.findNext(nodeIdx))
         {
            if (nodeIdx < firstNode || nodeIdx >= endNode)
               continue;

            if (!translationSet[nodeIdx])
            {
               slm::vec3 transA = mShape->getSequenceTranslation(sequence, thread.keyInfo.keyA, transFrame);
               slm::vec3 transB = mShape->getSequenceTranslation(sequence, thread.keyInfo.keyB, transFrame);
               nodeTranslations[nodeIdx] = slm::mix(transA, transB, thread.keyInfo.keyPos);
               translationSet[nodeIdx] = true;
            }
            transFrame++;
         }

         for (int32_t nodeIdx = sequence.mattersScale.findFirst();
              nodeIdx >= 0;
              nodeIdx = sequence.mattersScale.findNext(nodeIdx))
         {
            if (nodeIdx < firstNode || nodeIdx >= endNode)
               continue;

            if (!scaleSet[nodeIdx])
            {
               if (sequence.testFlags(Dts3::Sequence::ArbitraryScale))
               {
                  Dts3::ArbitraryScale scaleA = mShape->getSequenceArbitraryScale(sequence, thread.keyInfo.keyA, scaleFrame);
                  Dts3::ArbitraryScale scaleB = mShape->getSequenceArbitraryScale(sequence, thread.keyInfo.keyB, scaleFrame);
                  nodeScales[nodeIdx] = slm::mix(scaleA.pos, scaleB.pos, thread.keyInfo.keyPos);
               }
               else if (sequence.testFlags(Dts3::Sequence::AlignedScale))
               {
                  slm::vec3 scaleA = mShape->getSequenceAlignedScale(sequence, thread.keyInfo.keyA, scaleFrame);
                  slm::vec3 scaleB = mShape->getSequenceAlignedScale(sequence, thread.keyInfo.keyB, scaleFrame);
                  nodeScales[nodeIdx] = slm::mix(scaleA, scaleB, thread.keyInfo.keyPos);
               }
               else if (sequence.testFlags(Dts3::Sequence::UniformScale))
               {
                  float scaleA = mShape->getSequenceUniformScale(sequence, thread.keyInfo.keyA, scaleFrame);
                  float scaleB = mShape->getSequenceUniformScale(sequence, thread.keyInfo.keyB, scaleFrame);
                  float uniformScale = slm::mix(scaleA, scaleB, thread.keyInfo.keyPos);
                  nodeScales[nodeIdx] = slm::vec3(uniformScale);
               }
               scaleSet[nodeIdx] = true;
            }
            scaleFrame++;
         }
      });

      for (int32_t nodeIdx = firstNode; nodeIdx < endNode; nodeIdx++)
      {
         slm::mat4 localTransform;
         CompatQuatSetMatrix(nodeRotations[nodeIdx], localTransform);
         localTransform[3] = slm::vec4(nodeTranslations[nodeIdx], 1.0f);

         if (mApplyScaleTransforms)
         {
            localTransform = localTransform * slm::scaling(nodeScales[nodeIdx]);
         }

         mLocalNodeTransforms[nodeIdx] = localTransform;

         int32_t parentIdx = mShape->mNodes[nodeIdx].parent;
         if (parentIdx >= 0 && parentIdx < mNodeTransforms.size())
         {
            mNodeTransforms[nodeIdx] = mNodeTransforms[parentIdx] * localTransform;
         }
         else
         {
            mNodeTransforms[nodeIdx] = localTransform;
         }
      }
      
      updateTransformTexture();
   }
   
   void applyBlendSequence(Dts3::Thread& thread, const Dts3::SubShape& subShape)
   {
      
   }
   
   void applyTransitionNodes(const Dts3::SubShape& subShape)
   {
      
   }
   
   void animateVisibility(const Dts3::SubShape& subShape)
   {
      if (mRuntimeObjectInfos.empty())
      {
         return;
      }
      
      IntegerSet checkSet;
      checkSet.set();
         
      forEachSortedThread([&](Dts3::Thread& thread){
         checkSet.sub(mShape->mSequences[thread.sequenceIdx].mattersVis);
      });
      
      // NOTE: this is assuming objectStates starts with the base objects; also
      // this is just for setting the initial state (it doesn't save storage space).
      for (uint32_t i=subShape.firstDecal; i<subShape.firstDecal + subShape.numDecals; i++)
      {
         if (checkSet.test(i))
         {
            mRuntimeObjectInfos[i].mLastVis  = mShape->mObjectStates[i].vis;
         }
      }
      
      forEachSortedThread([&](Dts3::Thread& thread){
         Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
         
         // NOTE: object states are stored as (frame, matFrame, vis) thus why we have to combine
         // the state flags here
         IntegerSet objMatters = sequence.mattersFrame;
         objMatters |= sequence.mattersMatframe;
         objMatters |= sequence.mattersVis;
         
         int32_t startObjectsMatters = objMatters.findFirst();
         int32_t endObjectMatters = subShape.firstObject + subShape.numObjects;
         if (startObjectsMatters >= 0)
         {
            int32_t objFrame = 0;
            for (int32_t oi=startObjectsMatters; oi<endObjectMatters; oi = objMatters.findNext(oi))
            {
               if (oi < 0)
               {
                  break;
               }
               
               if (!checkSet.test(oi) &&
                   sequence.mattersVis.test(oi))
               {
                  float visStart = mShape->getSequenceObjectState(sequence, thread.keyInfo.keyA, objFrame).vis;
                  float visEnd = mShape->getSequenceObjectState(sequence, thread.keyInfo.keyB, objFrame).vis;
                  
                  const float visRange = visEnd - visStart;
                  if ((visRange * visRange) > 0.99f)
                  {
                     mRuntimeObjectInfos[oi].mLastVis = thread.keyInfo.keyPos < 0.5f ? visStart : visEnd;
                  }
                  else
                  {
                     mRuntimeObjectInfos[oi].mLastVis = ((1.0f - thread.keyInfo.keyPos) * visStart) + (thread.keyInfo.keyPos * visEnd);
                  }
                  
                  checkSet.set(oi, true);
               }
            }
         }
      });
   }
   
   void animateMeshFrame(const Dts3::SubShape& subShape)
   {
      if (mRuntimeObjectInfos.empty())
      {
         return;
      }
      
      IntegerSet checkSet;
      checkSet.set();
      
      forEachSortedThread([&](Dts3::Thread& thread){
         checkSet.sub(mShape->mSequences[thread.sequenceIdx].mattersFrame);
      });
      
      // NOTE: this is assuming objectStates starts with the base objects; also
      // this is just for setting the initial state (it doesn't save storage space).
      for (uint32_t i=subShape.firstDecal; i<subShape.firstDecal + subShape.numDecals; i++)
      {
         if (checkSet.test(i))
         {
            mRuntimeObjectInfos[i].mLastMeshframe  = mShape->mObjectStates[i].frame;
         }
      }
      
      forEachSortedThread([&](Dts3::Thread& thread){
         Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
         
         // NOTE: object states are stored as (frame, matFrame, vis) thus why we have to combine
         // the state flags here
         IntegerSet objMatters = sequence.mattersFrame;
         objMatters |= sequence.mattersMatframe;
         objMatters |= sequence.mattersVis;
         
         int32_t startObjectsMatters = objMatters.findFirst();
         int32_t endObjectMatters = subShape.firstObject + subShape.numObjects;
         if (startObjectsMatters >= 0)
         {
            int32_t objFrame = 0;
            for (int32_t oi=startObjectsMatters; oi<endObjectMatters; oi = objMatters.findNext(oi))
            {
               if (oi < 0)
               {
                  break;
               }
               
               if (!checkSet.test(oi) &&
                   sequence.mattersFrame.test(oi))
               {
                  int32_t keyNum = thread.keyInfo.keyPos < 0.5f ? thread.keyInfo.keyA : thread.keyInfo.keyB;
                  int32_t meshFrame = mShape->getSequenceObjectState(sequence, keyNum, objFrame).frame;
                  mRuntimeObjectInfos[oi].mLastMeshframe = meshFrame;
                  checkSet.set(oi, true);
               }
            }
         }
      });
   }
   
   void animateMatFrame(const Dts3::SubShape& subShape)
   {
      if (mRuntimeObjectInfos.empty())
      {
         return;
      }
      
      IntegerSet checkSet;
      checkSet.set();
      
      forEachSortedThread([&](Dts3::Thread& thread){
         checkSet.sub(mShape->mSequences[thread.sequenceIdx].mattersMatframe);
      });
      
      // NOTE: this is assuming objectStates starts with the base objects; also
      // this is just for setting the initial state (it doesn't save storage space).
      for (uint32_t i=subShape.firstDecal; i<subShape.firstDecal + subShape.numDecals; i++)
      {
         if (checkSet.test(i))
         {
            mRuntimeObjectInfos[i].mLastMatFrame  = mShape->mObjectStates[i].matFrame;
         }
      }
      
      forEachSortedThread([&](Dts3::Thread& thread){
         Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
         
         // NOTE: object states are stored as (frame, matFrame, vis) thus why we have to combine
         // the state flags here
         IntegerSet objMatters = sequence.mattersFrame;
         objMatters |= sequence.mattersMatframe;
         objMatters |= sequence.mattersVis;
         
         int32_t startObjectsMatters = objMatters.findFirst();
         int32_t endObjectMatters = subShape.firstObject + subShape.numObjects;
         if (startObjectsMatters >= 0)
         {
            int32_t objFrame = 0;
            for (int32_t oi=startObjectsMatters; oi<endObjectMatters; oi = objMatters.findNext(oi))
            {
               if (oi < 0)
               {
                  break;
               }
               
               if (!checkSet.test(oi) &&
                   sequence.mattersMatframe.test(oi))
               {
                  int32_t keyNum = thread.keyInfo.keyPos < 0.5f ? thread.keyInfo.keyA : thread.keyInfo.keyB;
                  int32_t matFrame = mShape->getSequenceObjectState(sequence, keyNum, objFrame).matFrame;
                  mRuntimeObjectInfos[oi].mLastMatFrame = matFrame;
                  checkSet.set(oi, true);
               }
            }
         }
      });
   }
   
   void animateDecals(const Dts3::SubShape& subShape)
   {
      if (mRuntimeDecalInfos.empty())
      {
         return;
      }
   
      IntegerSet checkSet;
      checkSet.set();
      
      forEachSortedThread([&](Dts3::Thread& thread){
         checkSet.sub(mShape->mSequences[thread.sequenceIdx].mattersDecal);
      });
      
      // NOTE: this is assuming decalStates starts with the base decals; also
      // this is just for setting the initial frame (it doesn't save storage space).
      for (uint32_t i=subShape.firstDecal; i<subShape.firstDecal + subShape.numDecals; i++)
      {
         if (checkSet.test(i))
         {
            mRuntimeDecalInfos[i].mFrame = mShape->mDecalStates[i].frame;
         }
      }
      
      forEachSortedThread([&](Dts3::Thread& thread){
         Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
         int32_t startDecalMatters = sequence.mattersDecal.findFirst();
         int32_t endDecal = subShape.firstDecal + subShape.numDecals;
         if (startDecalMatters >= 0)
         {
            int32_t decalFrame = 0;
            for (int32_t dci=startDecalMatters; dci<endDecal; dci = sequence.mattersDecal.findNext(dci))
            {
               if (dci < 0)
               {
                  break;
               }
               
               if (!checkSet.test(dci))
               {
                  int32_t keyNum = thread.keyInfo.keyPos < 0.5f ? thread.keyInfo.keyA : thread.keyInfo.keyB;
                  mRuntimeDecalInfos[dci].mFrame = mShape->getSequenceDecalState(sequence, keyNum, decalFrame++).frame;
                  checkSet.set(dci, true);
               }
            }
         }
      });
   }
   
   // Loading
   
   void loadShape(Dts3::Shape& inShape)
   {
      clear();
      
      mShape = &inShape;
      mMaterialList = &mShape->mMaterials;
      
      initShapeObjects();
      
      initMaterials();
      
      initRender();
      updateScaleAnimatonState();
      if (!mShape->mSequences.empty())
      {
         uint32_t threadIdx = addThread();
         setThreadSequence(threadIdx, 0, 0.0f);
      }
      
      // Setup default pose for nodes
      if (!mShape->mDetailLevels.empty())
      {
         mCurrentDetail = 0;
         animate(mShape->mDetailLevels[mCurrentDetail]);
      }
   }
   
   void initShapeObjects()
   {
      for (Dts3::Node& n : mShape->mNodes)
      {
         n.resetRuntime();
      }
      for (Dts3::Object& o : mShape->mObjects)
      {
         o.resetRuntime();
      }
      
      // Assign sibling nodes
      for (int32_t i=0; i<mShape->mNodes.size(); i++)
      {
         Dts3::Node& n = mShape->mNodes[i];
         int32_t parentIdx = n.parent;
         
         if (parentIdx >= 0)
         {
            if (mShape->mNodes[parentIdx].firstChild < 0)
            {
               mShape->mNodes[parentIdx].firstChild = i;
            }
            else
            {
               int32_t childIdx=mShape->mNodes[parentIdx].firstChild;
               while (mShape->mNodes[childIdx].nextSibling>=0)
               {
                  childIdx = mShape->mNodes[childIdx].nextSibling;
               }
               mShape->mNodes[childIdx].nextSibling = i;
            }
         }
      }
      
      // Assign sibling objects
      for (int32_t i=0; i<mShape->mObjects.size(); i++)
      {
         Dts3::Object& o = mShape->mObjects[i];
         int32_t nodeIdx = o.node;
         
         if (nodeIdx >= 0)
         {
            if (mShape->mNodes[nodeIdx].firstObject < 0)
            {
               mShape->mNodes[nodeIdx].firstObject = i;
            }
            else
            {
               int32_t objectIdx=mShape->mNodes[nodeIdx].firstObject;
               while (mShape->mObjects[objectIdx].nextSibling>=0)
               {
                  objectIdx = mShape->mObjects[objectIdx].nextSibling;
               }
               mShape->mObjects[objectIdx].nextSibling = i;
            }
         }
      }
      
      // Assign sibling decals
      for (int32_t i=0; i<mShape->mDecals.size(); i++)
      {
         Dts3::Decal& d = mShape->mDecals[i];
         int32_t objectIdx = d.object;
      
         if (mShape->mObjects[objectIdx].firstDecal < 0)
         {
            mShape->mObjects[objectIdx].firstDecal = i;
         }
         else
         {
            int32_t decalIdx=mShape->mObjects[objectIdx].firstDecal;
            while (mShape->mDecals[decalIdx].nextSibling>=0)
            {
               decalIdx = mShape->mDecals[decalIdx].nextSibling;
            }
            mShape->mObjects[decalIdx].nextSibling = i;
         }
      }
      
      // Set runtime flag for sequence
      mShape->mRuntimeFlags = 0;
      for (Dts3::Sequence& seq : mShape->mSequences)
      {
         if (!seq.testFlags(Dts3::Shape::AnyScale))
            continue;
         
         uint8_t baseFlag = mShape->mRuntimeFlags & Dts3::Shape::AnyScale;
         uint8_t seqFlag = seq.flags & Dts3::Shape::AnyScale;
         mShape->mRuntimeFlags &= ~Dts3::Shape::AnyScale;
         mShape->mRuntimeFlags |= std::max(baseFlag, seqFlag);
      }
   }
   
   void initVertexBuffer()
   {
      clearVertexBuffer();
      
      /*
       NOTE: We put skin data first, then follow it with basic data. This is so
       we can bind the skin data without dealing with alignment issues.
       */
      
      std::vector<ModelVertex> modelVerts;
      std::vector<ModelTexVertex> modelTexVerts;
      std::vector<ModelSkinVertex> packedSkinVertices;
      std::vector<RuntimeMeshInfo*> skinMeshList;
      std::vector<RuntimeMeshInfo*> basicMeshList;
      std::vector<uint16_t> modelInds;
      
      // Load meshes
      uint32_t count = 0;
      uint32_t vertCount = 0;
      uint32_t indexCount = 0;
      uint32_t skinVertCount = 0;
      
      for (RuntimeMeshInfo& rm : mRuntimeMeshInfos)
      {
         ModelVertex* mv = NULL;
         Dts3::BasicData* bd = rm.mMesh->getBasicData();
         Dts3::BasicData* sd = rm.mMesh->getSkinData();
         
         if (sd)
         {
            rm.mVertCount = sd->verts.size();
            skinVertCount += sd->verts.size();
            skinMeshList.push_back(&rm);
            rm.mUseSkinData = true;
         }
         else if (bd)
         {
            rm.mVertCount = bd->verts.size();
            vertCount += bd->verts.size();
            basicMeshList.push_back(&rm);
            rm.mUseSkinData = false;
         }
         
         if (bd)
         {
            rm.mRealVertsPerFrame = rm.mMesh->mVertsPerFrame;
            rm.mIndexCount = (uint32_t)bd->indices.size();
            indexCount += bd->indices.size();
         }
         else
         {
            rm.mRealVertsPerFrame = 0;
            rm.mIndexCount = 0;
         }
      }
      
      modelVerts.resize(vertCount + skinVertCount);
      modelTexVerts.resize(vertCount + skinVertCount);
      if (skinMeshList.size() != 0)
      {
         packedSkinVertices.resize(vertCount + skinVertCount);
      }
      modelInds.resize(indexCount);
      
      skinVertCount = 0;
      indexCount = 0;
      for (RuntimeMeshInfo* rm : skinMeshList)
      {
         Dts3::SkinData* sd = rm->mMesh->getSkinData();
         
         // Copy verts
         Dts3::EmitModelVertices(sd, &modelVerts[skinVertCount]);
         Dts3::EmitModelTexVertices(sd, &modelTexVerts[skinVertCount]);
         Dts3::EmitPackedSkinVertices(sd, &packedSkinVertices[skinVertCount]);
         memcpy(&modelInds[indexCount], &sd->indices[0], sizeof(uint16_t) * sd->indices.size());
         
         // Count & offsets
         rm->mVertOffset = skinVertCount;
         rm->mIndexOffset = indexCount;
         skinVertCount += rm->mVertCount;
         indexCount += rm->mIndexCount;
      }
      
      vertCount = skinVertCount;
      for (RuntimeMeshInfo* rm : basicMeshList)
      {
         Dts3::BasicData* bd = rm->mMesh->getBasicData();
         
         // Copy verts
         Dts3::EmitModelVertices(bd, &modelVerts[vertCount]);
         Dts3::EmitModelTexVertices(bd, &modelTexVerts[vertCount]);
         memcpy(&modelInds[indexCount], &bd->indices[0], sizeof(uint16_t) * bd->indices.size());
         
         // Count & offsets
         rm->mVertOffset = vertCount;
         rm->mIndexOffset = indexCount;
         vertCount += rm->mVertCount;
         indexCount += rm->mIndexCount;
      }
      
      GFXLoadModelData(0,
                       modelVerts.empty() ? NULL : &modelVerts[0],
                       modelTexVerts.empty() ? NULL : &modelTexVerts[0],
                       modelInds.empty() ? NULL : &modelInds[0],
                       packedSkinVertices.empty() ? NULL : &packedSkinVertices[0],
                       (uint32_t)modelVerts.size(),
                       (uint32_t)modelTexVerts.size(),
                       (uint32_t)modelInds.size());
      initVB = true;
   }
   
   void clearVertexBuffer()
   {
      if (!initVB)
         return;
      
      GFXLoadModelData(0, NULL, NULL, NULL, NULL, 0, 0, 0);
      initVB = false;
   }
   
   // Rendering
   
   void determineNodeVisibility()
   {
   }
   
   void selectDetail(float dist, int w, int h)
   {
      mCurrentDetail = -1;
      for (int32_t i=0; i<mShape->mDetailLevels.size(); i++)
      {
         Dts3::DetailLevel& detail = mShape->mDetailLevels[i];
         if (detail.subshape < 0 || detail.objectDetail < 0)
            continue;
         mCurrentDetail = i;
         break;
      }
   }
   
   void drawLine(slm::vec3 start, slm::vec3 end, slm::vec4 color, float width)
   {
      updateMVP();
      GFXBeginLinePipelineState();
      GFXDrawLine(start, end, color, width);
   }
   
   void render()
   {
      if (mCurrentDetail < 0 || mCurrentDetail >= mShape->mDetailLevels.size())
         return;

      animate(mShape->mDetailLevels[mCurrentDetail]);
      renderDetail(mCurrentDetail);
   }
   
   void renderObject(uint32_t objectIndex, uint32_t meshNum)
   {
      Dts3::Object& obj = mShape->mObjects[objectIndex];
      if (meshNum >= obj.numMeshes)
         return;
      
      RuntimeObjectInfo& ri = mRuntimeObjectInfos[objectIndex];
      RuntimeMeshInfo& mi = mRuntimeMeshInfos[obj.firstMesh + meshNum];
      
      // Need to take different paths here
      Dts3::SkinData* sd = mi.mMesh->getSkinData();
      Dts3::BasicData* bd = mi.mMesh->getBasicData();
      Dts3::SortedData* sort = mi.mMesh->getSortedData();
      
      /*
       General logic:
       
       - Node transform texture gets updated for all mesh types
       - A secondary texture is used to provide initial transforms
          - Static meshes use the same base identity matrices for initial transforms
          - Skinned meshes use static sets of base matrices custom per mesh
       - Render uniforms set:
          - The base texture offset
          - The initial transforms offset
          - Dimensions both both textures
       - Vertex shader transforms vertices according to transform lookups
       - Fragment shader applies core and extra features with flags in uniforms
       
       */
      
      if (sort)
      {
         // Sorted meshes have an array for offsets
         // NOTE: tverts or verts change here, not both.
         uint32_t nc = std::min<uint32_t>(mi.mMeshFrame, sort->numVerts.size());
         uint32_t mf = std::min<uint32_t>(mi.mMeshFrame, sort->firstVerts.size());
         uint32_t tf = std::min<uint32_t>(mi.mMeshTexFrame ? mi.mMeshTexFrame : mi.mMeshFrame, sort->firstTVerts.size());
         
         // NOTE: ideally we should render in cluster order here, but
         // to keep things simple we'll just let the GPU do all the work.
         renderMesh(mi, bd,
                    sort->numVerts[nc],
                    sort->firstVerts[mf],
                    sort->firstTVerts[tf],
                    true);
      }
      else if (bd)
      {
         Dts3::SortedData* sd = mi.mMesh->getSortedData();
         renderMesh(mi, bd,
                    mi.mRealVertsPerFrame,
                    (mi.mMeshFrame    * mi.mRealVertsPerFrame),
                    (mi.mMeshTexFrame * mi.mRealVertsPerFrame),
                    false);
      }
      else
      {
         Dts3::DecalData* dd = mi.mMesh->getDecalData();
         if (dd)
         {
            RuntimeMeshInfo& smi = mRuntimeMeshInfos[dd->meshIndex];
            renderDecal(mi, smi, bd, dd);
         }
      }
   }
   
   ModelPipelineState calcPipelineState(uint32_t flags) const
   {
      if ((flags & (MaterialList::Additive | MaterialList::Subtractive)) != 0)
      {
         if ((flags & (MaterialList::Additive)) != 0)
            return ModelPipeline_AdditiveBlend;
         else
            return ModelPipeline_SubtractiveBlend;
      }
      else if ((flags & MaterialList::Translucent) != 0)
      {
         return ModelPipeline_TranslucentBlend;
      }
      else
      {
         return ModelPipeline_DefaultDiffuse;
      }
   }
   
   void renderDecal(RuntimeMeshInfo& mi, RuntimeMeshInfo& smi, Dts3::BasicData* bd, Dts3::DecalData* dd)
   {
      GFXSetModelVerts(0, 0, 0, 0);
      GFXSetModelViewProjection(mModelMatrix, mViewMatrix, mProjectionMatrix, smi.mRenderFlags);
      GFXSetTSPipelineProps(mi.mMeshTexFrame, smi.mMeshTransformOffset, dd->texGenS[mi.mMeshFrame], dd->texGenT[mi.mMeshFrame]);
      
      uint32_t start = dd->startPrimitive[mi.mMeshFrame];
      uint32_t end = mi.mMeshFrame+1 < dd->startPrimitive.size() ? dd->startPrimitive[mi.mMeshFrame+1] : dd->primitives.size();
      
      for (uint32_t i=start; i<end; i++)
      {
         Dts3::Primitive& prim = dd->primitives[i];
         uint32_t matIndex = dd->matIndex & Dts3::Primitive::MaterialMask;
         uint32_t drawMode = prim.matIndex & Dts3::Primitive::TypeMask;
         
         // To keep things simple, everything is assembled into a single texture group, though
         // this should not include env maps and whatnot.
         const MaterialList::Material& mat = mMaterialList->operator[](matIndex);
         ActiveMaterial& amat = mActiveMaterials[matIndex];
         uint32_t groupID = amat.tex.texID; // TOOD
         
         ModelPipelineState pipelineState = calcPipelineState(mat.tsProps.flags);
         GFXBeginTSModelPipelineState(pipelineState,
                                      groupID,
                                      1.1f, false, false);
         
         assert(drawMode == Dts3::Primitive::Triangles);
         
         GFXDrawModelPrims(smi.mRealVertsPerFrame,
                           prim.numElements,
                           mi.mIndexOffset + prim.firstElement,
                           mi.mVertOffset + (smi.mMeshFrame * smi.mRealVertsPerFrame));
      }
   }
   
   void renderMesh(RuntimeMeshInfo& mi, Dts3::BasicData* bd, uint32_t drawVerts, uint32_t firstVert, uint32_t firstTVert, bool depthPeel=false)
   {
      GFXSetModelVerts(0, 0, 0, 0);
      GFXSetModelViewProjection(mModelMatrix, mViewMatrix, mProjectionMatrix, mi.mRenderFlags);
      GFXSetTSPipelineProps(mi.mMeshTexFrame, mi.mMeshTransformOffset, slm::vec4(0), slm::vec4(0));
      
      // NOTE: if we wanted to more optimally batch, emitting a drawcall per matIndex would make
      // more sense here.
      // Unfortunately we can't use texture arrays for everything here since the material list
      // doesn't guarantee that every texture is consistently sized.
      
      uint32_t passes = depthPeel ? 4 : 1;
      
      for (uint32_t i=0; i<passes; i++)
      {
         for (Dts3::Primitive& prim : bd->primitives)
         {
            uint32_t matIndex = prim.matIndex & Dts3::Primitive::MaterialMask;
            uint32_t drawMode = prim.matIndex & Dts3::Primitive::TypeMask;
            
            // To keep things simple, everything is assembled into a single texture group.
            // IFL materials make use of the texture array feature.
            MaterialList::Material& mat = mMaterialList->operator[](matIndex);
            ActiveMaterial& amat = mActiveMaterials[matIndex];
            uint32_t groupID = amat.texGroupID; // TODO
            
            ModelPipelineState pipelineState = calcPipelineState(mat.tsProps.flags);
            GFXBeginTSModelPipelineState(pipelineState, 
                                         groupID,
                                         1.1f, depthPeel, (passes % 2) == 0);
            
            assert(drawMode == Dts3::Primitive::Triangles);
            
            GFXDrawModelPrims(mi.mRealVertsPerFrame,
                              prim.numElements,
                              mi.mIndexOffset + prim.firstElement,
                              mi.mVertOffset + (mi.mMeshFrame * mi.mRealVertsPerFrame));
         }
      }
   }
   
   void renderDetail(uint32_t detailLevel)
   {
      Dts3::DetailLevel& level = mShape->mDetailLevels[detailLevel];
      if (level.subshape < 0)
      {
         // render billboard
         return;
      }
      
      if (level.objectDetail < 0)
         return;
      
      // Setup IFL
      // TODO
      
      Dts3::SubShape& ss = mShape->mSubshapes[level.subshape];
      if (ss.firstTranslucent < 0)
      {
         ss.firstTranslucent = ss.firstObject + ss.numObjects;
      }
      
      // NOTE: The original render code treats all meshes as separate and renders them one-by-one.
      // Instead we opt to stick everything in a single vertex buffer and render everything in two main batches.
      
      for (uint32_t i=ss.firstObject; i<ss.firstTranslucent; i++)
      {
         renderObject(i, level.objectDetail);
      }
      for (uint32_t i=ss.firstTranslucent; i<ss.firstObject+ss.numObjects; i++)
      {
         renderObject(i, level.objectDetail);
      }
   }
   
   void renderNodes(int32_t nodeIdx, slm::vec3 parentPos, int32_t highlightIdx)
   {
      if (nodeIdx < 0)
      {
         for (int32_t i=0; i<mShape->mNodes.size(); i++)
         {
            if (mShape->mNodes[i].parent < 0)
            {
               renderNodes(i, parentPos, highlightIdx);
            }
         }
         return;
      }
      
      
#if 0
      slm::mat4 firstXfm = slm::inverse(mNodeTransforms[0]);
      slm::mat4 baseModel = mModelMatrix;
      slm::mat4 y_up = slm::rotation_x(slm::radians(-90.0f));
      
      slm::mat4 slmMat = mNodeTransforms[nodeIdx];
      
      slmMat[3] = slm::vec4(0,0,0,1);
      //slmMat = slm::transpose(slmMat);
      slmMat[3] = mNodeTransforms[nodeIdx][3];
      
      assert(slmMat[3].w == 1);
      
      slm::vec4 pos = baseModel * y_up * firstXfm * slmMat * slm::vec4(0,0,0,1);
      
      drawLine(pos.xyz(), parentPos, nodeIdx == highlightIdx ? slm::vec4(0,1,0,1) : slm::vec4(1,0,0,1), 1);
      
      // Recurse
      Shape::NodeChildInfo info = mShape->mNodeChildren[nodeIdx+1];
      for (int32_t i=0; i<info.numChildren; i++)
      {
         renderNodes(mShape->mNodeChildIds[info.firstChild+i], pos.xyz(), highlightIdx);
      }
#endif
   }
};

class ShapeViewerController : public ViewController
{
public:
   ShapeViewer mViewer;
   SDL_Window* mWindow;
   float xRot, yRot, mDetailDist;
   Dts3::Shape* mShape;
   int32_t mHighlightNodeIdx;
   
   std::vector<const char*> mSequenceList;
   std::vector<int> mNextSequence;
   
   int32_t mRemoveThreadId;
   bool mRenderNodes;
   bool mManualThreads;
   
   ShapeViewerController(SDL_Window* window, ResManager* mgr) :
   mViewer(mgr)
   {
      mViewPos = slm::vec3(0,0,0);
      mCamRot = slm::vec3(0,0,0);
      mViewer.initRender();
      mWindow = window;
      xRot = mDetailDist = 0;
      yRot = slm::radians(180.0f);
      mShape = NULL;
      mHighlightNodeIdx = -1;
      mRemoveThreadId = -1;
      mRenderNodes = true;
      mManualThreads = false;
   }
   
   ~ShapeViewerController()
   {
      if (mShape)
         delete mShape;
   }
   
   bool isResourceLoaded()
   {
      return mShape != NULL;
   }
   
   void rebuildSequenceUI()
   {
      mSequenceList.clear();
      mNextSequence.clear();

      if (mShape == NULL)
         return;

      mSequenceList.reserve(mShape->mSequences.size());
      for (Dts3::Sequence& seq : mShape->mSequences)
      {
         const char* name = mShape->getName(seq.nameIndex);
         mSequenceList.push_back(name ? name : "<unnamed>");
      }

      mNextSequence.resize(mViewer.mThreads.size(), -1);
      for (size_t i=0; i<mViewer.mThreads.size(); i++)
      {
         mNextSequence[i] = mViewer.mThreads[i].sequenceIdx;
      }
   }

   void updateNextSequence()
   {
      mNextSequence.resize(mViewer.mThreads.size());
      for (size_t i=0; i<mViewer.mThreads.size(); i++)
      {
         mNextSequence[i] = mViewer.mThreads[i].sequenceIdx;
      }
   }
   
   void loadShape(const char *filename, int pathIdx=-1)
   {
      mViewer.clear();
      if (mShape)
         delete mShape;
      mShape = NULL;
      
      ResourceInstance* inst = mViewer.mResourceManager->createResource(filename, pathIdx);
      
      if (inst)
      {
         Dts3::Shape* shape = (Dts3::Shape*)inst;
         mShape = shape;
         mViewer.clear();
         mViewer.setResourcePath(filename, pathIdx);
         mViewer.loadShape(*mShape);
         rebuildSequenceUI();
         
         mViewPos = slm::vec3(0);//slm::vec3(0, mViewer.mShape->mCenter.z, mViewer.mShape->mRadius);
      }
   }

   bool loadDSQ(const char *filename, int pathIdx=-1)
   {
      if (mShape == NULL)
      {
         fprintf(stderr, "cannot load DSQ '%s' without a loaded shape\n", filename);
         return false;
      }

      MemRStream stream(0, NULL);
      if (!mViewer.mResourceManager->openFile(filename, stream, pathIdx))
      {
         fprintf(stderr, "failed to open DSQ '%s'\n", filename);
         return false;
      }

      if (!Dts3::IO::readDSQSequences(mShape, stream, Dts3::DefaultVersion, false, true, true))
      {
         fprintf(stderr, "failed to import DSQ '%s'\n", filename);
         return false;
      }

      mViewer.clear();
      mViewer.loadShape(*mShape);
      rebuildSequenceUI();
      return true;
   }
   
   void update(float dt)
   {
      mViewer.mModelMatrix = slm::rotation_x(xRot) * slm::rotation_y(yRot);
      slm::mat4 rotMat = slm::rotation_z(slm::radians(mCamRot.z)) * slm::rotation_y(slm::radians(mCamRot.y)) *  slm::rotation_x(slm::radians(mCamRot.x));
      rotMat = inverse(rotMat);
      mViewer.mViewMatrix = slm::mat4(1) * rotMat * slm::translation(-mViewPos);
      
      int w, h;
      SDL_GetWindowSize(mWindow, &w, &h);
      mViewer.mProjectionMatrix = slm::perspective_fov_rh( slm::radians(90.0), (float)w/(float)h, 0.01f, 10000.0f);
      
      if (!mManualThreads)
         mViewer.advanceThreads(dt);
      mViewer.selectDetail(mDetailDist, w, h);
      mViewer.render();
      if (mRenderNodes)
      {
         mViewer.renderNodes(-1, slm::vec3(0,0,0), mHighlightNodeIdx);
      }
      
      // Now render gui
      ImGui::Begin("Nodes");
      ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.75);
      nodeTree(0);
      ImGui::PopStyleVar(1);
      ImGui::End();
      
      ImGui::Begin("Anim");
      char buffer[1024];
      
      if (ImGui::Button("Add Thread"))
      {
         uint32_t threadIdx = mViewer.addThread();
         if (!mShape->mSequences.empty())
         {
            mViewer.setThreadSequence(threadIdx, 0, 0.0f);
         }
         updateNextSequence();
      }
      
      ImGui::SameLine();
      ImGui::Checkbox("Manual Control", &mManualThreads);
      
      if (mRemoveThreadId >= 0)
      {
         mViewer.removeThread(mRemoveThreadId);
         mRemoveThreadId = -1;
      }
      
      for (Dts3::Thread &thread : mViewer.mThreads)
      {
         int32_t idx = &thread - &mViewer.mThreads[0];
         snprintf(buffer, 1024, "Thread %i", idx);
         
         bool vis = ImGui::CollapsingHeader(buffer);
         ImGui::SameLine();
         
         if (thread.sequenceIdx == -1 || thread.sequenceIdx >= mViewer.mShape->mSequences.size())
         {
            snprintf(buffer, 1024, "INVALID");
         }
         else
         {
            snprintf(buffer, 1024, "seq=%s pos=%f",
                     thread.sequenceIdx == -1 ? "NULL" : mShape->getName(mShape->mSequences[thread.sequenceIdx].nameIndex),
                     thread.pos);
         }
         
         ImGui::Text("%s", buffer);
         
         if (vis)
         {
            bool playing = mViewer.mThreads[idx].playing;
            snprintf(buffer, 1024, "Playing##th%i", idx);
            if (ImGui::Checkbox(buffer, &playing))
            {
               mViewer.mThreads[idx].playing = playing;
               if (idx < mViewer.mThreadActive.size())
                  mViewer.mThreadActive[idx] = playing && mViewer.mThreads[idx].sequenceIdx >= 0;
            }
            ImGui::SameLine();
            snprintf(buffer, 1024, "Remove##th%i", idx);
            if (ImGui::Button(buffer))
               mRemoveThreadId = idx;
            snprintf(buffer, 1024, "Pos##th%i", idx);
            if (ImGui::SliderFloat(buffer, &mViewer.mThreads[idx].pos, 0.0f, 1.0f) &&
                mViewer.mThreads[idx].sequenceIdx >= 0 &&
                mViewer.mThreads[idx].sequenceIdx < mShape->mSequences.size())
            {
               mViewer.selectKeyFrames(mViewer.mThreads[idx].pos,
                                       mShape->mSequences[mViewer.mThreads[idx].sequenceIdx],
                                       mViewer.mThreads[idx].keyInfo);
               mViewer.setDirty(ShapeViewer::RuntimeSubShapeInfo::AllDirty);
            }
            ImGui::NewLine();
            if (!mSequenceList.empty())
            {
               snprintf(buffer, 1024, "Sequences##th%i", idx);
               ImGui::ListBox(buffer, &mNextSequence[idx], mSequenceList.data(), (int)mSequenceList.size());
            }
         }
      }
      
      ImGui::End();
      
      ImGui::Begin("View");
      ImGui::SliderAngle("X Rotation", &xRot);
      ImGui::SliderAngle("Y Rotation", &yRot);
      ImGui::SliderFloat("Detail Distance", &mDetailDist, 0, 1000.0f);
      ImGui::Checkbox("Render Nodes", &mRenderNodes);
      ImGui::End();
      
      // Update state changed by gui
      for (size_t i=0; i<mNextSequence.size(); i++)
      {
         if (mNextSequence[i] != mViewer.mThreads[i].sequenceIdx)
         {
            mViewer.setThreadSequence(i, mNextSequence[i], mViewer.mThreads[i].pos);
         }
      }
   }
   
   void nodeTree(int32_t nodeIdx)
   {
      if (nodeIdx < 0)
      {
         for (int32_t i=0; i<mShape->mNodes.size(); i++)
         {
            if (mShape->mNodes[i].parent < 0)
            {
               nodeTree(i);
            }
         }
         return;
      }

      uint32_t baseFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;

      bool visDetail = false;
      if (mViewer.mCurrentDetail >= 0 && mViewer.mCurrentDetail < mShape->mDetailLevels.size())
      {
         Dts3::DetailLevel& detail = mShape->mDetailLevels[mViewer.mCurrentDetail];
         if (detail.subshape >= 0 && detail.subshape < mShape->mSubshapes.size())
         {
            Dts3::SubShape& subShape = mShape->mSubshapes[detail.subshape];
            int32_t endNode = subShape.firstNode + subShape.numNodes;
            visDetail = nodeIdx >= subShape.firstNode && nodeIdx < endNode;
         }
      }
      
      if (visDetail)
      {
         ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0);
      }

      bool hasChildren = mShape->mNodes[nodeIdx].firstChild >= 0;
      bool hasObjects = mShape->mNodes[nodeIdx].firstObject >= 0;
      bool vis = ImGui::TreeNodeEx(
         mShape->getName(mShape->mNodes[nodeIdx].name),
         (hasChildren || hasObjects) ? baseFlags : (baseFlags | ImGuiTreeNodeFlags_Leaf));
      if (ImGui::IsItemClicked())
      {
         mHighlightNodeIdx = nodeIdx;
      }
      
      if (vis)
      {
         for (int32_t objectIdx = mShape->mNodes[nodeIdx].firstObject;
              objectIdx >= 0;
              objectIdx = mShape->mObjects[objectIdx].nextSibling)
         {
            ImGui::BulletText("Object: %s", mShape->getName(mShape->mObjects[objectIdx].name));
         }

         for (int32_t childIdx = mShape->mNodes[nodeIdx].firstChild;
              childIdx >= 0;
              childIdx = mShape->mNodes[childIdx].nextSibling)
         {
            nodeTree(childIdx);
         }
         ImGui::TreePop();
      }
      
      if (visDetail)
      {
         ImGui::PopStyleVar(1);
      }
   }
   
};


static const uint64_t tickMS = 1000.0 / 60;

struct MainState
{
   ResManager resManager;
   ShapeViewerController* shapeController;
   //InteriorViewerController* interiorController;
   //TerrainViewerController* terrainController;
   ViewController *currentController;
   
   //
   slm::vec3 deltaMovement;
   slm::vec3 deltaRot;
   slm::vec3 testPos;
   uint64_t lastTicks;
   
   int selectedFileIdx;
   int selectedVolumeIdx;
   std::vector<ResManager::EnumEntry> fileList;
   std::vector<std::string> restrictExtList;
   std::vector<const char*> cFileList;
   std::vector<std::string> sFileList;
   std::vector<const char*> cVolumeList;
   
   int oldSelectedVolumeIdx;
   int oldSelectedFileIdx;
   //
   
   int in_argc;
   const char** in_argv;
   
   bool isGFXSetup;
   bool running;
   
   SDL_Window* window;
   
   MainState() : shapeController(NULL), /* interiorController(NULL), terrainController(NULL),*/ currentController(NULL), in_argc(0), isGFXSetup(false)
   {
      lastTicks = 0;
      selectedFileIdx = -1;
      selectedVolumeIdx = -1;
      oldSelectedVolumeIdx = -1;
      oldSelectedFileIdx = -1;
      running = false;
      window = NULL;
      
      testPos = slm::vec3(0);
   }
   
   void init(SDL_Window* in_window, int argc, const char** argv)
   {
      window = in_window;
      in_argc = argc;
      in_argv = argv;
      
      shapeController = new ShapeViewerController(window, &resManager);
      //interiorController = new InteriorViewerController(window, &resManager);
      //terrainController = new TerrainViewerController(window, &resManager);
   }
   
   int boot();
   int loop();
   
   int testBoot();
   int testLoop();
   
   void shutdown();
};

static MainState gMainState;


template<class T> static ResourceInstance* _createClass() { return new T(); }

void ResManager::initStatics()
{
   registerCreateFunc(".dts", _createClass<Dts3::Shape>);
}


int main(int argc, const char * argv[])
{
   SDL_Window* window = NULL;
   SDL_Renderer* renderer = NULL;
   
   assert(sizeof(slm::vec2) == 8);
   assert(sizeof(slm::vec3) == 12);
   assert(sizeof(slm::vec4) == 16);
   
   ConsolePersistObject::initStatics();
   ResManager::initStatics();
   
   if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
      printf("Couldn't initialize SDL: %s\n", SDL_GetError());
      return (1);
   }
   
   if (!SDL_CreateWindowAndRenderer("DTS Viewer", 1024, 700, SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE, &window, &renderer)) {
      printf( "Window could not be created! SDL_Error: %s\n", SDL_GetError() );
      return (1);
   }
   
   // Init basic main
   gMainState.init(window, argc, argv);
   
   int setupCode = GFXSetup(window, renderer);
   
   if (setupCode < 0)
   {
      return 1;
   }
   
   // Non-Emscripten setup
   while (setupCode != 0)
   {
      setupCode = GFXSetup(window, renderer);
   }
   
   int ret = gMainState.boot();
   if (ret != 0)
      return ret;
   
   while (gMainState.loop() == 0)
   {
      ;
   }
   
   gMainState.shutdown();
   
   return 0;
}

void MainState::shutdown()
{
   if (shapeController)
   {
      delete shapeController;
      //delete interiorController;
      //delete terrainController;
      shapeController = NULL;
      //interiorController = NULL;
      //terrainController = NULL;
   }
   
   GFXTeardown();
   SDL_DestroyWindow( gMainState.window );
   SDL_Quit();
}

int MainState::boot()
{
   currentController = shapeController;
   
   for (int i=1; i<in_argc; i++)
   {
      const char *path = in_argv[i];
      if (path && path[0] == '-')
         break;
      
      fs::path filePath = path;
      std::string  ext = filePath.extension();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      
      if (ext == ".dts")
      {
         shapeController->loadShape(path);
         currentController = shapeController;
      }
      else if (ext == ".dsq")
      {
         if (!shapeController->loadDSQ(path))
         {
            return 1;
         }
         currentController = shapeController;
      }
      else if (ext == ".vol" || ext == ".zip")
      {
         resManager.addVolume(path);
      }
      else if (ext == ".dif")
      {
         //interiorController->loadInterior(path);
         //currentController = interiorController;
      }
      else if (ext == ".ter")
      {
         //terrainController->loadSingleBlock(path);
         //currentController = terrainController;
      }
      else if (ext == "")
      {
         resManager.mPaths.emplace_back(path);
      }
   }
   
   if (!currentController->isResourceLoaded())
   {
      fprintf(stderr, "please specify a starting shape or interior or terrain to load\n");
      return 1;
   }
   
   running = true;
   
   deltaMovement = slm::vec3(0);
   deltaRot = slm::vec3(0);
   lastTicks = SDL_GetTicks();
   
   selectedFileIdx = -1;
   selectedVolumeIdx = -1;
   restrictExtList.clear();
   fileList.clear();
   restrictExtList.push_back(".dts");
   restrictExtList.push_back(".dif");
   restrictExtList.push_back(".ter");
   resManager.enumerateFiles(fileList, selectedVolumeIdx, &restrictExtList);
   sFileList.resize(fileList.size());
   
   for (int i=0; i<fileList.size(); i++)
   {
      sFileList[i] = fileList[i].filename;
   }
   
   for (int i=0; i<fileList.size(); i++)
   {
      cFileList.push_back(sFileList[i].c_str());
   }
   
   resManager.enumerateSearchPaths(cVolumeList);
   
   oldSelectedVolumeIdx = -1;
   oldSelectedFileIdx = -1;
   
   return 0;
}

int MainState::loop()
{
   if (!running)
      return 1;
   
   ImGui::StyleColorsDark();
   
   SDL_Event event;
   
   uint64_t curTicks = SDL_GetTicks();
   uint64_t oldLastTicks = lastTicks;
   float dt = ((float)(curTicks - lastTicks)) / 1000.0f;
   lastTicks = curTicks;
   
   currentController->mCamRot += deltaRot * dt * 100;
   slm::mat4 rotMat = slm::rotation_z(slm::radians(currentController->mCamRot.z)) * slm::rotation_y(slm::radians(currentController->mCamRot.y)) *  slm::rotation_x(slm::radians(currentController->mCamRot.x));
   //rotMat = inverse(rotMat);
   slm::vec4 forwardVec = rotMat * slm::vec4(deltaMovement, 1);
   currentController->mViewPos += forwardVec.xyz() * currentController->mViewSpeed * dt;
   
   int w, h;
   SDL_GetWindowSize(window, &w, &h);
   
   //glViewport(0,0,w,h);
   
   if (oldSelectedVolumeIdx != selectedVolumeIdx)
   {
      fileList.clear();
      resManager.enumerateFiles(fileList, selectedVolumeIdx, &restrictExtList);
      oldSelectedVolumeIdx = selectedVolumeIdx;
      
      cFileList.clear();
      sFileList.resize(fileList.size());
      for (int i=0; i<fileList.size(); i++)
      {
         sFileList[i] = fileList[i].filename;
      }
      for (int i=0; i<fileList.size(); i++)
      {
         cFileList.push_back(sFileList[i].c_str());
      }
      
      oldSelectedFileIdx = selectedFileIdx = -1;
   }
   
   if (oldSelectedFileIdx != selectedFileIdx)
   {
      fs::path filePath = cFileList[selectedFileIdx];
      std::string  ext = filePath.extension();
      
      if (ext == ".dif")
      {
         //interiorController->loadInterior(cFileList[selectedFileIdx], selectedVolumeIdx);
         //currentController = interiorController;
      }
      else if (ext == ".ter")
      {
         //errainController->loadSingleBlock(cFileList[selectedFileIdx], selectedVolumeIdx);
         //currentController = terrainController;
      }
      else
      {
         shapeController->loadShape(cFileList[selectedFileIdx], selectedVolumeIdx);
         currentController = shapeController;
      }

      oldSelectedFileIdx = selectedFileIdx;
   }
   
   while (SDL_PollEvent(&event))
   {
      ImGui_ImplSDL3_ProcessEvent(&event);
      
      switch (event.type)
      {
         case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
         case SDL_EVENT_WINDOW_RESIZED:
            GFXHandleResize();
            break;
            
         case SDL_EVENT_KEY_DOWN:
         case SDL_EVENT_KEY_UP:
         {
            slm::vec3 forwardVec = slm::vec3();
            switch (event.key.key)
            {
               case SDLK_A:  deltaMovement.x = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_D:  deltaMovement.x = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_Q:  deltaMovement.y = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_E:  deltaMovement.y = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_W:  deltaMovement.z = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_S:  deltaMovement.z = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_LEFT:  deltaRot.y = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_RIGHT: deltaRot.y = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_UP:  deltaRot.x = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_DOWN: deltaRot.x = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
            }
         }
            break;
            
         case SDL_EVENT_QUIT:
            running = false;
            break;
      }
   }
   
   if (GFXBeginFrame())
   {
      currentController->update(dt);
      
      ImGui::Begin("Browse");
      ImGui::Columns(2);
      ImGui::ListBox("##bvols", &selectedVolumeIdx, cVolumeList.data(), cVolumeList.size());
      ImGui::NextColumn();
      ImGui::ListBox("##bfiles", &selectedFileIdx, cFileList.data(), cFileList.size());
      ImGui::End();
      
      GFXEndFrame();
   }
   else
   {
      lastTicks = oldLastTicks;
   }
   
   uint64_t endTicks = SDL_GetTicks();
   if (endTicks - lastTicks < tickMS)
   {
      SDL_Delay(tickMS - (endTicks - lastTicks));
   }
   
   return running ? 0 : 1;
}

int MainState::testBoot()
{
   lastTicks = SDL_GetTicks();
   testPos = slm::vec3(0,0,0);
   deltaMovement = slm::vec3(0,0,0);
   deltaRot = slm::vec3(0,0,0);
}

int MainState::testLoop()
{
   if (!running)
      return 1;
   
   uint64_t curTicks = SDL_GetTicks();
   float dt = ((float)(curTicks - lastTicks)) / 1000.0f;
   lastTicks = curTicks;
   
   int w, h;
   SDL_GetWindowSize(window, &w, &h);
   
   testPos += deltaMovement * dt * 100;
   
   //SDL_SetRenderDrawColor(renderer, 255, 255, 255, 0);
   //SDL_RenderClear(renderer);
   //SDL_RenderCopy(renderer, bitmapTex, NULL, NULL);
   
   GFXTestRender(testPos);
   
   //SDL_RenderPresent(renderer);
   
   SDL_Event event;
   while (SDL_PollEvent(&event))
   {
      switch (event.type)
      {
         case SDL_EVENT_KEY_DOWN:
         case SDL_EVENT_KEY_UP:
         {
            switch (event.key.key)
            {
               case SDLK_A:  deltaMovement.x = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_S: deltaMovement.x = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_Q:    deltaMovement.y = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_E:  deltaMovement.y = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_W:  deltaMovement.z = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_D:  deltaMovement.z = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
            }
         }
            break;
            
         case SDL_EVENT_QUIT:
            running = false;
            break;
      }
   }
   
   return running ? 0 : 1;
}
