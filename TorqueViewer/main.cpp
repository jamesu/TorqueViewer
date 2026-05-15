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
#include <unordered_set>
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
static const slm::mat4 kTorqueZUpViewBasis = slm::rotation_x(slm::radians(-90.0f));

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
   if( rot.x*rot.x + rot.y*rot.y + rot.z*rot.z + rot.w*rot.w < 10E-20f)
   {
      outMat = slm::mat4(1);
      return;
   }
   
   // NOTE: torques quaternions rotate the other way around from the slm convention
   outMat = slm::mat4(slm::conjugate(rot));
}


#include "CommonData.h"


void ConsolePersistObject::initStatics()
{
}


class GenericViewer
{
public:
   static const std::array<const char*, 4>& getKnownTextureExtensions()
   {
      static const std::array<const char*, 4> kKnownTextureExtensions = {{
         ".bmp",
         ".gif",
         ".png",
         ".jpg",
      }};
      return kKnownTextureExtensions;
   }

   static std::string normalizeResourceSlashes(std::string path)
   {
      std::replace(path.begin(), path.end(), '\\', '/');
      return path;
   }

   static bool tryOpenTextureCandidate(ResManager* resourceManager, const std::string& candidate, MemRStream& outStream, int32_t mountIdx, std::string* outResolvedPath = NULL)
   {
      fprintf(stderr, "texture search: trying candidate='%s' mount=%d\n", candidate.c_str(), mountIdx);
      if (!resourceManager->openFile(candidate.c_str(), outStream, mountIdx))
         return false;

      fprintf(stderr, "texture search: opened candidate='%s' mount=%d\n", candidate.c_str(), mountIdx);
      if (outResolvedPath)
         *outResolvedPath = candidate;
      return true;
   }

   static bool pathHasPrefix(const fs::path& path, const fs::path& prefix)
   {
      auto pathItr = path.begin();
      auto prefixItr = prefix.begin();
      for (; prefixItr != prefix.end(); ++prefixItr, ++pathItr)
      {
         if (pathItr == path.end() || *pathItr != *prefixItr)
            return false;
      }
      return true;
   }

   static bool pathEndsWith(const fs::path& path, const fs::path& suffix)
   {
      std::vector<fs::path> pathParts(path.begin(), path.end());
      std::vector<fs::path> suffixParts(suffix.begin(), suffix.end());
      if (suffixParts.size() > pathParts.size())
         return false;

      const size_t startIndex = pathParts.size() - suffixParts.size();
      for (size_t i = 0; i < suffixParts.size(); ++i)
      {
         if (pathParts[startIndex + i] != suffixParts[i])
            return false;
      }
      return true;
   }

   static bool tryOpenTextureFromFilesystemMount(ResManager* resourceManager, const std::string& candidate, const fs::path& searchRoot, int32_t mountIdx, MemRStream& outStream, std::string* outResolvedPath = NULL)
   {
      if (mountIdx < 0 || mountIdx >= (int32_t)resourceManager->mPaths.size())
         return false;

      const fs::path mountPath = fs::path(resourceManager->mPaths[mountIdx]);
      const fs::path absoluteRoot = searchRoot.empty() ? mountPath : (mountPath / searchRoot);
      fprintf(stderr, "texture search: filesystem mount=%d root='%s' candidate='%s'\n",
              mountIdx, absoluteRoot.generic_string().c_str(), candidate.c_str());
      if (!fs::exists(absoluteRoot) || !fs::is_directory(absoluteRoot))
      {
         fprintf(stderr, "texture search: filesystem root missing mount=%d root='%s'\n",
                 mountIdx, absoluteRoot.generic_string().c_str());
         return false;
      }

      std::unordered_set<std::string> checkedCandidates;
      auto tryDirectory = [&](const fs::path& relativeDir) -> bool
      {
         const fs::path candidatePath = relativeDir.empty() ? fs::path(candidate) : (relativeDir / fs::path(candidate));
         if (candidatePath.empty() || candidatePath == ".")
            return false;

         const std::string normalizedCandidate = normalizeResourceSlashes(candidatePath.generic_string());
         if (!checkedCandidates.insert(normalizedCandidate).second)
            return false;

         return tryOpenTextureCandidate(resourceManager, normalizedCandidate, outStream, mountIdx, outResolvedPath);
      };

      if (tryDirectory(searchRoot))
         return true;

      for (const fs::directory_entry& itr : fs::directory_iterator(absoluteRoot))
      {
         if (!itr.is_directory())
            continue;

         const fs::path relativeDir = itr.path().lexically_relative(mountPath);
         if (tryDirectory(relativeDir))
            return true;
      }

      return false;
   }

   static bool tryOpenTextureFromVolumeMount(ResManager* resourceManager, const std::string& candidate, const fs::path& searchRoot, int32_t mountIdx, MemRStream& outStream, std::string* outResolvedPath = NULL)
   {
      fprintf(stderr, "texture search: volume mount=%d root='%s' candidate='%s'\n",
              mountIdx, searchRoot.generic_string().c_str(), candidate.c_str());
      std::vector<ResManager::EnumEntry> fileList;
      resourceManager->enumerateFiles(fileList, mountIdx, NULL);

      const fs::path normalizedRoot = searchRoot.lexically_normal();
      const fs::path candidatePath = fs::path(candidate).lexically_normal();
      for (const ResManager::EnumEntry& entry : fileList)
      {
         fs::path entryPath = fs::path(normalizeResourceSlashes(entry.filename)).lexically_normal();
         if (!pathEndsWith(entryPath, candidatePath))
            continue;

         fs::path baseDir = entryPath;
         for (auto ignoredPart = candidatePath.begin(); ignoredPart != candidatePath.end(); ++ignoredPart)
            baseDir = baseDir.parent_path();

         if (!normalizedRoot.empty())
         {
            if (baseDir != normalizedRoot && baseDir.parent_path() != normalizedRoot)
               continue;
         }

         fprintf(stderr, "texture search: volume matched entry='%s' mount=%d\n",
                 entryPath.generic_string().c_str(), mountIdx);
         return tryOpenTextureCandidate(resourceManager, entryPath.generic_string(), outStream, mountIdx, outResolvedPath);
      }

      return false;
   }

   static bool tryOpenMountedTexturePath(ResManager* resourceManager, const std::string& candidate, const std::string& resourceFilename, int32_t preferredMount, MemRStream& outStream, std::string* outResolvedPath = NULL)
   {
      const fs::path searchRoot;
      const int32_t pathMountCount = (int32_t)resourceManager->mPaths.size();
      const int32_t mountCount = pathMountCount + (int32_t)resourceManager->mVolumes.size();
      fprintf(stderr, "texture search: mounted search candidate='%s' resource='%s' preferredMount=%d root='%s'\n",
              candidate.c_str(), resourceFilename.c_str(), preferredMount, searchRoot.generic_string().c_str());

      auto tryMount = [&](int32_t mountIdx) -> bool
      {
         if (mountIdx < 0 || mountIdx >= mountCount)
            return false;

         if (mountIdx < pathMountCount)
            return tryOpenTextureFromFilesystemMount(resourceManager, candidate, searchRoot, mountIdx, outStream, outResolvedPath);
         return tryOpenTextureFromVolumeMount(resourceManager, candidate, searchRoot, mountIdx, outStream, outResolvedPath);
      };

      if (tryMount(preferredMount))
         return true;

      for (int32_t mountIdx = 0; mountIdx < mountCount; mountIdx++)
      {
         if (mountIdx == preferredMount)
            continue;
         if (tryMount(mountIdx))
            return true;
      }

      return false;
   }

   static bool openTextureStreamWithFallback(ResManager* resourceManager, const std::string& resourceFilename, int32_t resourceMount, const char* filename, MemRStream& outStream, std::string* outResolvedPath = NULL)
   {
      const std::string normalizedFilename = normalizeResourceSlashes(filename ? filename : "");
      fs::path texturePath(normalizedFilename);
      fs::path textureStem = texturePath;

      std::vector<std::string> extensionsToTry;
      std::string originalExt = texturePath.extension().generic_string();
      std::transform(originalExt.begin(), originalExt.end(), originalExt.begin(), ::tolower);

      bool hasKnownTextureExtension = hasRecognizedTextureExtension(texturePath);

      if (hasKnownTextureExtension)
      {
         extensionsToTry.push_back(originalExt);
         textureStem = texturePath.stem();
      }
      else
      {
         for (const char* knownExt : getKnownTextureExtensions())
            extensionsToTry.push_back(knownExt);
      }

      fprintf(stderr, "texture search: request='%s' normalized='%s' resource='%s' mount=%d knownExt=%s\n",
              filename ? filename : "", normalizedFilename.c_str(), resourceFilename.c_str(), resourceMount,
              hasKnownTextureExtension ? "yes" : "no");
      fprintf(stderr, "texture search: stem='%s' extensions=", textureStem.generic_string().c_str());
      for (size_t i = 0; i < extensionsToTry.size(); ++i)
      {
         fprintf(stderr, "%s%s", i == 0 ? "" : ",", extensionsToTry[i].c_str());
      }
      fprintf(stderr, "\n");

      if (!normalizedFilename.empty() &&
          tryOpenTextureCandidate(resourceManager, normalizedFilename, outStream, resourceMount, outResolvedPath))
      {
         return true;
      }

      fs::path searchDir = fs::path(normalizeResourceSlashes(resourceFilename)).parent_path();
      for (int depth = 0;; ++depth)
      {
         for (const std::string& ext : extensionsToTry)
         {
            fs::path candidatePath = searchDir / textureStem;
            if (hasKnownTextureExtension)
               candidatePath.replace_extension(ext);
            else
               candidatePath += ext;
            const std::string candidate = normalizeResourceSlashes(candidatePath.generic_string());
            fprintf(stderr, "texture search: relative try depth=%d dir='%s' candidate='%s'\n",
                    depth, searchDir.generic_string().c_str(), candidate.c_str());
            if (tryOpenTextureCandidate(resourceManager, candidate, outStream, resourceMount, outResolvedPath))
               return true;

            if (tryOpenMountedTexturePath(resourceManager, candidate, resourceFilename, resourceMount, outStream, outResolvedPath))
               return true;
         }

         if (searchDir.empty())
            break;
         searchDir = searchDir.parent_path();
      }

      fprintf(stderr, "texture search: failed request='%s'\n", filename ? filename : "");
      return false;
   }

   
   struct LoadedTexture
   {
      int32_t texID;
      uint32_t bmpFlags;
      uint16_t width, height;
      
      LoadedTexture() : texID(-1), bmpFlags(0), width(0), height(0) {;}
      LoadedTexture(int32_t tid, uint32_t bf) : texID(tid), bmpFlags(bf), width(0), height(0) {;}
   };
   
   struct ActiveMaterial
   {
      LoadedTexture tex;
      uint32_t texGroupID;

      ActiveMaterial() : texGroupID(UINT32_MAX) {;}
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

   static bool hasRecognizedTextureExtension(const fs::path& path)
   {
      std::string ext = path.extension().generic_string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
      for (const char* knownExt : getKnownTextureExtensions())
      {
         if (ext == knownExt)
            return true;
      }
      return false;
   }

   static std::string trimWhitespace(const std::string& in)
   {
      const size_t start = in.find_first_not_of(" \t\r\n");
      if (start == std::string::npos)
         return std::string();
      const size_t end = in.find_last_not_of(" \t\r\n");
      return in.substr(start, end - start + 1);
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
      uint32_t mSkinVertOffset;
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

      RuntimeIflMaterialInfo() : mFrame(0), mStartFrame(0), mIflMaterial(0), mDuration(0.0f) {;}
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

   struct MaterialBindingInfo
   {
      uint32_t materialIndex;
      uint32_t textureGroupID;
      uint32_t materialFrame;
      ModelPipelineState pipelineState;

      MaterialBindingInfo() : materialIndex(0), textureGroupID(0), materialFrame(0), pipelineState(ModelPipeline_DefaultDiffuse) {;}
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
   std::vector<slm::mat4> mDefaultNodeTransforms; // Default/bind absolute node transforms
   
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
            
            // Three RGBA texels store one 4x3 matrix. Round up so the raw CPU backing
            // always covers the full texture byte size used by the upload path.
            uint32_t transformCapacity = std::max<uint32_t>(((pow2Size * pow2Size) + 2) / 3, 1);
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

   struct PackedModelData
   {
      std::vector<ModelVertex> verts;
      std::vector<ModelTexVertex> texVerts;
      std::vector<ModelSkinVertex> skinVerts;
      std::vector<uint16_t> indices;
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
      mDefaultNodeTransforms.clear();
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
   
   size_t getSharedTransformCount() const
   {
      // Layout:
      //   [base + 0]      = identity transform
      //   [base + 1 + ni] = evaluated absolute transform for shape node ni
      return 1 + mShape->mNodes.size();
   }

   bool initIflMaterials()
   {
      if (!mShape || !mResourceManager || mShape->mIflMaterialsInitialized)
         return true;

      const size_t baseMaterialCount = mShape->mMaterials.size();
      mShape->mIflFrameTimes.clear();
      mShape->mIflFrameTimes.resize(mShape->mMaterials.size(), 0.0f);

      const fs::path resourceDir = fs::path(normalizeResourceSlashes(mResourceFilename)).parent_path();
      for (Dts3::IflMaterial& ifl : mShape->mIflMaterials)
      {
         ifl.firstFrame = (int32_t)mShape->mMaterials.size();
         ifl.numFrames = 0;

         std::string iflName = mShape->getName(ifl.name);
         iflName = normalizeResourceSlashes(iflName);
         const std::string fullPath = normalizeResourceSlashes((resourceDir / fs::path(iflName)).generic_string());

         MemRStream stream(0, NULL);
         if (!mResourceManager->openFile(fullPath.c_str(), stream, mResourceMount))
         {
            ifl.firstFrame = ifl.slot;
            continue;
         }

         float totalTime = 0.0f;
         std::string contents((const char*)stream.mPtr, (size_t)stream.mSize);
         std::istringstream lines(contents);
         std::string line;
         while (std::getline(lines, line))
         {
            std::replace(line.begin(), line.end(), '\t', ' ');
            line = trimWhitespace(line);
            if (line.empty())
               continue;

            std::istringstream lineStream(line);
            std::string textureName;
            lineStream >> textureName;
            if (textureName.empty())
               continue;

            int durationFrames = 1;
            if (!(lineStream >> durationFrames) || durationFrames <= 0)
               durationFrames = 1;

            textureName = normalizeResourceSlashes(textureName);
            fs::path texturePath(textureName);
            if (hasRecognizedTextureExtension(texturePath))
               textureName = normalizeResourceSlashes(texturePath.replace_extension().generic_string());

            MaterialList::Material props = {};
            if (ifl.slot >= 0 && ifl.slot < (int32_t)mShape->mMaterials.size())
               props = mShape->mMaterials[(uint32_t)ifl.slot];
            props.tsProps.flags |= MaterialList::IflFrame;
            mShape->mMaterials.push_back(textureName.c_str(), &props);

            totalTime += ((float)durationFrames) / 30.0f;
            mShape->mIflFrameTimes.push_back(totalTime);
            ifl.numFrames++;
         }

         if (ifl.numFrames == 0)
            ifl.firstFrame = ifl.slot;
      }

      mShape->mIflMaterialsInitialized = true;
      fprintf(stderr, "ifl init: baseMaterials=%zu totalMaterials=%zu iflMaterials=%zu iflFrameTimes=%zu\n",
              baseMaterialCount, mShape->mMaterials.size(), mShape->mIflMaterials.size(), mShape->mIflFrameTimes.size());
      for (size_t i = 0; i < mShape->mIflMaterials.size(); ++i)
      {
         const Dts3::IflMaterial& ifl = mShape->mIflMaterials[i];
         fprintf(stderr, "ifl init: idx=%zu slot=%d firstFrame=%d numFrames=%d name='%s'\n",
                 i, ifl.slot, ifl.firstFrame, ifl.numFrames, mShape->getName(ifl.name) ? mShape->getName(ifl.name) : "<unnamed>");
      }
      return true;
   }

   void buildDefaultNodeTransforms()
   {
      const size_t nodeCount = mShape ? mShape->mNodes.size() : 0;
      mDefaultNodeTransforms.resize(nodeCount, slm::mat4(1.0f));
      std::vector<slm::mat4> localNodeTransforms(nodeCount, slm::mat4(1.0f));

      for (size_t nodeIdx = 0; nodeIdx < nodeCount; nodeIdx++)
      {
         slm::quat nodeRotation(0, 0, 0, 1);
         slm::vec3 nodeTranslation(0.0f);
         if (nodeIdx < mShape->mDefaultRotations.size())
            nodeRotation = mShape->mDefaultRotations[nodeIdx].toQuat();
         if (nodeIdx < mShape->mDefaultTranslations.size())
            nodeTranslation = mShape->mDefaultTranslations[nodeIdx];

         slm::mat4 localTransform;
         CompatQuatSetMatrix(nodeRotation, localTransform);
         localTransform[3] = slm::vec4(nodeTranslation, 1.0f);
         localNodeTransforms[nodeIdx] = localTransform;
      }

      for (size_t nodeIdx = 0; nodeIdx < nodeCount; nodeIdx++)
      {
         const int32_t parentIdx = mShape->mNodes[nodeIdx].parent;
         if (parentIdx >= 0 && parentIdx < mDefaultNodeTransforms.size())
            mDefaultNodeTransforms[nodeIdx] = mDefaultNodeTransforms[parentIdx] * localNodeTransforms[nodeIdx];
         else
            mDefaultNodeTransforms[nodeIdx] = localNodeTransforms[nodeIdx];
      }
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
      mDefaultNodeTransforms.resize(mShape->mNodes.size(), slm::mat4(1.0f));
      mActiveRotations.resize(mShape->mNodes.size());
      mActiveTranslations.resize(mShape->mNodes.size());
      mActiveUniformScales.resize(mShape->mNodes.size(), 1.0f);
      mActiveAlignedScales.resize(mShape->mNodes.size(), slm::vec3(1.0f));
      mActiveArbitraryScales.resize(mShape->mNodes.size());
      mNodeRotationThreads.resize(mShape->mNodes.size(), -1);
      mNodeTranslationThreads.resize(mShape->mNodes.size(), -1);
      mNodeScaleThreads.resize(mShape->mNodes.size(), -1);
      for (size_t i=0; i<mRuntimeSubShapeInfos.size(); i++)
      {
         mRuntimeSubShapeInfos[i].subShapeIndex = (uint32_t)i;
         mRuntimeSubShapeInfos[i].dirtyFlags = RuntimeSubShapeInfo::AllDirty;
      }
      for (size_t i=0; i<mRuntimeIflMaterialInfos.size(); i++)
      {
         RuntimeIflMaterialInfo& info = mRuntimeIflMaterialInfos[i];
         Dts3::IflMaterial& ifl = mShape->mIflMaterials[i];
         info.mIflMaterial = (uint32_t)i;
         info.mStartFrame = ifl.firstFrame;
         info.mFrame = 0;
         if (ifl.numFrames > 0 &&
             ifl.firstFrame >= 0 &&
             (size_t)(ifl.firstFrame + ifl.numFrames - 1) < mShape->mIflFrameTimes.size())
            info.mDuration = mShape->mIflFrameTimes[(size_t)(ifl.firstFrame + ifl.numFrames - 1)];
         else
            info.mDuration = 0.0f;
      }

      buildDefaultNodeTransforms();
      
      // Load base skin transforms texture
      nodeMeshTransformsTex.reset();
      
      const uint32_t sharedTransformCount = (uint32_t)getSharedTransformCount();
      mBaseTextureTransform = nodeMeshTransformsTex.allocTransforms(sharedTransformCount);

      for (size_t i=0; i<mRuntimeMeshInfos.size(); i++)
      {
         mRuntimeMeshInfos[i].mMesh = &mShape->mMeshes[i];
         mRuntimeMeshInfos[i].mMeshTransformOffset = mBaseTextureTransform;
      }
      
      for (Dts3::Object& obj : mShape->mObjects)
      {
         uint32_t objectTransformOffset = mBaseTextureTransform;
         if (obj.node >= 0 && obj.node < mShape->mNodes.size())
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
      
      if (nodeMeshTransformsTex.memoryUsed > 0)
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
      thread.transitioning = false;
      thread.transitionState = Dts3::ThreadTransitionState();

      if (thread.sequenceIdx >= 0 && thread.sequenceIdx < mShape->mSequences.size())
      {
         Dts3::Sequence& oldSeq = mShape->mSequences[thread.sequenceIdx];
         thread.transitionState.oldRotations = oldSeq.mattersRot;
         thread.transitionState.oldTranslations = oldSeq.mattersTranslation;
         thread.transitionState.oldScales = oldSeq.mattersScale;
         thread.transitionState.oldSequenceIdx = (uint32_t)thread.sequenceIdx;
         thread.transitionState.oldPos = thread.pos;
      }

      setThreadSequence(thread.index, sequenceIdx, pos);

      thread.transitioning = true;
      thread.transitionState.duration = std::max(duration, 0.0001f);
      thread.transitionState.pos = 0.0f;
      thread.transitionState.direction = 1.0f;
      thread.transitionState.targetScale = 1.0f;
      thread.playing = playing;
   }
   
   void updateTransitions()
   {
      mTransitionSets.rotationNodes.reset();
      mTransitionSets.translationNodes.reset();
      mTransitionSets.scaleNodes.reset();

      for (uint32_t idx : mTransitionThreads)
      {
         if (idx >= mThreads.size())
            continue;
         Dts3::Thread& thread = mThreads[idx];
         if (!thread.transitioning)
            continue;
         if (thread.sequenceIdx < 0 || thread.sequenceIdx >= mShape->mSequences.size())
            continue;

         Dts3::Sequence& seq = mShape->mSequences[thread.sequenceIdx];
         mTransitionSets.rotationNodes |= thread.transitionState.oldRotations;
         mTransitionSets.rotationNodes |= seq.mattersRot;
         mTransitionSets.translationNodes |= thread.transitionState.oldTranslations;
         mTransitionSets.translationNodes |= seq.mattersTranslation;
         mTransitionSets.scaleNodes |= thread.transitionState.oldScales;
         mTransitionSets.scaleNodes |= seq.mattersScale;
      }
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

      thread.transitioning = false;
      thread.transitionState = Dts3::ThreadTransitionState();
      mTransitionThreads.erase(std::remove(mTransitionThreads.begin(), mTransitionThreads.end(), (uint32_t)thread.index), mTransitionThreads.end());
      updateTransitions();
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
               if (numFrames <= 0 || firstFrame < 0 ||
                   (size_t)(firstFrame + numFrames - 1) >= mShape->mIflFrameTimes.size())
               {
                  info.mFrame = 0;
                  return false;
               }
               
               float time = (thread.pos * seq.duration) + seq.toolBegin;
               if (time > duration && duration > 0.0f)
               {
                  time -= duration * (float)((int32_t) (time / duration)); // loop
               }
               
               // Lookup frame t1 style
               int32_t frameIdx = 0;
               for (; frameIdx < numFrames-1 && time > mShape->mIflFrameTimes[firstFrame + frameIdx]; frameIdx++);
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
      for (Dts3::Thread& thread : mThreads)
      {
         if (thread.index >= 0 &&
             thread.index < mThreadActive.size() &&
             mThreadActive[thread.index] &&
             thread.playing)
         {
            advanceThreadTime(thread, dt);

            if (thread.transitioning)
            {
               thread.transitionState.pos += dt / std::max(thread.transitionState.duration, 0.0001f);
               if (thread.transitionState.pos >= 1.0f)
               {
                  thread.transitionState.pos = 1.0f;
                  clearTransition(thread);
               }
            }
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
      if (mBaseTextureTransform < nodeMeshTransformsTex.memorySize)
      {
         nodeMeshTransformsTex.updateMem[mBaseTextureTransform] = packMatrix43(slm::mat4(1.0f));
      }

      for (size_t i=0; i<mNodeTransforms.size(); i++)
      {
         const size_t transformIndex = mBaseTextureTransform + 1 + i;
         if (transformIndex >= nodeMeshTransformsTex.memorySize)
            break;
         nodeMeshTransformsTex.updateMem[transformIndex] = packMatrix43(mNodeTransforms[i]);
      }

      for (RuntimeMeshInfo& rm : mRuntimeMeshInfos)
      {
         Dts3::SkinData* sd = rm.mMesh->getSkinData();
         if (!sd)
            continue;

         for (size_t i=0; i<sd->nodeTransforms.size(); i++)
         {
            const size_t transformIndex = rm.mMeshTransformOffset + i;
            if (transformIndex >= nodeMeshTransformsTex.memorySize)
               break;

            slm::mat4 finalTransform = sd->nodeTransforms[i];
            if (i < sd->nodeIndex.size() && sd->nodeIndex[i] < mNodeTransforms.size())
            {
               finalTransform = mNodeTransforms[sd->nodeIndex[i]] * sd->nodeTransforms[i];
            }
            nodeMeshTransformsTex.updateMem[transformIndex] = packMatrix43(finalTransform);
         }
      }
      
      GFXUpdateCustomTextureAligned(nodeMeshTransformsTex.texID, nodeMeshTransformsTex.updateMem);
      GFXSetModelTransformTexture(nodeMeshTransformsTex.texID);
   }

   int32_t getNodeMatterIndex(const IntegerSet& matterSet, int32_t nodeIdx) const
   {
      int32_t matterIndex = 0;
      for (int32_t it = (int32_t)matterSet.findFirst(); it >= 0; it = (int32_t)matterSet.findNext(it))
      {
         if (it == nodeIdx)
            return matterIndex;
         matterIndex++;
      }
      return -1;
   }

   bool sampleSequenceRotationForNode(Dts3::Sequence& sequence, const Dts3::Thread::KeyFrameInfo& keyInfo, int32_t nodeIdx, slm::quat& outRotation)
   {
      int32_t rotIndex = getNodeMatterIndex(sequence.mattersRot, nodeIdx);
      if (rotIndex < 0)
         return false;
      slm::quat rotA = mShape->getSequenceRotation(sequence, keyInfo.keyA, rotIndex).toQuat();
      slm::quat rotB = mShape->getSequenceRotation(sequence, keyInfo.keyB, rotIndex).toQuat();
      outRotation = CompatInterpolate(rotA, rotB, keyInfo.keyPos);
      return true;
   }

   bool sampleSequenceTranslationForNode(Dts3::Sequence& sequence, const Dts3::Thread::KeyFrameInfo& keyInfo, int32_t nodeIdx, slm::vec3& outTranslation)
   {
      int32_t transIndex = getNodeMatterIndex(sequence.mattersTranslation, nodeIdx);
      if (transIndex < 0)
         return false;
      slm::vec3 transA = mShape->getSequenceTranslation(sequence, keyInfo.keyA, transIndex);
      slm::vec3 transB = mShape->getSequenceTranslation(sequence, keyInfo.keyB, transIndex);
      outTranslation = slm::mix(transA, transB, keyInfo.keyPos);
      return true;
   }

   bool sampleSequenceScaleForNode(Dts3::Sequence& sequence, const Dts3::Thread::KeyFrameInfo& keyInfo, int32_t nodeIdx, slm::vec3& outScale)
   {
      int32_t scaleIndex = getNodeMatterIndex(sequence.mattersScale, nodeIdx);
      if (scaleIndex < 0)
         return false;

      if (sequence.testFlags(Dts3::Sequence::ArbitraryScale))
      {
         Dts3::ArbitraryScale scaleA = mShape->getSequenceArbitraryScale(sequence, keyInfo.keyA, scaleIndex);
         Dts3::ArbitraryScale scaleB = mShape->getSequenceArbitraryScale(sequence, keyInfo.keyB, scaleIndex);
         outScale = slm::mix(scaleA.pos, scaleB.pos, keyInfo.keyPos);
      }
      else if (sequence.testFlags(Dts3::Sequence::AlignedScale))
      {
         slm::vec3 scaleA = mShape->getSequenceAlignedScale(sequence, keyInfo.keyA, scaleIndex);
         slm::vec3 scaleB = mShape->getSequenceAlignedScale(sequence, keyInfo.keyB, scaleIndex);
         outScale = slm::mix(scaleA, scaleB, keyInfo.keyPos);
      }
      else if (sequence.testFlags(Dts3::Sequence::UniformScale))
      {
         float scaleA = mShape->getSequenceUniformScale(sequence, keyInfo.keyA, scaleIndex);
         float scaleB = mShape->getSequenceUniformScale(sequence, keyInfo.keyB, scaleIndex);
         float uniformScale = slm::mix(scaleA, scaleB, keyInfo.keyPos);
         outScale = slm::vec3(uniformScale);
      }
      else
      {
         return false;
      }

      return true;
   }

   void initializeActiveNodeTransforms(const Dts3::SubShape& subShape)
   {
      const size_t nodeCount = mShape->mNodes.size();
      for (size_t nodeIdx = 0; nodeIdx < nodeCount; nodeIdx++)
      {
         mActiveRotations[nodeIdx] = mShape->getDefaultNodeRotation((int32_t)nodeIdx);
         mActiveTranslations[nodeIdx] = slm::vec4(mShape->getDefaultNodeTranslation((int32_t)nodeIdx), 1.0f);
         mActiveAlignedScales[nodeIdx] = mShape->getDefaultNodeScale((int32_t)nodeIdx);
         mActiveUniformScales[nodeIdx] = 1.0f;
         mActiveArbitraryScales[nodeIdx].rot = Quat16();
         mActiveArbitraryScales[nodeIdx].pos = slm::vec3(1.0f);
      }

      const int32_t firstNode = std::max(subShape.firstNode, 0);
      const int32_t endNode = std::min<int32_t>(subShape.firstNode + subShape.numNodes, (int32_t)nodeCount);
      for (int32_t nodeIdx = firstNode; nodeIdx < endNode; nodeIdx++)
      {
         mNodeRotationThreads[nodeIdx] = -1;
         mNodeTranslationThreads[nodeIdx] = -1;
         mNodeScaleThreads[nodeIdx] = -1;
      }
   }

   void applyBaseSequence(Dts3::Thread& thread, const Dts3::SubShape& subShape, std::vector<bool>& rotationSet, std::vector<bool>& translationSet, std::vector<bool>& scaleSet)
   {
      Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
      const int32_t firstNode = std::max(subShape.firstNode, 0);
      const int32_t endNode = std::min<int32_t>(subShape.firstNode + subShape.numNodes, (int32_t)mShape->mNodes.size());

      int32_t rotFrame = 0;
      for (int32_t nodeIdx = (int32_t)sequence.mattersRot.findFirst();
           nodeIdx >= 0;
           nodeIdx = (int32_t)sequence.mattersRot.findNext(nodeIdx))
      {
         if (nodeIdx < firstNode || nodeIdx >= endNode)
         {
            rotFrame++;
            continue;
         }

         if (!rotationSet[nodeIdx])
         {
            slm::quat rotA = mShape->getSequenceRotation(sequence, thread.keyInfo.keyA, rotFrame).toQuat();
            slm::quat rotB = mShape->getSequenceRotation(sequence, thread.keyInfo.keyB, rotFrame).toQuat();
            mActiveRotations[nodeIdx] = CompatInterpolate(rotA, rotB, thread.keyInfo.keyPos);
            rotationSet[nodeIdx] = true;
            mNodeRotationThreads[nodeIdx] = thread.index;
         }
         rotFrame++;
      }

      int32_t transFrame = 0;
      for (int32_t nodeIdx = (int32_t)sequence.mattersTranslation.findFirst();
           nodeIdx >= 0;
           nodeIdx = (int32_t)sequence.mattersTranslation.findNext(nodeIdx))
      {
         if (nodeIdx < firstNode || nodeIdx >= endNode)
         {
            transFrame++;
            continue;
         }

         if (!translationSet[nodeIdx])
         {
            slm::vec3 transA = mShape->getSequenceTranslation(sequence, thread.keyInfo.keyA, transFrame);
            slm::vec3 transB = mShape->getSequenceTranslation(sequence, thread.keyInfo.keyB, transFrame);
            mActiveTranslations[nodeIdx] = slm::vec4(slm::mix(transA, transB, thread.keyInfo.keyPos), 1.0f);
            translationSet[nodeIdx] = true;
            mNodeTranslationThreads[nodeIdx] = thread.index;
         }
         transFrame++;
      }

      int32_t scaleFrame = 0;
      for (int32_t nodeIdx = (int32_t)sequence.mattersScale.findFirst();
           nodeIdx >= 0;
           nodeIdx = (int32_t)sequence.mattersScale.findNext(nodeIdx))
      {
         if (nodeIdx < firstNode || nodeIdx >= endNode)
         {
            scaleFrame++;
            continue;
         }

         if (!scaleSet[nodeIdx])
         {
            slm::vec3 scaleValue(1.0f);
            if (sequence.testFlags(Dts3::Sequence::ArbitraryScale))
            {
               Dts3::ArbitraryScale scaleA = mShape->getSequenceArbitraryScale(sequence, thread.keyInfo.keyA, scaleFrame);
               Dts3::ArbitraryScale scaleB = mShape->getSequenceArbitraryScale(sequence, thread.keyInfo.keyB, scaleFrame);
               mActiveArbitraryScales[nodeIdx].pos = slm::mix(scaleA.pos, scaleB.pos, thread.keyInfo.keyPos);
               mActiveArbitraryScales[nodeIdx].rot = scaleA.rot;
               scaleValue = mActiveArbitraryScales[nodeIdx].pos;
            }
            else if (sequence.testFlags(Dts3::Sequence::AlignedScale))
            {
               slm::vec3 scaleA = mShape->getSequenceAlignedScale(sequence, thread.keyInfo.keyA, scaleFrame);
               slm::vec3 scaleB = mShape->getSequenceAlignedScale(sequence, thread.keyInfo.keyB, scaleFrame);
               scaleValue = slm::mix(scaleA, scaleB, thread.keyInfo.keyPos);
            }
            else if (sequence.testFlags(Dts3::Sequence::UniformScale))
            {
               float scaleA = mShape->getSequenceUniformScale(sequence, thread.keyInfo.keyA, scaleFrame);
               float scaleB = mShape->getSequenceUniformScale(sequence, thread.keyInfo.keyB, scaleFrame);
               float uniformScale = slm::mix(scaleA, scaleB, thread.keyInfo.keyPos);
               mActiveUniformScales[nodeIdx] = uniformScale;
               scaleValue = slm::vec3(uniformScale);
            }

            mActiveAlignedScales[nodeIdx] = scaleValue;
            scaleSet[nodeIdx] = true;
            mNodeScaleThreads[nodeIdx] = thread.index;
         }
         scaleFrame++;
      }
   }

   void buildAnimatedNodeTransforms(const Dts3::SubShape& subShape)
   {
      const int32_t firstNode = std::max(subShape.firstNode, 0);
      const int32_t endNode = std::min<int32_t>(subShape.firstNode + subShape.numNodes, (int32_t)mShape->mNodes.size());
      for (int32_t nodeIdx = firstNode; nodeIdx < endNode; nodeIdx++)
      {
         slm::mat4 localTransform;
         CompatQuatSetMatrix(mActiveRotations[nodeIdx], localTransform);
         localTransform[3] = mActiveTranslations[nodeIdx];

         if (mApplyScaleTransforms)
            localTransform = localTransform * slm::scaling(mActiveAlignedScales[nodeIdx]);

         mLocalNodeTransforms[nodeIdx] = localTransform;

         int32_t parentIdx = mShape->mNodes[nodeIdx].parent;
         if (parentIdx >= 0 && parentIdx < mNodeTransforms.size())
            mNodeTransforms[nodeIdx] = mNodeTransforms[parentIdx] * localTransform;
         else
            mNodeTransforms[nodeIdx] = localTransform;
      }
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
      if (mDefaultNodeTransforms.size() != nodeCount)
         buildDefaultNodeTransforms();

      initializeActiveNodeTransforms(subShape);

      std::vector<bool> rotationSet(nodeCount, false);
      std::vector<bool> translationSet(nodeCount, false);
      std::vector<bool> scaleSet(nodeCount, false);

      for (uint32_t num : mSortedThreads)
      {
         if (num >= mThreads.size())
            continue;
         Dts3::Thread& thread = mThreads[num];
         if (thread.index < 0)
            continue;
         if (thread.sequenceIdx < 0 || thread.sequenceIdx >= mShape->mSequences.size())
            continue;

         Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
         if (sequence.testFlags(Dts3::Sequence::Blend))
            break;

         applyBaseSequence(thread, subShape, rotationSet, translationSet, scaleSet);
      }

      forEachSortedThread([&](Dts3::Thread& thread){
         Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
         if (sequence.testFlags(Dts3::Sequence::Blend) && !thread.noBlend)
            applyBlendSequence(thread, subShape);
      });

      applyTransitionNodes(subShape);
      buildAnimatedNodeTransforms(subShape);
      updateTransformTexture();
   }
   
   void applyBlendSequence(Dts3::Thread& thread, const Dts3::SubShape& subShape)
   {
      if (thread.sequenceIdx < 0 || thread.sequenceIdx >= mShape->mSequences.size())
         return;

      Dts3::Sequence& sequence = mShape->mSequences[thread.sequenceIdx];
      const int32_t firstNode = std::max(subShape.firstNode, 0);
      const int32_t endNode = std::min<int32_t>(subShape.firstNode + subShape.numNodes, (int32_t)mShape->mNodes.size());

      for (int32_t nodeIdx = firstNode; nodeIdx < endNode; nodeIdx++)
      {
         slm::quat blendRot;
         if (sampleSequenceRotationForNode(sequence, thread.keyInfo, nodeIdx, blendRot))
            mActiveRotations[nodeIdx] = slm::normalize(mActiveRotations[nodeIdx] * blendRot);

         slm::vec3 blendTrans;
         if (sampleSequenceTranslationForNode(sequence, thread.keyInfo, nodeIdx, blendTrans))
            mActiveTranslations[nodeIdx] += slm::vec4(blendTrans, 0.0f);

         slm::vec3 blendScale;
         if (sampleSequenceScaleForNode(sequence, thread.keyInfo, nodeIdx, blendScale))
            mActiveAlignedScales[nodeIdx] *= blendScale;
      }
   }
   
   void applyTransitionNodes(const Dts3::SubShape& subShape)
   {
      const int32_t firstNode = std::max(subShape.firstNode, 0);
      const int32_t endNode = std::min<int32_t>(subShape.firstNode + subShape.numNodes, (int32_t)mShape->mNodes.size());

      for (uint32_t threadIdx : mTransitionThreads)
      {
         if (threadIdx >= mThreads.size())
            continue;
         Dts3::Thread& thread = mThreads[threadIdx];
         if (!thread.transitioning)
            continue;
         if (thread.sequenceIdx < 0 || thread.sequenceIdx >= mShape->mSequences.size())
            continue;
         if (thread.transitionState.oldSequenceIdx >= mShape->mSequences.size())
            continue;

         Dts3::Sequence& oldSequence = mShape->mSequences[thread.transitionState.oldSequenceIdx];
         Dts3::Sequence& newSequence = mShape->mSequences[thread.sequenceIdx];
         Dts3::Thread::KeyFrameInfo oldKeyInfo;
         selectKeyFrames(thread.transitionState.oldPos, oldSequence, oldKeyInfo);
         float t = std::clamp(thread.transitionState.pos * thread.transitionState.targetScale, 0.0f, 1.0f);

         for (int32_t nodeIdx = firstNode; nodeIdx < endNode; nodeIdx++)
         {
            const bool affectsRot = thread.transitionState.oldRotations.test(nodeIdx) || newSequence.mattersRot.test(nodeIdx);
            if (affectsRot)
            {
               slm::quat oldRot = mShape->getDefaultNodeRotation(nodeIdx);
               slm::quat newRot = mShape->getDefaultNodeRotation(nodeIdx);
               sampleSequenceRotationForNode(oldSequence, oldKeyInfo, nodeIdx, oldRot);
               sampleSequenceRotationForNode(newSequence, thread.keyInfo, nodeIdx, newRot);
               mActiveRotations[nodeIdx] = CompatInterpolate(oldRot, newRot, t);
            }

            const bool affectsTrans = thread.transitionState.oldTranslations.test(nodeIdx) || newSequence.mattersTranslation.test(nodeIdx);
            if (affectsTrans)
            {
               slm::vec3 oldTrans = mShape->getDefaultNodeTranslation(nodeIdx);
               slm::vec3 newTrans = mShape->getDefaultNodeTranslation(nodeIdx);
               sampleSequenceTranslationForNode(oldSequence, oldKeyInfo, nodeIdx, oldTrans);
               sampleSequenceTranslationForNode(newSequence, thread.keyInfo, nodeIdx, newTrans);
               mActiveTranslations[nodeIdx] = slm::vec4(slm::mix(oldTrans, newTrans, t), 1.0f);
            }

            const bool affectsScale = thread.transitionState.oldScales.test(nodeIdx) || newSequence.mattersScale.test(nodeIdx);
            if (affectsScale)
            {
               slm::vec3 oldScale = mShape->getDefaultNodeScale(nodeIdx);
               slm::vec3 newScale = mShape->getDefaultNodeScale(nodeIdx);
               sampleSequenceScaleForNode(oldSequence, oldKeyInfo, nodeIdx, oldScale);
               sampleSequenceScaleForNode(newSequence, thread.keyInfo, nodeIdx, newScale);
               mActiveAlignedScales[nodeIdx] = slm::mix(oldScale, newScale, t);
            }
         }
      }
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
      for (uint32_t i=subShape.firstObject; i<subShape.firstObject + subShape.numObjects; i++)
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
      for (uint32_t i=subShape.firstObject; i<subShape.firstObject + subShape.numObjects; i++)
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
      for (uint32_t i=subShape.firstObject; i<subShape.firstObject + subShape.numObjects; i++)
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
      initIflMaterials();
      
      initShapeObjects();
      
      initMaterials();
      
      initRender();
      initVertexBuffer();
      updateScaleAnimatonState();
      if (!mShape->mSequences.empty())
      {
         uint32_t threadIdx = addThread();
         setThreadSequence(threadIdx, 0, 0.0f);
      }
      
      // Setup default pose for nodes
      if (!mShape->mDetailLevels.empty())
      {
         mCurrentDetail = selectNearestRenderableDetail(0);
         if (mCurrentDetail >= 0)
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
            mShape->mObjects[objectIdx].nextSibling = i;
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
   
   void buildPackedModelData(PackedModelData& packed)
   {
      /*
       NOTE: We put skin data first, then follow it with basic data. This is so
       we can bind the skin data without dealing with alignment issues.
       */
      
      packed.verts.clear();
      packed.texVerts.clear();
      packed.skinVerts.clear();
      packed.indices.clear();

      std::vector<RuntimeMeshInfo*> skinMeshList;
      std::vector<RuntimeMeshInfo*> basicMeshList;
      
      // Load meshes
      uint32_t vertCount = 0;
      uint32_t indexCount = 0;
      uint32_t skinVertCount = 0;
      uint32_t maxVertsInMesh = 0;
      
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
            maxVertsInMesh = std::max(maxVertsInMesh, rm.mRealVertsPerFrame);
         }
         else
         {
            rm.mRealVertsPerFrame = 0;
            rm.mIndexCount = 0;
         }

         rm.mSkinVertOffset = 0;
      }
      
      packed.verts.resize(vertCount + skinVertCount);
      packed.texVerts.resize(vertCount + skinVertCount);
      if (maxVertsInMesh != 0 || skinMeshList.size() != 0)
         packed.skinVerts.resize(maxVertsInMesh + skinVertCount);
      packed.indices.resize(indexCount);

      for (uint32_t i=0; i<maxVertsInMesh; i++)
      {
         packed.skinVerts[i].index[0] = 0;
         packed.skinVerts[i].weights[0] = 1.0f;
      }
      
      uint32_t skinVertBase = maxVertsInMesh;
      skinVertCount = 0;
      indexCount = 0;
      for (RuntimeMeshInfo* rm : skinMeshList)
      {
         Dts3::SkinData* sd = rm->mMesh->getSkinData();
         
         // Copy verts
         Dts3::EmitModelVertices(sd, &packed.verts[skinVertCount]);
         Dts3::EmitModelTexVertices(sd, &packed.texVerts[skinVertCount]);
         Dts3::EmitPackedSkinVertices(sd, &packed.skinVerts[skinVertBase + skinVertCount]);
         memcpy(&packed.indices[indexCount], &sd->indices[0], sizeof(uint16_t) * sd->indices.size());
         
         // Count & offsets
         rm->mVertOffset = skinVertCount;
         rm->mIndexOffset = indexCount;
         rm->mSkinVertOffset = skinVertBase + skinVertCount;
         skinVertCount += rm->mVertCount;
         indexCount += rm->mIndexCount;
      }
      
      vertCount = skinVertCount;
      for (RuntimeMeshInfo* rm : basicMeshList)
      {
         Dts3::BasicData* bd = rm->mMesh->getBasicData();
         
         // Copy verts
         Dts3::EmitModelVertices(bd, &packed.verts[vertCount]);
         Dts3::EmitModelTexVertices(bd, &packed.texVerts[vertCount]);
         memcpy(&packed.indices[indexCount], bd->indices.data(), sizeof(uint16_t) * bd->indices.size());
         
         // Count & offsets
         rm->mVertOffset = vertCount;
         rm->mIndexOffset = indexCount;
         rm->mSkinVertOffset = 0;
         vertCount += rm->mVertCount;
         indexCount += rm->mIndexCount;
      }
   }

   void initVertexBuffer()
   {
      clearVertexBuffer();
      
      PackedModelData packed;
      buildPackedModelData(packed);
      
      GFXLoadModelData(0,
                       packed.verts.empty() ? NULL : &packed.verts[0],
                       packed.texVerts.empty() ? NULL : &packed.texVerts[0],
                       packed.indices.empty() ? NULL : &packed.indices[0],
                       packed.skinVerts.empty() ? NULL : &packed.skinVerts[0],
                       (uint32_t)packed.verts.size(),
                       (uint32_t)packed.texVerts.size(),
                       (uint32_t)packed.indices.size(),
                       (uint32_t)packed.skinVerts.size());
      initVB = true;
   }

   bool dumpPackedModelOBJ(const char *filename)
   {
      if (mShape == NULL || filename == NULL)
         return false;

      PackedModelData packed;
      buildPackedModelData(packed);

      std::ofstream out(filename);
      if (!out.is_open())
      {
         fprintf(stderr, "failed to open packed OBJ dump '%s'\n", filename);
         return false;
      }

      uint32_t vertexBase = 1;
      uint32_t texcoordBase = 1;
      for (uint32_t meshIdx = 0; meshIdx < mRuntimeMeshInfos.size(); meshIdx++)
      {
         RuntimeMeshInfo& rm = mRuntimeMeshInfos[meshIdx];
         Dts3::BasicData* basicData = rm.mMesh ? rm.mMesh->getBasicData() : NULL;
         if (basicData == NULL || rm.mRealVertsPerFrame == 0)
            continue;

         const uint32_t frameCount = std::max<uint32_t>(rm.mMesh->mNumFrames, 1);
         const uint32_t texFrameCount = std::max<uint32_t>(rm.mMesh->mNumMatFrames, 1);
         const uint32_t meshTexBase = rm.mVertOffset;

         for (uint32_t frameIdx = 0; frameIdx < frameCount; frameIdx++)
         {
            const uint32_t vertStart = rm.mVertOffset + (frameIdx * rm.mRealVertsPerFrame);
            const uint32_t texFrameIdx = std::min<uint32_t>(frameIdx, texFrameCount - 1);
            const uint32_t texStart = meshTexBase + (texFrameIdx * rm.mRealVertsPerFrame);

            if (vertStart + rm.mRealVertsPerFrame > packed.verts.size())
               break;
            if (texStart + rm.mRealVertsPerFrame > packed.texVerts.size())
               break;

            std::string meshName = "packed_mesh_" + std::to_string(meshIdx) + "_frame_" + std::to_string(frameIdx);
            out << "g " << meshName << "\n";
            out << "o " << meshName << "\n";

            for (uint32_t i = 0; i < rm.mRealVertsPerFrame; i++)
            {
               const slm::vec3& v = packed.verts[vertStart + i].position;
               out << "v " << v.x << " " << v.y << " " << v.z << "\n";
            }

            for (uint32_t i = 0; i < rm.mRealVertsPerFrame; i++)
            {
               const slm::vec2& vt = packed.texVerts[texStart + i].texcoord;
               out << "vt " << vt.x << " " << (1.0f - vt.y) << "\n";
            }

            for (const Dts3::Primitive& prim : basicData->primitives)
            {
               const uint32_t drawMode = prim.matIndex & Dts3::Primitive::TypeMask;
               if (drawMode != Dts3::Primitive::Triangles)
                  continue;

               for (uint32_t i = 0; i + 2 < prim.numElements; i += 3)
               {
                  const uint32_t i0 = packed.indices[rm.mIndexOffset + prim.firstElement + i + 0];
                  const uint32_t i1 = packed.indices[rm.mIndexOffset + prim.firstElement + i + 1];
                  const uint32_t i2 = packed.indices[rm.mIndexOffset + prim.firstElement + i + 2];

                  if (i0 >= rm.mRealVertsPerFrame || i1 >= rm.mRealVertsPerFrame || i2 >= rm.mRealVertsPerFrame)
                     continue;

                  const uint32_t v0 = vertexBase + i0;
                  const uint32_t v1 = vertexBase + i1;
                  const uint32_t v2 = vertexBase + i2;
                  const uint32_t t0 = texcoordBase + i0;
                  const uint32_t t1 = texcoordBase + i1;
                  const uint32_t t2 = texcoordBase + i2;
                  out << "f "
                      << v0 << "/" << t0 << " "
                      << v1 << "/" << t1 << " "
                      << v2 << "/" << t2 << "\n";
               }
            }

            out << "\n";
            vertexBase += rm.mRealVertsPerFrame;
            texcoordBase += rm.mRealVertsPerFrame;
         }
      }

      return true;
   }

   bool validateMeshDrawIndices(const RuntimeMeshInfo& mi, const Dts3::BasicData* bd, const char* context) const
   {
      if (bd == NULL || mi.mRealVertsPerFrame == 0)
         return true;

      for (const Dts3::Primitive& prim : bd->primitives)
      {
         const uint32_t drawMode = prim.matIndex & Dts3::Primitive::TypeMask;
         if (drawMode != Dts3::Primitive::Triangles)
            continue;

         if (prim.firstElement + prim.numElements > bd->indices.size())
         {
            fprintf(stderr, "%s: primitive index range overruns mesh index buffer (first=%u count=%u size=%zu)\n",
                    context, prim.firstElement, prim.numElements, bd->indices.size());
            return false;
         }

         for (uint32_t i = 0; i < prim.numElements; i++)
         {
            const uint32_t idx = bd->indices[prim.firstElement + i];
            if (idx >= mi.mRealVertsPerFrame)
            {
               fprintf(stderr, "%s: vertex index %u out of range for vertsPerFrame=%u (prim first=%u count=%u)\n",
                       context, idx, mi.mRealVertsPerFrame, prim.firstElement, prim.numElements);
               return false;
            }
         }
      }

      return true;
   }
   
   void clearVertexBuffer()
   {
      if (!initVB)
         return;
      
      GFXLoadModelData(0, NULL, NULL, NULL, NULL, 0, 0, 0, 0);
      initVB = false;
   }
   
   // Rendering
   
   void determineNodeVisibility()
   {
   }

   bool isRenderableDetail(const Dts3::DetailLevel& detail) const
   {
      return detail.subshape >= 0 &&
             detail.subshape < mShape->mSubshapes.size() &&
             detail.objectDetail >= 0;
   }

   int32_t selectNearestRenderableDetail(int32_t requestedDetail) const
   {
      if (!mShape || mShape->mDetailLevels.empty())
         return -1;

      requestedDetail = std::clamp<int32_t>(requestedDetail, 0, (int32_t)mShape->mDetailLevels.size() - 1);
      if (isRenderableDetail(mShape->mDetailLevels[requestedDetail]))
         return requestedDetail;

      for (int32_t radius = 1; radius < mShape->mDetailLevels.size(); radius++)
      {
         const int32_t lower = requestedDetail - radius;
         if (lower >= 0 && isRenderableDetail(mShape->mDetailLevels[lower]))
            return lower;

         const int32_t upper = requestedDetail + radius;
         if (upper < mShape->mDetailLevels.size() && isRenderableDetail(mShape->mDetailLevels[upper]))
            return upper;
      }

      return -1;
   }
   
   void selectDetail(float dist, int w, int h)
   {
      if (!mShape)
      {
         mCurrentDetail = -1;
         return;
      }

      const int32_t requestedDetail = (int32_t)std::lround(dist);
      mCurrentDetail = selectNearestRenderableDetail(requestedDetail);
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
      if (obj.numMeshes <= 0)
         return;
      meshNum = std::min<uint32_t>(meshNum, (uint32_t)obj.numMeshes - 1);
      
      RuntimeObjectInfo& ri = mRuntimeObjectInfos[objectIndex];
      if (ri.mLastVis <= 0.0f || !ri.mDraw)
         return;
      RuntimeMeshInfo& mi = mRuntimeMeshInfos[obj.firstMesh + meshNum];

      if (mi.mMesh != NULL)
      {
         const uint32_t numMeshFrames = std::max<uint32_t>(mi.mMesh->mNumFrames, 1);
         mi.mMeshFrame = (uint32_t)std::clamp(ri.mLastMeshframe, 0, (int32_t)numMeshFrames - 1);
      }
      else
      {
         mi.mMeshFrame = 0;
      }
      mi.mMeshTexFrame = 0;
      
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
         if (sort->numVerts.empty() || sort->firstVerts.empty() || sort->firstTVerts.empty())
            return;
         
         uint32_t nc = std::min<uint32_t>(mi.mMeshFrame, (uint32_t)sort->numVerts.size() - 1);
         uint32_t mf = std::min<uint32_t>(mi.mMeshFrame, (uint32_t)sort->firstVerts.size() - 1);
         uint32_t tfFrame = mi.mMeshTexFrame ? mi.mMeshTexFrame : mi.mMeshFrame;
         uint32_t tf = std::min<uint32_t>(tfFrame, (uint32_t)sort->firstTVerts.size() - 1);
         
         // NOTE: ideally we should render in cluster order here, but
         // to keep things simple we'll just let the GPU do all the work.
         renderMesh(mi, ri, bd,
                    sort->numVerts[nc],
                    sort->firstVerts[mf],
                    sort->firstTVerts[tf],
                    true);
      }
      else if (bd)
      {
         Dts3::SortedData* sd = mi.mMesh->getSortedData();
         renderMesh(mi, ri, bd,
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
            renderDecal(mi, smi, ri, bd, dd);
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

   int32_t findIflMaterialSlot(uint32_t slotIndex) const
   {
      for (size_t i=0; i<mShape->mIflMaterials.size(); i++)
      {
         if (mShape->mIflMaterials[i].slot == slotIndex)
            return (int32_t)i;
      }
      return -1;
   }

   MaterialBindingInfo resolveMaterialBinding(uint32_t baseMaterialIndex, const RuntimeObjectInfo& objectInfo) const
   {
      MaterialBindingInfo binding;
      const uint32_t materialCount = mMaterialList ? mMaterialList->size() : 0;
      if (materialCount == 0)
         return binding;

      uint32_t resolvedMaterialIndex = std::min(baseMaterialIndex, materialCount - 1);
      int32_t iflIndex = findIflMaterialSlot(resolvedMaterialIndex);
      if (iflIndex >= 0 && iflIndex < mRuntimeIflMaterialInfos.size())
      {
         const RuntimeIflMaterialInfo& runtimeIfl = mRuntimeIflMaterialInfos[iflIndex];
         const Dts3::IflMaterial& ifl = mShape->mIflMaterials[iflIndex];
         uint32_t iflFrame = (uint32_t)std::max(runtimeIfl.mFrame, 0);
         if (ifl.numFrames > 0)
            iflFrame = std::min<uint32_t>(iflFrame, (uint32_t)ifl.numFrames - 1);
         const uint32_t iflMaterialIndex = ifl.firstFrame + iflFrame;
         if (iflMaterialIndex < materialCount)
            resolvedMaterialIndex = iflMaterialIndex;
      }
      else if (objectInfo.mLastMatFrame > 0)
      {
         const uint32_t frameMaterialIndex = resolvedMaterialIndex + (uint32_t)objectInfo.mLastMatFrame;
         if (frameMaterialIndex < materialCount)
            resolvedMaterialIndex = frameMaterialIndex;
      }

      binding.materialIndex = resolvedMaterialIndex;
      binding.pipelineState = calcPipelineState(mMaterialList->operator[](resolvedMaterialIndex).tsProps.flags);
      if (useShared)
      {
         binding.textureGroupID = mSharedMaterials.tex.texID;
         binding.materialFrame = resolvedMaterialIndex;
      }
      else if (resolvedMaterialIndex < mActiveMaterials.size())
      {
         binding.textureGroupID = mActiveMaterials[resolvedMaterialIndex].texGroupID;
      }

      return binding;
   }
   
   void renderDecal(RuntimeMeshInfo& mi, RuntimeMeshInfo& smi, RuntimeObjectInfo& ri, Dts3::BasicData* bd, Dts3::DecalData* dd)
   {
      const uint32_t meshVertOffset = smi.mVertOffset + (smi.mMeshFrame * smi.mRealVertsPerFrame);
      const uint32_t meshTexOffset = smi.mVertOffset + (smi.mMeshTexFrame * smi.mRealVertsPerFrame);
      const uint32_t meshSkinOffset = smi.mSkinVertOffset + (smi.mUseSkinData ? (smi.mMeshFrame * smi.mRealVertsPerFrame) : 0);
      GFXSetModelVerts(0, meshVertOffset, meshTexOffset, smi.mIndexOffset, meshSkinOffset);
      GFXSetModelViewProjection(mModelMatrix, mViewMatrix, mProjectionMatrix, smi.mRenderFlags);
      
      uint32_t start = dd->startPrimitive[mi.mMeshFrame];
      uint32_t end = mi.mMeshFrame+1 < dd->startPrimitive.size() ? dd->startPrimitive[mi.mMeshFrame+1] : dd->primitives.size();
      bool haveBoundState = false;
      ModelPipelineState currentPipelineState = ModelPipeline_DefaultDiffuse;
      uint32_t currentGroupID = 0;
      
      for (uint32_t i=start; i<end; i++)
      {
         Dts3::Primitive& prim = dd->primitives[i];
         uint32_t matIndex = dd->matIndex & Dts3::Primitive::MaterialMask;
         uint32_t drawMode = prim.matIndex & Dts3::Primitive::TypeMask;
         MaterialBindingInfo binding = resolveMaterialBinding(matIndex, ri);
         if (!haveBoundState ||
             binding.pipelineState != currentPipelineState ||
             binding.textureGroupID != currentGroupID)
         {
            GFXBeginTSModelPipelineState(binding.pipelineState,
                                         binding.textureGroupID,
                                         1.1f, false, false);
            currentPipelineState = binding.pipelineState;
            currentGroupID = binding.textureGroupID;
            haveBoundState = true;
         }
         GFXSetTSPipelineProps(binding.materialFrame, smi.mMeshTransformOffset, dd->texGenS[mi.mMeshFrame], dd->texGenT[mi.mMeshFrame]);
         
         assert(drawMode == Dts3::Primitive::Triangles);
         
         GFXDrawModelPrims(smi.mRealVertsPerFrame,
                           prim.numElements,
                           prim.firstElement,
                           0);
      }
   }
   
   void renderMesh(RuntimeMeshInfo& mi, RuntimeObjectInfo& ri, Dts3::BasicData* bd, uint32_t drawVerts, uint32_t firstVert, uint32_t firstTVert, bool depthPeel=false)
   {
      if (!validateMeshDrawIndices(mi, bd, "renderMesh"))
         return;

      const uint32_t meshVertOffset = mi.mVertOffset + firstVert;
      const uint32_t meshTexOffset = mi.mVertOffset + firstTVert;
      const uint32_t meshSkinOffset = mi.mSkinVertOffset + (mi.mUseSkinData ? firstVert : 0);
      GFXSetModelVerts(0, meshVertOffset, meshTexOffset, mi.mIndexOffset, meshSkinOffset);
      GFXSetModelViewProjection(mModelMatrix, mViewMatrix, mProjectionMatrix, mi.mRenderFlags);
      
      // NOTE: if we wanted to more optimally batch, emitting a drawcall per matIndex would make
      // more sense here.
      // Unfortunately we can't use texture arrays for everything here since the material list
      // doesn't guarantee that every texture is consistently sized.
      
      uint32_t passes = depthPeel ? 4 : 1;
      
      for (uint32_t i=0; i<passes; i++)
      {
         bool haveBoundState = false;
         ModelPipelineState currentPipelineState = ModelPipeline_DefaultDiffuse;
         uint32_t currentGroupID = 0;
         for (Dts3::Primitive& prim : bd->primitives)
         {
            uint32_t matIndex = prim.matIndex & Dts3::Primitive::MaterialMask;
            uint32_t drawMode = prim.matIndex & Dts3::Primitive::TypeMask;
            MaterialBindingInfo binding = resolveMaterialBinding(matIndex, ri);
            if (!haveBoundState ||
                binding.pipelineState != currentPipelineState ||
                binding.textureGroupID != currentGroupID)
            {
               GFXBeginTSModelPipelineState(binding.pipelineState, 
                                            binding.textureGroupID,
                                            1.1f, depthPeel, (passes % 2) == 0);
               currentPipelineState = binding.pipelineState;
               currentGroupID = binding.textureGroupID;
               haveBoundState = true;
            }
            GFXSetTSPipelineProps(binding.materialFrame, mi.mMeshTransformOffset, slm::vec4(0), slm::vec4(0));
            
            assert(drawMode == Dts3::Primitive::Triangles);
            
            GFXDrawModelPrims(mi.mRealVertsPerFrame,
                              prim.numElements,
                              prim.firstElement,
                              0);
         }
      }
   }
   
   void renderDetail(uint32_t detailLevel)
   {
      Dts3::DetailLevel& level = mShape->mDetailLevels[detailLevel];
      if (!isRenderableDetail(level))
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

      if (nodeIdx >= (int32_t)mNodeTransforms.size())
         return;

      const Dts3::Node& node = mShape->mNodes[nodeIdx];
      const slm::vec4 worldPos4 = mModelMatrix * slm::vec4(mNodeTransforms[nodeIdx][3].xyz(), 1.0f);
      const slm::vec3 worldPos = worldPos4.xyz();

      if (node.parent >= 0)
      {
         const slm::vec4 color = nodeIdx == highlightIdx ? slm::vec4(0, 1, 0, 1) : slm::vec4(1, 0, 0, 1);
         GFXBeginLinePipelineState();
         GFXDrawLine(parentPos, worldPos, color, 1.0f);
      }

      for (int32_t childIdx = node.firstChild; childIdx >= 0; childIdx = mShape->mNodes[childIdx].nextSibling)
      {
         renderNodes(childIdx, worldPos, highlightIdx);
      }
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
   int32_t mSelectedMaterialIdx;
   
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
      yRot = 0.0f;
      mShape = NULL;
      mHighlightNodeIdx = -1;
      mSelectedMaterialIdx = 0;
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
         mDetailDist = std::max<float>((float)mViewer.mCurrentDetail, 0.0f);
         mSelectedMaterialIdx = 0;
         rebuildSequenceUI();
         
         const float viewDist = std::max(mViewer.mShape->mRadius * 2.0f, 1.0f);
         mViewPos = mViewer.mShape->mCenter + slm::vec3(0.0f, -viewDist, 0.0f);
         mCamRot = slm::vec3(0.0f, 0.0f, 0.0f);
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
      mDetailDist = std::max<float>((float)mViewer.mCurrentDetail, 0.0f);
      mSelectedMaterialIdx = 0;
      rebuildSequenceUI();
      return true;
   }

   void renderMaterialDebugWindow()
   {
      ImGui::Begin("Materials");

      if (mShape == NULL || mViewer.mMaterialList == NULL)
      {
         ImGui::TextUnformatted("No shape loaded.");
         ImGui::End();
         return;
      }

      const int materialCount = (int)mViewer.mMaterialList->mMaterials.size();
      if (materialCount <= 0)
      {
         ImGui::TextUnformatted("No materials.");
         ImGui::End();
         return;
      }

      mSelectedMaterialIdx = std::clamp(mSelectedMaterialIdx, 0, materialCount - 1);

      int iflFrameMaterialCount = 0;
      for (const MaterialList::Material& material : mViewer.mMaterialList->mMaterials)
      {
         if ((material.tsProps.flags & MaterialList::IflFrame) != 0)
            iflFrameMaterialCount++;
      }

      ImGui::Text("Total: %d  IFL Materials: %d  IFL Frames: %d",
                  materialCount, (int)mShape->mIflMaterials.size(), iflFrameMaterialCount);
      ImGui::Separator();

      ImGui::Columns(2, "materials_columns", true);

      if (ImGui::BeginListBox("##material_list", ImVec2(-FLT_MIN, 320.0f)))
      {
         for (int i = 0; i < materialCount; ++i)
         {
            const MaterialList::Material& mat = mViewer.mMaterialList->mMaterials[i];
            const bool selected = i == mSelectedMaterialIdx;
            const char* matName = mat.name.empty() ? "<blank>" : mat.name.c_str();
            char entryLabel[1024];
            snprintf(entryLabel, sizeof(entryLabel), "%s##mat_%d", matName, i);
            if (ImGui::Selectable(entryLabel, selected))
               mSelectedMaterialIdx = i;
            if (ImGui::IsItemFocused())
               mSelectedMaterialIdx = i;
            if (selected)
               ImGui::SetItemDefaultFocus();
         }
         ImGui::EndListBox();
      }

      ImGui::NextColumn();

      const MaterialList::Material& mat = mViewer.mMaterialList->mMaterials[mSelectedMaterialIdx];
      const GenericViewer::ActiveMaterial* activeMat = mSelectedMaterialIdx < (int)mViewer.mActiveMaterials.size() ? &mViewer.mActiveMaterials[mSelectedMaterialIdx] : NULL;
      const GenericViewer::LoadedTexture* tex = activeMat ? &activeMat->tex : NULL;
      void* textureHandle = tex ? GFXGetTextureViewHandle(tex->texID) : NULL;

      ImGui::Text("Index: %d", mSelectedMaterialIdx);
      ImGui::TextWrapped("Name: %s", mat.name.empty() ? "<blank>" : mat.name.c_str());
      ImGui::Text("IFL Frame: %s", (mat.tsProps.flags & MaterialList::IflFrame) != 0 ? "yes" : "no");
      ImGui::Separator();
      ImGui::Text("Flags: 0x%04x", mat.tsProps.flags);
      ImGui::Text("Reflect: %d  Bump: %d  Detail: %d",
                  mat.tsProps.reflectanceMap, mat.tsProps.bumpMap, mat.tsProps.detailMap);
      ImGui::Text("DetailScale: %.3f  ReflectAmt: %.3f",
                  mat.tsProps.detailScale, mat.tsProps.reflectionAmount);

      if (activeMat)
      {
         ImGui::Text("TexID: %d  Group: %u", activeMat->tex.texID, activeMat->texGroupID);
         ImGui::Text("Size: %ux%u", activeMat->tex.width, activeMat->tex.height);
      }
      else
      {
         ImGui::TextUnformatted("No runtime material state.");
      }

      if (textureHandle)
      {
         ImGui::Separator();
         ImGui::TextUnformatted("Preview");
         float maxPreview = 256.0f;
         float previewW = tex->width > 0 ? (float)tex->width : maxPreview;
         float previewH = tex->height > 0 ? (float)tex->height : maxPreview;
         float scale = 1.0f;
         if (previewW > maxPreview || previewH > maxPreview)
            scale = std::min(maxPreview / previewW, maxPreview / previewH);
         previewW *= scale;
         previewH *= scale;
         ImGui::Image((ImTextureID)textureHandle, ImVec2(previewW, previewH));
      }
      else
      {
         ImGui::Separator();
         ImGui::TextUnformatted("Preview unavailable.");
      }

      ImGui::Columns(1);
      ImGui::End();
   }
   
   bool dumpLoadedShapeOBJ(const char *filename)
   {
      if (mShape == NULL || filename == NULL)
      {
         return false;
      }

      std::ofstream out(filename);
      if (!out.is_open())
      {
         fprintf(stderr, "failed to open OBJ dump '%s'\n", filename);
         return false;
      }

      uint32_t vertexBase = 1;
      uint32_t texcoordBase = 1;
      for (uint32_t meshIdx = 0; meshIdx < mShape->mMeshes.size(); meshIdx++)
      {
         Dts3::Mesh& mesh = mShape->mMeshes[meshIdx];
         Dts3::BasicData* basicData = mesh.getBasicData();
         if (basicData == NULL)
            continue;

         const uint32_t vertsPerFrame = mesh.mVertsPerFrame ? mesh.mVertsPerFrame : (uint32_t)basicData->verts.size();
         const uint32_t texVertsPerFrame = mesh.mVertsPerFrame ? std::min<uint32_t>(mesh.mVertsPerFrame, (uint32_t)basicData->tverts.size()) : (uint32_t)basicData->tverts.size();
         if (vertsPerFrame == 0)
            continue;

         std::string meshName = "mesh_" + std::to_string(meshIdx);
         out << "g " << meshName << "\n";
         out << "o " << meshName << "\n";

         for (uint32_t i = 0; i < vertsPerFrame && i < basicData->verts.size(); i++)
         {
            const slm::vec3& v = basicData->verts[i];
            out << "v " << v.x << " " << v.y << " " << v.z << "\n";
         }

         for (uint32_t i = 0; i < texVertsPerFrame && i < basicData->tverts.size(); i++)
         {
            const slm::vec2& vt = basicData->tverts[i];
            out << "vt " << vt.x << " " << (1.0f - vt.y) << "\n";
         }

         for (const Dts3::Primitive& prim : basicData->primitives)
         {
            const uint32_t drawMode = prim.matIndex & Dts3::Primitive::TypeMask;
            if (drawMode != Dts3::Primitive::Triangles)
               continue;

            for (uint32_t i = 0; i + 2 < prim.numElements; i += 3)
            {
               const uint32_t i0 = basicData->indices[prim.firstElement + i + 0];
               const uint32_t i1 = basicData->indices[prim.firstElement + i + 1];
               const uint32_t i2 = basicData->indices[prim.firstElement + i + 2];

               if (i0 >= vertsPerFrame || i1 >= vertsPerFrame || i2 >= vertsPerFrame)
                  continue;

               const uint32_t v0 = vertexBase + i0;
               const uint32_t v1 = vertexBase + i1;
               const uint32_t v2 = vertexBase + i2;

               if (i0 < texVertsPerFrame && i1 < texVertsPerFrame && i2 < texVertsPerFrame)
               {
                  const uint32_t t0 = texcoordBase + i0;
                  const uint32_t t1 = texcoordBase + i1;
                  const uint32_t t2 = texcoordBase + i2;
                  out << "f "
                      << v0 << "/" << t0 << " "
                      << v1 << "/" << t1 << " "
                      << v2 << "/" << t2 << "\n";
               }
               else
               {
                  out << "f " << v0 << " " << v1 << " " << v2 << "\n";
               }
            }
         }

         out << "\n";
         vertexBase += vertsPerFrame;
         texcoordBase += texVertsPerFrame;
      }

      return true;
   }

   bool dumpPackedShapeOBJ(const char *filename)
   {
      return mViewer.dumpPackedModelOBJ(filename);
   }
   
   void update(float dt)
   {
      mViewer.mModelMatrix = slm::rotation_x(xRot) * slm::rotation_z(yRot);
      slm::mat4 rotMat = slm::rotation_z(slm::radians(mCamRot.z)) * slm::rotation_y(slm::radians(mCamRot.y)) *  slm::rotation_x(slm::radians(mCamRot.x));
      rotMat = inverse(rotMat);
      mViewer.mViewMatrix = kTorqueZUpViewBasis * rotMat * slm::translation(-mViewPos);
      
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
      int32_t detailSelection = (int32_t)std::lround(mDetailDist);
      int32_t maxDetailSelection = mShape ? std::max<int32_t>((int32_t)mShape->mDetailLevels.size() - 1, 0) : 0;
      if (ImGui::SliderInt("Detail Level", &detailSelection, 0, maxDetailSelection))
      {
         mDetailDist = (float)detailSelection;
      }
      ImGui::Checkbox("Render Nodes", &mRenderNodes);
      ImGui::End();

      renderMaterialDebugWindow();
      
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
   
   assert(sizeof(slm::vec2) == 8);
   assert(sizeof(slm::vec3) == 12);
   assert(sizeof(slm::vec4) == 16);
   
   ConsolePersistObject::initStatics();
   ResManager::initStatics();
   
   if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0) {
      printf("Couldn't initialize SDL: %s\n", SDL_GetError());
      return (1);
   }
   
   window = SDL_CreateWindow("DTS Viewer", 1024, 700, SDL_WINDOW_METAL | SDL_WINDOW_HIGH_PIXEL_DENSITY | SDL_WINDOW_RESIZABLE);
   if (window == NULL) {
      printf( "Window could not be created! SDL_Error: %s\n", SDL_GetError() );
      return (1);
   }
   
   // Init basic main
   gMainState.init(window, argc, argv);
   
   int setupCode = GFXSetup(window, NULL);
   
   if (setupCode < 0)
   {
      return 1;
   }
   
   // Non-Emscripten setup
   while (setupCode != 0)
   {
      setupCode = GFXSetup(window, NULL);
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
   std::string dumpObjPath;
   std::string dumpPackedObjPath;
   
   for (int i=1; i<in_argc; i++)
   {
      const char *path = in_argv[i];
      if (path && strcmp(path, "--dump-obj") == 0)
      {
         i++;
         continue;
      }
      if (path && strcmp(path, "--dump-packed-obj") == 0)
      {
         i++;
         continue;
      }
      if (path && path[0] == '-')
         continue;

      fs::path filePath = path;
      std::string  ext = filePath.extension();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

      if (fs::is_directory(filePath))
      {
         resManager.mPaths.emplace_back(path);
         continue;
      }

      if (ext == ".vol" || ext == ".zip")
      {
         resManager.addVolume(path);
      }
   }

   for (int i=1; i<in_argc; i++)
   {
      const char *path = in_argv[i];
      if (path && strcmp(path, "--dump-obj") == 0)
      {
         if (i + 1 >= in_argc)
         {
            fprintf(stderr, "--dump-obj requires an output path\n");
            return 1;
         }
         dumpObjPath = in_argv[++i];
         continue;
      }
      if (path && strcmp(path, "--dump-packed-obj") == 0)
      {
         if (i + 1 >= in_argc)
         {
            fprintf(stderr, "--dump-packed-obj requires an output path\n");
            return 1;
         }
         dumpPackedObjPath = in_argv[++i];
         continue;
      }
      if (path && path[0] == '-')
         continue;
      
      fs::path filePath = path;
      std::string  ext = filePath.extension();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

      if (fs::is_directory(filePath))
         continue;
      
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
   }
   
   if (!currentController->isResourceLoaded())
   {
      fprintf(stderr, "please specify a starting shape or interior or terrain to load\n");
      return 1;
   }

   if (!dumpObjPath.empty())
   {
      if (!shapeController->dumpLoadedShapeOBJ(dumpObjPath.c_str()))
         return 1;
   }
   if (!dumpPackedObjPath.empty())
   {
      if (!shapeController->dumpPackedShapeOBJ(dumpPackedObjPath.c_str()))
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
   const bool captureKeyboard = ImGui::GetIO().WantCaptureKeyboard;
   if (!captureKeyboard)
   {
      currentController->mCamRot += deltaRot * dt * 100;
      slm::mat4 rotMat = slm::rotation_z(slm::radians(currentController->mCamRot.z)) * slm::rotation_y(slm::radians(currentController->mCamRot.y)) *  slm::rotation_x(slm::radians(currentController->mCamRot.x));
      slm::vec4 forwardVec = rotMat * slm::vec4(deltaMovement, 0);
      currentController->mViewPos += forwardVec.xyz() * currentController->mViewSpeed * dt;
   }
   
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
      const bool captureKeyboardEvent = ImGui::GetIO().WantCaptureKeyboard;
      
      switch (event.type)
      {
         case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
         case SDL_EVENT_WINDOW_RESIZED:
            GFXHandleResize();
            break;
            
         case SDL_EVENT_KEY_DOWN:
         case SDL_EVENT_KEY_UP:
         {
            if (captureKeyboardEvent)
               break;
            switch (event.key.key)
            {
               case SDLK_A:  deltaMovement.x = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_D:  deltaMovement.x = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_Q:  deltaMovement.z = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_E:  deltaMovement.z = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_W:  deltaMovement.y = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_S:  deltaMovement.y = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_LEFT:  deltaRot.z = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_RIGHT: deltaRot.z = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_UP:  deltaRot.x = event.type == SDL_EVENT_KEY_DOWN ? 1 : 0; break;
               case SDLK_DOWN: deltaRot.x = event.type == SDL_EVENT_KEY_DOWN ? -1 : 0; break;
               case SDLK_F12:
                  if (event.type == SDL_EVENT_KEY_DOWN)
                  {
                     GFXRequestDebuggerCapture();
                  }
                  break;
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
