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

   struct RenderableBlock
   {
      std::string label;
      DIF::Interior* interior;
      MaterialList materials;
      std::vector<GenericViewer::ActiveMaterial> activeMaterials;
      std::vector<InteriorPart> parts;
      ShapeViewer::PackedModelData packed;
      uint32_t modelId;
      bool enabled;
      bool geometryReady;

      RenderableBlock() : interior(NULL), modelId(0), enabled(true), geometryReady(false) {;}
   };

   DIF::DIF mDIF;
   DIF::Interior* mInterior;
   MaterialList mInteriorMaterials;
   std::vector<InteriorPart> mParts;
   ShapeViewer::PackedModelData mPacked;
   bool mGeometryReady;
   bool mDebugRenderNormals;
   bool mDisableLighting;
   std::vector<RenderableBlock> mBlocks;
   int mSelectedBlockIdx;
   int mSelectedMaterialIdx;

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
      mSelectedBlockIdx = -1;
      mSelectedMaterialIdx = 0;
   }

   ~InteriorViewer()
   {
      clear();
   }

   void clear()
   {
      for (RenderableBlock& block : mBlocks)
      {
         if (block.geometryReady)
            GFXClearModelData(block.modelId);
      }
      GFXClearModelData(0);
      mBlocks.clear();
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
      mActiveMaterials.clear();
      mResourceFilename.clear();
      mResourceMount = -1;
      mSelectedBlockIdx = -1;
      mSelectedMaterialIdx = 0;
      clearTextures();
      mAppliedLightPos = slm::vec3(0.0f, -2.0f, 2.0f);
      mAppliedLightRange = 0.0f;
   }

   RenderableBlock* getSelectedBlock()
   {
      if (mSelectedBlockIdx < 0 || mSelectedBlockIdx >= (int)mBlocks.size())
         return NULL;
      return &mBlocks[mSelectedBlockIdx];
   }

   const RenderableBlock* getSelectedBlock() const
   {
      if (mSelectedBlockIdx < 0 || mSelectedBlockIdx >= (int)mBlocks.size())
         return NULL;
      return &mBlocks[mSelectedBlockIdx];
   }

   void syncSelectedBlockMaterialState()
   {
      RenderableBlock* block = getSelectedBlock();
      if (!block)
      {
         mMaterialList = &mInteriorMaterials;
         mActiveMaterials.clear();
         mSelectedMaterialIdx = 0;
         return;
      }

      mMaterialList = &block->materials;
      mActiveMaterials = block->activeMaterials;
      if (mMaterialList->mMaterials.empty())
         mSelectedMaterialIdx = 0;
      else
         mSelectedMaterialIdx = std::clamp(mSelectedMaterialIdx, 0, (int)mMaterialList->mMaterials.size() - 1);
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

   void buildPackedGeometry(RenderableBlock& block)
   {
      block.parts.clear();
      block.packed.verts.clear();
      block.packed.texVerts.clear();
      block.packed.indices.clear();

      if (!block.interior)
         return;

      const size_t materialCount = block.materials.mMaterials.size();
      if (materialCount == 0)
         return;

      for (size_t surfIdx = 0; surfIdx < block.interior->surface.size(); ++surfIdx)
      {
         const DIF::Interior::Surface& surface = block.interior->surface[surfIdx];
         if (surface.textureIndex >= materialCount)
            continue;
         if (surface.planeIndex >= block.interior->plane.size() ||
             surface.texGenIndex >= block.interior->texGenEq.size())
            continue;
         if (surface.windingCount < 3)
            continue;
         if (surface.windingStart + surface.windingCount > block.interior->index.size())
            continue;

         const DIF::Interior::Plane& plane = block.interior->plane[surface.planeIndex];
         if (plane.normalIndex >= block.interior->normal.size())
            continue;

         slm::vec3 normal = interiorSwizzle(block.interior->normal[plane.normalIndex]);
         if (surface.planeFlipped)
            normal *= -1.0f;

         const DIF::Interior::TexGenEq& texGenEq = block.interior->texGenEq[surface.texGenIndex];
         const uint32_t firstIndex = (uint32_t)block.packed.indices.size();
         const uint32_t firstVertex = (uint32_t)block.packed.verts.size();
         uint32_t triangleCount = 0;

         for (uint32_t j = surface.windingStart + 2; j < surface.windingStart + surface.windingCount; ++j)
         {
            const bool flipWinding = ((j - (surface.windingStart + 2)) % 2) != 0;
            const uint32_t i0 = block.interior->index[j - 2];
            const uint32_t i1 = block.interior->index[j - 1];
            const uint32_t i2 = block.interior->index[j - 0];

            if (i0 >= block.interior->point.size() ||
                i1 >= block.interior->point.size() ||
                i2 >= block.interior->point.size())
               continue;

            const glm::vec3 p0 = block.interior->point[flipWinding ? i2 : i0];
            const glm::vec3 p1 = block.interior->point[i1];
            const glm::vec3 p2 = block.interior->point[flipWinding ? i0 : i2];

            const slm::vec3 v0 = interiorSwizzle(p0);
            const slm::vec3 v1 = interiorSwizzle(p1);
            const slm::vec3 v2 = interiorSwizzle(p2);

            const slm::vec2 uv0(interiorPlaneDistance(texGenEq.planeX, slm::vec3(p0.x, p0.y, p0.z)), interiorPlaneDistance(texGenEq.planeY, slm::vec3(p0.x, p0.y, p0.z)));
            const slm::vec2 uv1(interiorPlaneDistance(texGenEq.planeX, slm::vec3(p1.x, p1.y, p1.z)), interiorPlaneDistance(texGenEq.planeY, slm::vec3(p1.x, p1.y, p1.z)));
            const slm::vec2 uv2(interiorPlaneDistance(texGenEq.planeX, slm::vec3(p2.x, p2.y, p2.z)), interiorPlaneDistance(texGenEq.planeY, slm::vec3(p2.x, p2.y, p2.z)));

            if (block.packed.verts.size() + 3 >= 65535)
               break;

            block.packed.verts.push_back({v0, normal});
            block.packed.verts.push_back({v1, normal});
            block.packed.verts.push_back({v2, normal});

            block.packed.texVerts.push_back({uv0});
            block.packed.texVerts.push_back({uv1});
            block.packed.texVerts.push_back({uv2});

            block.packed.indices.push_back((uint16_t)(firstVertex + triangleCount * 3 + 0));
            block.packed.indices.push_back((uint16_t)(firstVertex + triangleCount * 3 + 1));
            block.packed.indices.push_back((uint16_t)(firstVertex + triangleCount * 3 + 2));
            triangleCount++;
         }

         if (triangleCount > 0)
         {
            InteriorPart part;
            part.materialIndex = (uint32_t)surface.textureIndex;
            part.firstIndex = firstIndex;
            part.indexCount = triangleCount * 3;
            block.parts.push_back(part);
         }
      }
   }

   bool buildRenderableBlock(RenderableBlock& block, uint32_t modelId)
   {
      block.modelId = modelId;
      block.geometryReady = false;

      if (!block.interior)
         return false;

      block.materials.free();
      for (const std::string& materialName : block.interior->materialName)
         block.materials.push_back(materialName.c_str());
      if (block.materials.mMaterials.empty())
         block.materials.push_back("");

      mMaterialList = &block.materials;
      initMaterials();
      block.activeMaterials = mActiveMaterials;

      buildPackedGeometry(block);
      if (block.packed.verts.empty() || block.packed.indices.empty())
      {
         GFXClearModelData(modelId);
         return false;
      }

      GFXLoadModelData(modelId,
                       block.packed.verts.empty() ? NULL : &block.packed.verts[0],
                       block.packed.texVerts.empty() ? NULL : &block.packed.texVerts[0],
                       block.packed.indices.empty() ? NULL : &block.packed.indices[0],
                       NULL,
                       (uint32_t)block.packed.verts.size(),
                       (uint32_t)block.packed.texVerts.size(),
                       (uint32_t)block.packed.indices.size(),
                       0);

      block.geometryReady = true;
      return true;
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
      mBlocks.reserve(mDIF.interior.size() + mDIF.subObject.size());

      for (size_t i = 0; i < mDIF.interior.size(); ++i)
      {
         RenderableBlock block;
         block.interior = &mDIF.interior[i];
         block.label = (i == 0) ? "Main Interior" : ("Interior " + std::to_string(i));
         buildRenderableBlock(block, (uint32_t)mBlocks.size());
         mBlocks.push_back(std::move(block));
      }

      for (size_t i = 0; i < mDIF.subObject.size(); ++i)
      {
         RenderableBlock block;
         block.interior = &mDIF.subObject[i];
         block.label = "SubObject " + std::to_string(i);
         buildRenderableBlock(block, (uint32_t)mBlocks.size());
         mBlocks.push_back(std::move(block));
      }

      bool anyRenderable = false;
      for (const RenderableBlock& block : mBlocks)
      {
         if (block.geometryReady && !block.parts.empty())
         {
            anyRenderable = true;
            break;
         }
      }

      if (!anyRenderable)
      {
         fprintf(stderr, "interior '%s' produced no renderable geometry\n", filename ? filename : "<null>");
         clear();
         return false;
      }

      mSelectedBlockIdx = 0;
      while (mSelectedBlockIdx < (int)mBlocks.size() && (!mBlocks[mSelectedBlockIdx].geometryReady || mBlocks[mSelectedBlockIdx].parts.empty()))
         ++mSelectedBlockIdx;
      if (mSelectedBlockIdx >= (int)mBlocks.size())
         mSelectedBlockIdx = 0;
      syncSelectedBlockMaterialState();

      mGeometryReady = true;
      mLightPos = slm::vec3(0.0f, -2.0f, 2.0f);
      mLightColor = slm::vec4(1.0f);
      mAppliedLightPos = mLightPos;
      mAppliedLightRange = 0.0f;
      return true;
   }

   void render()
   {
      if (!mGeometryReady || mInterior == NULL || mBlocks.empty())
         return;

      updateMVP();
      for (const RenderableBlock& block : mBlocks)
      {
         if (!block.enabled || !block.geometryReady || block.parts.empty())
            continue;

         GFXSetModelVerts(block.modelId, 0, 0, 0, 0);

         for (const InteriorPart& part : block.parts)
         {
            if (part.materialIndex >= block.materials.mMaterials.size())
               continue;

            const MaterialList::Material& mat = block.materials.mMaterials[part.materialIndex];
            const uint32_t materialFlags = mat.tsProps.flags;
            const uint32_t textureGroupID = part.materialIndex < block.activeMaterials.size()
               ? block.activeMaterials[part.materialIndex].texGroupID
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

            GFXDrawModelPrims((uint32_t)block.packed.verts.size(), part.indexCount, part.firstIndex, 0);
         }
      }
   }

   void reloadMaterials()
   {
      clearTextures();
      for (RenderableBlock& block : mBlocks)
      {
         mMaterialList = &block.materials;
         initMaterials();
         block.activeMaterials = mActiveMaterials;
      }
      syncSelectedBlockMaterialState();
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
   int mSelectedMaterialIdx;

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
      mSelectedMaterialIdx = 0;
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
      mSelectedMaterialIdx = 0;
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

      ImGui::Begin("Blocks");
      if (mViewer.mBlocks.empty())
      {
         ImGui::TextUnformatted("No interior blocks loaded.");
      }
      else
      {
         ImGui::Text("Blocks: %zu", mViewer.mBlocks.size());
         ImGui::Separator();
         for (int i = 0; i < (int)mViewer.mBlocks.size(); ++i)
         {
            InteriorViewer::RenderableBlock& block = mViewer.mBlocks[i];
            bool enabled = block.enabled;
            char enabledId[64];
            snprintf(enabledId, sizeof(enabledId), "##block_enabled_%d", i);
            if (ImGui::Checkbox(enabledId, &enabled))
               block.enabled = enabled;

            ImGui::SameLine();
            const bool selected = (i == mViewer.mSelectedBlockIdx);
            char blockId[128];
            snprintf(blockId, sizeof(blockId), "%s##block_sel_%d", block.label.c_str(), i);
            if (ImGui::Selectable(blockId, selected))
            {
               mViewer.mSelectedBlockIdx = i;
               mViewer.syncSelectedBlockMaterialState();
               mSelectedMaterialIdx = 0;
            }
            if (selected)
               ImGui::SetItemDefaultFocus();
         }
      }
      ImGui::End();

      if (mRenderMaterials)
      {
         ImGui::Begin("Materials");
         InteriorViewer::RenderableBlock* selectedBlock = mViewer.getSelectedBlock();
         if (mViewer.mInterior == NULL || selectedBlock == NULL)
         {
            ImGui::TextUnformatted("No interior loaded.");
            ImGui::End();
         }
         else
         {
            const int materialCount = (int)selectedBlock->materials.mMaterials.size();
            if (materialCount <= 0)
            {
               ImGui::TextUnformatted("No interior materials.");
               ImGui::End();
               return;
            }

            mSelectedMaterialIdx = std::clamp(mSelectedMaterialIdx, 0, materialCount - 1);

            ImGui::Text("Block: %s", selectedBlock->label.c_str());
            ImGui::Text("Total: %d  Surfaces: %zu",
                        materialCount,
                        selectedBlock->interior ? selectedBlock->interior->surface.size() : 0);
            ImGui::Separator();
            ImGui::Columns(2, "interior_materials_columns", true);

            if (ImGui::BeginListBox("##interior_material_list", ImVec2(-FLT_MIN, 320.0f)))
            {
               for (int i = 0; i < materialCount; ++i)
               {
                  const MaterialList::Material& mat = selectedBlock->materials.mMaterials[i];
                  const bool selected = i == mSelectedMaterialIdx;
                  const char* matName = mat.name.empty() ? "<blank>" : mat.name.c_str();
                  char entryLabel[1024];
                  snprintf(entryLabel, sizeof(entryLabel), "%s##imat_%d", matName, i);
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

            const MaterialList::Material& mat = selectedBlock->materials.mMaterials[mSelectedMaterialIdx];
            const GenericViewer::ActiveMaterial* activeMat = mSelectedMaterialIdx < (int)selectedBlock->activeMaterials.size() ? &selectedBlock->activeMaterials[mSelectedMaterialIdx] : NULL;
            const GenericViewer::LoadedTexture* tex = activeMat ? &activeMat->tex : NULL;
            void* textureHandle = tex ? GFXGetTextureViewHandle(tex->texID) : NULL;

            ImGui::Text("Index: %d", mSelectedMaterialIdx);
            ImGui::TextWrapped("Name: %s", mat.name.empty() ? "<blank>" : mat.name.c_str());
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
      }
   }
};

#endif // TORQUEVIEWER_INTERIORVIEWER_H
