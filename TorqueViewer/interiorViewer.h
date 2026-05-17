//-----------------------------------------------------------------------------
// Interior viewer
//-----------------------------------------------------------------------------

#ifndef TORQUEVIEWER_INTERIORVIEWER_H
#define TORQUEVIEWER_INTERIORVIEWER_H

#include <sstream>
#include <unordered_set>

#include <dif/objects/dif.h>

//-----------------------------------------------------------------------------
// Interior rendering helpers
//-----------------------------------------------------------------------------

static inline float interiorPlaneDistance(const DIF::PlaneF& plane, const slm::vec3& point)
{
   return (plane.x * point.x) + (plane.y * point.y) + (plane.z * point.z) + plane.d;
}

static inline slm::vec3 interiorSwizzle(const glm::vec3& src)
{
   return slm::vec3(src.x, src.y, src.z);
}

class InteriorViewer : public GenericViewer
{
public:
   struct InteriorPart
   {
      uint32_t materialIndex;
      uint32_t firstIndex;
      uint32_t indexCount;

      InteriorPart() : materialIndex(0), firstIndex(0), indexCount(0) {;}
   };

   DIF::DIF mDIF;
   DIF::Interior* mInterior;
   MaterialList mInteriorMaterials;
   std::vector<InteriorPart> mParts;
   ShapeViewer::PackedModelData mPacked;
   bool mGeometryReady;
   bool mDebugRenderNormals;
   bool mDisableLighting;

   InteriorViewer(ResManager* res)
      : mInterior(NULL), mGeometryReady(false)
   {
      mResourceManager = res;
      mMaterialList = &mInteriorMaterials;
      mLightColor = slm::vec4(1.0f);
      mLightPos = slm::vec3(0.0f, -2.0f, 2.0f);
      mAppliedLightPos = mLightPos;
      mAppliedLightRange = 0.0f;
      mLightFollowsCamera = false;
      mDirectionalLight = true;
      mScaleLightByShapeRadius = true;
      mDebugRenderNormals = false;
      mDisableLighting = false;
   }

   ~InteriorViewer()
   {
      clear();
   }

   void clear()
   {
      GFXClearModelData(0);
      mParts.clear();
      mPacked.verts.clear();
      mPacked.texVerts.clear();
      mPacked.skinVerts.clear();
      mPacked.indices.clear();
      mGeometryReady = false;
      mInterior = NULL;
      mDIF = DIF::DIF();
      mInteriorMaterials.free();
      mMaterialList = &mInteriorMaterials;
      mResourceFilename.clear();
      mResourceMount = -1;
      clearTextures();
      mAppliedLightPos = slm::vec3(0.0f, -2.0f, 2.0f);
      mAppliedLightRange = 0.0f;
   }

   static ModelPipelineState calcPipelineState(uint32_t flags)
   {
      if ((flags & MaterialList::Additive) != 0)
         return ModelPipeline_AdditiveBlend;
      if ((flags & MaterialList::Subtractive) != 0)
         return ModelPipeline_SubtractiveBlend;
      if ((flags & MaterialList::Translucent) != 0)
         return ModelPipeline_TranslucentBlend;
      return ModelPipeline_DefaultDiffuse;
   }

   void buildPackedGeometry()
   {
      mParts.clear();
      mPacked.verts.clear();
      mPacked.texVerts.clear();
      mPacked.indices.clear();

      if (!mInterior)
         return;

      const size_t materialCount = mInteriorMaterials.mMaterials.size();
      if (materialCount == 0)
         return;

      for (size_t surfIdx = 0; surfIdx < mInterior->surface.size(); ++surfIdx)
      {
         const DIF::Interior::Surface& surface = mInterior->surface[surfIdx];
         if (surface.textureIndex >= materialCount)
            continue;
         if (surface.planeIndex >= mInterior->plane.size() ||
             surface.texGenIndex >= mInterior->texGenEq.size())
            continue;
         if (surface.windingCount < 3)
            continue;
         if (surface.windingStart + surface.windingCount > mInterior->index.size())
            continue;

         const DIF::Interior::Plane& plane = mInterior->plane[surface.planeIndex];
         if (plane.normalIndex >= mInterior->normal.size())
            continue;

         slm::vec3 normal = interiorSwizzle(mInterior->normal[plane.normalIndex]);
         if (surface.planeFlipped)
            normal *= -1.0f;

         const DIF::Interior::TexGenEq& texGenEq = mInterior->texGenEq[surface.texGenIndex];
         const uint32_t firstIndex = (uint32_t)mPacked.indices.size();
         const uint32_t firstVertex = (uint32_t)mPacked.verts.size();
         uint32_t triangleCount = 0;

         for (uint32_t j = surface.windingStart + 2; j < surface.windingStart + surface.windingCount; ++j)
         {
            const bool flipWinding = ((j - (surface.windingStart + 2)) % 2) != 0;
            const uint32_t i0 = mInterior->index[j - 2];
            const uint32_t i1 = mInterior->index[j - 1];
            const uint32_t i2 = mInterior->index[j - 0];

            if (i0 >= mInterior->point.size() ||
                i1 >= mInterior->point.size() ||
                i2 >= mInterior->point.size())
               continue;

            const glm::vec3 p0 = mInterior->point[flipWinding ? i2 : i0];
            const glm::vec3 p1 = mInterior->point[i1];
            const glm::vec3 p2 = mInterior->point[flipWinding ? i0 : i2];

            const slm::vec3 v0 = interiorSwizzle(p0);
            const slm::vec3 v1 = interiorSwizzle(p1);
            const slm::vec3 v2 = interiorSwizzle(p2);

            const slm::vec2 uv0(interiorPlaneDistance(texGenEq.planeX, slm::vec3(p0.x, p0.y, p0.z)), interiorPlaneDistance(texGenEq.planeY, slm::vec3(p0.x, p0.y, p0.z)));
            const slm::vec2 uv1(interiorPlaneDistance(texGenEq.planeX, slm::vec3(p1.x, p1.y, p1.z)), interiorPlaneDistance(texGenEq.planeY, slm::vec3(p1.x, p1.y, p1.z)));
            const slm::vec2 uv2(interiorPlaneDistance(texGenEq.planeX, slm::vec3(p2.x, p2.y, p2.z)), interiorPlaneDistance(texGenEq.planeY, slm::vec3(p2.x, p2.y, p2.z)));

            if (mPacked.verts.size() + 3 >= 65535)
               break;

            mPacked.verts.push_back({v0, normal});
            mPacked.verts.push_back({v1, normal});
            mPacked.verts.push_back({v2, normal});

            mPacked.texVerts.push_back({uv0});
            mPacked.texVerts.push_back({uv1});
            mPacked.texVerts.push_back({uv2});

            mPacked.indices.push_back((uint16_t)(firstVertex + triangleCount * 3 + 0));
            mPacked.indices.push_back((uint16_t)(firstVertex + triangleCount * 3 + 1));
            mPacked.indices.push_back((uint16_t)(firstVertex + triangleCount * 3 + 2));
            triangleCount++;
         }

         if (triangleCount > 0)
         {
            InteriorPart part;
            part.materialIndex = (uint32_t)surface.textureIndex;
            part.firstIndex = firstIndex;
            part.indexCount = triangleCount * 3;
            mParts.push_back(part);
         }
      }
   }

   bool loadInterior(const char* filename, int pathIdx = -1)
   {
      clear();
      setResourcePath(filename, pathIdx);

      MemRStream stream(0, NULL);
      if (!mResourceManager->openFile(filename, stream, pathIdx))
      {
         fprintf(stderr, "failed to open interior '%s'\n", filename ? filename : "<null>");
         return false;
      }

      std::string interiorBytes((const char*)stream.mPtr, (size_t)stream.mSize);
      std::istringstream interiorStream(interiorBytes, std::ios::binary);

      DIF::Version version;
      if (!mDIF.read(interiorStream, version))
      {
         fprintf(stderr, "failed to parse interior '%s'\n", filename ? filename : "<null>");
         clear();
         return false;
      }

      if (mDIF.interior.empty())
      {
         fprintf(stderr, "interior '%s' contains no interior blocks\n", filename ? filename : "<null>");
         clear();
         return false;
      }

      mInterior = &mDIF.interior[0];
      mInteriorMaterials.free();
      for (const std::string& materialName : mInterior->materialName)
      {
         mInteriorMaterials.push_back(materialName.c_str());
      }
      if (mInteriorMaterials.mMaterials.empty())
         mInteriorMaterials.push_back("");

      mMaterialList = &mInteriorMaterials;
      initMaterials();

      buildPackedGeometry();
      if (mPacked.verts.empty() || mPacked.indices.empty())
      {
         fprintf(stderr, "interior '%s' produced no renderable geometry\n", filename ? filename : "<null>");
         clear();
         return false;
      }

      GFXLoadModelData(0,
                       mPacked.verts.empty() ? NULL : &mPacked.verts[0],
                       mPacked.texVerts.empty() ? NULL : &mPacked.texVerts[0],
                       mPacked.indices.empty() ? NULL : &mPacked.indices[0],
                       NULL,
                       (uint32_t)mPacked.verts.size(),
                       (uint32_t)mPacked.texVerts.size(),
                       (uint32_t)mPacked.indices.size(),
                       0);

      mGeometryReady = true;
      mLightPos = slm::vec3(0.0f, -2.0f, 2.0f);
      mLightColor = slm::vec4(1.0f);
      mAppliedLightPos = mLightPos;
      mAppliedLightRange = 0.0f;
      return true;
   }

   void render()
   {
      if (!mGeometryReady || mInterior == NULL || mParts.empty())
         return;

      updateMVP();
      GFXSetModelVerts(0, 0, 0, 0, 0);

      for (const InteriorPart& part : mParts)
      {
         if (part.materialIndex >= mMaterialList->mMaterials.size())
            continue;

         const MaterialList::Material& mat = mMaterialList->mMaterials[part.materialIndex];
         const uint32_t materialFlags = mat.tsProps.flags;
         const uint32_t textureGroupID = part.materialIndex < mActiveMaterials.size()
            ? mActiveMaterials[part.materialIndex].texGroupID
            : 0;
         const ModelPipelineState pipelineState = calcPipelineState(materialFlags);

         GFXBeginBasicModelPipelineState(pipelineState, textureGroupID, 1.1f, false, false);
         GFXSetTSPipelineProps(0,
                               0,
                               slm::vec4(0.0f),
                               slm::vec4(0.0f),
                               materialFlags,
                               false,
                               mDebugRenderNormals,
                               mDisableLighting,
                               slm::vec4(1.0f, 0.2f, 0.0f, 1.0f),
                               1.0e-4f);

         GFXDrawModelPrims((uint32_t)mPacked.verts.size(), part.indexCount, part.firstIndex, 0);
      }
   }
};

class InteriorViewerController : public ViewController
{
public:
   InteriorViewer mViewer;
   SDL_Window* mWindow;
   DIF::Interior* mInterior;
   float xRot, yRot;
   bool mRenderMaterials;

   InteriorViewerController(SDL_Window* window, ResManager* mgr)
      : mViewer(mgr)
   {
      mWindow = window;
      mInterior = NULL;
      mViewPos = slm::vec3(0.0f);
      mCamRot = slm::vec3(0.0f);
      mViewSpeed = 1.0f;
      xRot = 0.0f;
      yRot = 0.0f;
      mRenderMaterials = true;
      mViewer.mDirectionalLight = true;
      mViewer.mLightFollowsCamera = true;
      mViewer.mScaleLightByShapeRadius = true;
   }

   ~InteriorViewerController()
   {
   }

   bool isResourceLoaded()
   {
      return mViewer.mInterior != NULL;
   }

   bool loadInterior(const char* filename, int pathIdx = -1)
   {
      if (!mViewer.loadInterior(filename, pathIdx))
         return false;

      mInterior = mViewer.mInterior;
      mViewer.mDirectionalLight = true;
      mViewer.mLightFollowsCamera = true;
      mViewer.mScaleLightByShapeRadius = true;
      mViewer.mLightRadiusScale = 1.0f;
      const float radius = std::max(mInterior ? mInterior->boundingSphere.radius : 1.0f, 1.0f);
      const slm::vec3 center(mInterior ? mInterior->boundingSphere.x : 0.0f,
                             mInterior ? mInterior->boundingSphere.y : 0.0f,
                             mInterior ? mInterior->boundingSphere.z : 0.0f);
      mViewPos = center + slm::vec3(0.0f, -radius * 2.0f, 0.0f);
      mCamRot = slm::vec3(0.0f);
      xRot = 0.0f;
      yRot = 0.0f;
      return true;
   }

   void update(float dt)
   {
      mViewer.mModelMatrix = slm::rotation_x(xRot) * slm::rotation_z(yRot);

      slm::mat4 rotMat = slm::rotation_z(slm::radians(mCamRot.z)) * slm::rotation_y(slm::radians(mCamRot.y)) * slm::rotation_x(slm::radians(mCamRot.x));
      rotMat = inverse(rotMat);
      mViewer.mViewMatrix = kTorqueZUpViewBasis * rotMat * slm::translation(-mViewPos);

      int w, h;
      SDL_GetWindowSize(mWindow, &w, &h);
      mViewer.mProjectionMatrix = slm::perspective_fov_rh(slm::radians(90.0), (float)w / (float)h, 0.01f, 10000.0f);

      if (mViewer.mLightFollowsCamera)
      {
         if (mViewer.mDirectionalLight)
         {
            const slm::mat4 invView = inverse(mViewer.mViewMatrix);
            const slm::vec3 forwardDir = (invView * slm::vec4(0.0f, 1.0f, 0.0f, 0.0f)).xyz();
            mViewer.mAppliedLightPos = GenericViewer::normalizeOrDefault(forwardDir, slm::vec3(0.0f, 1.0f, 0.0f));
            mViewer.mAppliedLightRange = 0.0f;
         }
         else
         {
            mViewer.mAppliedLightPos = mViewPos;
            const float radius = std::max(mInterior ? mInterior->boundingSphere.radius : 1.0f, 1.0f);
            mViewer.mAppliedLightRange = mViewer.mScaleLightByShapeRadius ? std::max(radius * mViewer.mLightRadiusScale, 1.0f) : 0.0f;
         }
      }
      else
      {
         const float radius = std::max(mInterior ? mInterior->boundingSphere.radius : 1.0f, 1.0f);
         mViewer.mAppliedLightPos = mViewer.mDirectionalLight
            ? GenericViewer::normalizeOrDefault(mViewer.mLightPos, slm::vec3(0.0f, 1.0f, 0.0f))
            : mViewer.mLightPos;
         mViewer.mAppliedLightRange = (!mViewer.mDirectionalLight && mViewer.mScaleLightByShapeRadius)
            ? std::max(radius * mViewer.mLightRadiusScale, 1.0f)
            : 0.0f;
      }

      mViewer.render();

      ImGui::Begin("View");
      ImGui::SliderAngle("X Rotation", &xRot);
      ImGui::SliderAngle("Y Rotation", &yRot);
      ImGui::Separator();
      ImGui::TextUnformatted("Lighting");
      ImGui::Checkbox("Directional Light", &mViewer.mDirectionalLight);
      ImGui::Checkbox("Follow Camera", &mViewer.mLightFollowsCamera);
      ImGui::Checkbox("Scale Light Radius With Bounds", &mViewer.mScaleLightByShapeRadius);
      ImGui::SliderFloat("Radius Scale", &mViewer.mLightRadiusScale, 0.01f, 4.0f);
      if (mViewer.mDirectionalLight)
         ImGui::TextUnformatted("Light Direction (world)");
      else
         ImGui::TextUnformatted("Light Position (world)");
      ImGui::DragFloat3("##LightVector", &mViewer.mLightPos.x, 0.05f);
      ImGui::ColorEdit3("Light Color", &mViewer.mLightColor.x);
      float lightIntensity = std::max({mViewer.mLightColor.x, mViewer.mLightColor.y, mViewer.mLightColor.z, 0.0f});
      if (ImGui::SliderFloat("Light Intensity", &lightIntensity, 0.0f, 8.0f))
      {
         if (lightIntensity <= 0.0f)
         {
            mViewer.mLightColor.x = 0.0f;
            mViewer.mLightColor.y = 0.0f;
            mViewer.mLightColor.z = 0.0f;
         }
         else
         {
            float prevIntensity = std::max({mViewer.mLightColor.x, mViewer.mLightColor.y, mViewer.mLightColor.z, 0.0f});
            if (prevIntensity > 0.0f)
            {
               float scale = lightIntensity / prevIntensity;
               mViewer.mLightColor.x *= scale;
               mViewer.mLightColor.y *= scale;
               mViewer.mLightColor.z *= scale;
            }
            else
            {
               mViewer.mLightColor.x = lightIntensity;
               mViewer.mLightColor.y = lightIntensity;
               mViewer.mLightColor.z = lightIntensity;
            }
         }
      }
      ImGui::Checkbox("Debug Normals", &mViewer.mDebugRenderNormals);
      ImGui::Checkbox("Disable Lighting", &mViewer.mDisableLighting);
      if (ImGui::Button("Reset Light"))
      {
         mViewer.mLightPos = slm::vec3(0.0f, -2.0f, 2.0f);
         mViewer.mLightColor = slm::vec4(1.0f);
         mViewer.mDirectionalLight = true;
         mViewer.mLightFollowsCamera = true;
         mViewer.mScaleLightByShapeRadius = true;
         mViewer.mLightRadiusScale = 1.0f;
         mViewer.mAppliedLightRange = 0.0f;
      }
      ImGui::End();

      if (mRenderMaterials)
      {
         ImGui::Begin("Materials");
         if (mViewer.mInterior == NULL || mViewer.mMaterialList == NULL)
         {
            ImGui::TextUnformatted("No interior loaded.");
            ImGui::End();
         }
         else
         {
            ImGui::Text("Materials: %zu  Surfaces: %zu",
                        mViewer.mMaterialList->mMaterials.size(),
                        mViewer.mInterior->surface.size());
            ImGui::Separator();
            for (size_t i = 0; i < mViewer.mMaterialList->mMaterials.size(); ++i)
            {
               const MaterialList::Material& mat = mViewer.mMaterialList->mMaterials[i];
               const GenericViewer::LoadedTexture* tex = i < mViewer.mActiveMaterials.size() ? &mViewer.mActiveMaterials[i].tex : NULL;
               ImGui::Text("%03zu  %s  tex=%d", i, mat.name.empty() ? "<blank>" : mat.name.c_str(), tex ? tex->texID : -1);
            }
            ImGui::End();
         }
      }
   }
};

#endif // TORQUEVIEWER_INTERIORVIEWER_H
