
// NOTE: Shape IO in torque gets very convoluted since
// there are LOTS of versions it can support, plus it relies on
// weirdly splitting the stream up by element size.
//
// For debugging a simpler IO method is also provided.

namespace Dts3
{

struct IO
{
   enum
   {
      EXPORTER_VERSION = 2
   };
   
   template<typename T> static bool readShape(Shape* shape, T& ds)
   {
      // Reading sequences
      uint32_t numSequences = 0;
      ds.getBaseStream()->read(numSequences);
      
      shape->mSequences.resize(numSequences);
      for (Sequence& seq : shape->mSequences)
      {
         seq.read(*ds.getBaseStream(), ds.getVersion());
      }
      
      // Reading material list
      shape->mMaterials.mVariant = MaterialList::VARIANT_TS;
      shape->mMaterials.read(*ds.getBaseStream());
      
      // Reading various counts
      uint32_t numNodes = 0;
      uint32_t numObjects = 0;
      uint32_t numDecals = 0;
      uint32_t numSubShapes = 0;
      uint32_t numIflMaterials = 0;
      uint32_t numNodeRots = 0;
      uint32_t numNodeTrans = 0;
      uint32_t numNodeUniformScales = 0;
      uint32_t numNodeAlignedScales = 0;
      uint32_t numNodeArbitraryScales = 0;
      uint32_t numGroundFrames = 0;
      uint32_t numObjectStates = 0;
      uint32_t numDecalStates = 0;
      uint32_t numTriggers = 0;
      uint32_t numDetails = 0;
      uint32_t numMeshes = 0;
      uint32_t numNames = 0;
      uint32_t numSkins = 0;
      
      
      ds.read(numNodes);
      ds.read(numObjects);
      ds.read(numDecals);
      
      ds.read(numSubShapes);
      
      ds.read(numIflMaterials);
      
      if (ds.getVersion() >= 22)
      {
         ds.read(numNodeRots);
         ds.read(numNodeTrans);
         ds.read(numNodeUniformScales);
         ds.read(numNodeAlignedScales);
         ds.read(numNodeArbitraryScales);
      }
      else
      {
         // node counts are different
         uint32_t sz = 0;
         ds.read(sz);
         numNodeRots = numNodeTrans = sz - numNodes;
      }
      
      if (ds.getVersion() > 23)
      {
         ds.read(numGroundFrames); // < 23 has no ground frames
      }
      
      ds.read(numObjectStates);
      
      ds.read(numDecalStates);
      
      ds.read(numTriggers);
      
      ds.read(numDetails);
      
      ds.read(numMeshes);
      if (ds.getVersion() < 23)
      {
         ds.read(numSkins); // skins arranged differently <23
      }
      ds.read(numNames);
      
      ds.read(shape->mSmallestVisibleSize); // Not a float
      ds.read(shape->mSmallestVisibleDetailLevel);
      
      ds.readCheck();
      
      // Reading bounds
      ds.read(shape->mRadius);
      ds.read(shape->mTubeRadius);
      readPoint3F(ds, shape->mCenter);
      readBox(ds, shape->mBounds);
      
      ds.readCheck();
      
      // Reading nodes
      shape->mNodes.resize(numNodes);
      for (Node& n : shape->mNodes)
      {
         readNode(ds, n);
      }
      
      ds.readCheck();
      
      // Reading objects
      shape->mObjects.resize(numObjects);
      for (Object& n : shape->mObjects)
      {
         readObject(ds, n);
      }
      
      ds.readCheck();
      
      // Reading decals
      shape->mDecals.resize(numDecals);
      for (Decal& n : shape->mDecals)
      {
         readDecal(ds, n);
      }
      
      ds.readCheck();
      
      // Reading IflMaterials
      shape->mIflMaterials.resize(numIflMaterials);
      for (IflMaterial& n : shape->mIflMaterials)
      {
         readIflMaterial(ds, n);
      }
      
      ds.readCheck();
      
      // Reading subShapes
      shape->mSubshapes.resize(numSubShapes);
      
      for (SubShape& s : shape->mSubshapes)
      {
         ds.read(s.firstNode);
      }
      for (SubShape& s : shape->mSubshapes)
      {
         ds.read(s.firstObject);
      }
      for (SubShape& s : shape->mSubshapes)
      {
         ds.read(s.firstDecal);
      }
      
      ds.readCheck();
      
      for (SubShape& s : shape->mSubshapes)
      {
         ds.read(s.numNodes);
         ds.read(s.numObjects);
         ds.read(s.numDecals);
         s.firstTranslucent = -1;
      }
      
      ds.readCheck();
      
      // NOTE: firstTranslucent isn't stored in file
      
      // Mesh index list for old shapes
      std::vector<int32_t> meshIndexList;
      if (ds.getVersion() < 16)
      {
         uint32_t sz = 0;
         ds.read(sz);
         ds.read32(sz, &meshIndexList[0]);
      }
      
      // Reading default translations and rotations
      shape->mDefaultRotations.resize(numNodes);
      shape->mDefaultTranslations.resize(numNodes);
      for (uint32_t i = 0; i < numNodes; ++i)
      {
         IO::readQuat16(ds, shape->mDefaultRotations[i]);
         IO::readPoint3F(ds, shape->mDefaultTranslations[i]);
      }
      
      // Reading node sequence data
      shape->mNodeTranslations.resize(numNodeTrans);
      shape->mNodeRotations.resize(numNodeRots);
      for (slm::vec3& point : shape->mNodeTranslations)
      {
         IO::readPoint3F(ds, point);
      }
      for (Quat16& quat : shape->mNodeRotations)
      {
         IO::readQuat16(ds, quat);
      }
      
      ds.readCheck();
      
      // Reading more sequence data (scales)
      shape->mNodeUniformScales.resize(numNodeUniformScales);
      shape->mNodeAlignedScales.resize(numNodeAlignedScales);
      shape->mNodeArbitraryScaleFactors.resize(numNodeArbitraryScales);
      shape->mNodeArbitraryScaleRotations.resize(numNodeArbitraryScales);
      
      if (ds.getVersion() > 21)
      {
         for (float& scale : shape->mNodeUniformScales)
         {
            ds.read(scale);
         }
         for (slm::vec3& scale : shape->mNodeAlignedScales)
         {
            ds.read(scale);
         }
         for (uint32_t i = 0; i < numNodeArbitraryScales; ++i)
         {
            IO::readPoint3F(ds, shape->mNodeArbitraryScaleFactors[i]);
            IO::readQuat16(ds, shape->mNodeArbitraryScaleRotations[i]);
         }
      }
      
      ds.readCheck();
      
      // Reading ground frames
      shape->mGroundTranslations.resize(numGroundFrames);
      shape->mGroundRotations.resize(numGroundFrames);
      for (int i = 0; i < numGroundFrames; ++i)
      {
         IO::readPoint3F(ds, shape->mGroundTranslations[i]);
         IO::readQuat16(ds, shape->mGroundRotations[i]);
      }
      
      ds.readCheck();
      
      // Reading object states
      shape->mObjectStates.resize(numObjectStates);
      for (ObjectState& n : shape->mObjectStates)
      {
         readObjectState(ds, n);
      }
      
      ds.readCheck();
      
      // Reading decal states
      shape->mDecalStates.resize(numDecalStates);
      for (DecalState& n : shape->mDecalStates)
      {
         readDecalState(ds, n);
      }
      
      ds.readCheck();
      
      // Reading triggers
      shape->mTriggers.resize(numTriggers);
      for (Trigger& n : shape->mTriggers)
      {
         readTrigger(ds, n);
      }
      
      ds.readCheck();
      
      // Reading detail levels
      shape->mDetailLevels.resize(numDetails);
      for (DetailLevel& n : shape->mDetailLevels)
      {
         readDetailLevel(ds, n);
      }
      
      ds.readCheck();
      
      // Reading meshes
      // NOTE: to simplify things, we ignore skipping.
      uint32_t totalMeshes = numMeshes + numSkins; // should just be numMeshes in version <23
      
      if (ds.getVersion() > 15)
      {
         shape->mMeshes.resize(numMeshes);
         for (Mesh& m : shape->mMeshes)
         {
            ds.read(m.mType);
            bool didRead = readMesh(&m, shape, ds);
         }
      }
      else
      {
         shape->mMeshes.resize(numMeshes);
         
         // Use mesh index list
         for (uint32_t i=0; i<meshIndexList.size(); i++)
         {
            int32_t meshIndex = meshIndexList[i];
            if (meshIndex >= 0)
            {
               Mesh& m = shape->mMeshes[i];
               ds.read(m.mType);
               bool didRead = readMesh(&m, shape, ds);
            }
            else
            {
               // No mesh
               shape->mMeshes[i] = Mesh();
            }
         }
         assert(false);
      }
      
      ds.readCheck();
      
      // Reading names
      for (int i = 0; i < numNames; i++)
      {
         std::string str;
         ds.readNullString(str);
         shape->mNameTable.addString(str);
      }
      
      ds.readCheck();
      
      if (ds.getVersion() < 23)
      {
         // Skinned mesh counts are stored here
         std::vector<int32_t> detailFirstSkin(numDetails);
         std::vector<int32_t> detailNumSkins(numSkins);
         
         for (int32_t& value : detailFirstSkin)
         {
            ds.read(value);
         }
         for (int32_t& value : detailNumSkins)
         {
            ds.read(value);
         }
         
         ds.readCheck();
         
         for (uint32_t i=0; i<numSkins; i++)
         {
            Mesh& m = shape->mMeshes[numMeshes + i];
            m.mType = Mesh::T_Skin;
            bool didRead = readMesh(&m, shape, ds);
         }
         
         ds.readCheck();
         
         correctPreV32Skins(shape, detailFirstSkin, detailNumSkins, numMeshes, numSkins, numDetails);
         
         assert(false);
      }
      
      shape->mPreviousMerge.clear();
      for (int i = 0; i < numObjects; ++i)
      {
         shape->mPreviousMerge.push_back(-1);
      }
      
      shape->mExportMerge = ds.getVersion() >= 23;
      return true;
   }
   
   static void correctPreV32Skins(Shape* shape, const std::vector<int32_t>& detailFirstSkin, const std::vector<int32_t>& detailNumSkins, uint32_t numMeshes, uint32_t numSkins, uint32_t numDetails)
   {
      if (numSkins == 0 || numDetails == 0 ||
          detailFirstSkin.size() < numDetails || detailNumSkins.size() < numDetails)
      {
         return;
      }
      
      auto& meshes       = shape->mMeshes;
      auto& objects      = shape->mObjects;
      auto& objectStates = shape->mObjectStates;
      auto& subshapes    = shape->mSubshapes;
      auto& sequences    = shape->mSequences;

      // Count present skins in the tail.
      uint32_t present = 0;
      for (uint32_t i = 0; i < numSkins; ++i)
      {
         present += meshes[numMeshes + i].mType != Mesh::T_Null;
      }
      
      if (present == 0)
      {
         return;
      }

      const size_t oldNumObjects = objects.size();

      std::vector<Mesh> skinsCopy;
      skinsCopy.reserve(numSkins);

      uint32_t skinsUsed = 0;
      uint32_t numSkinObjects = 0;

      auto takeSkin = [&](uint32_t skinIdx) -> bool
      {
         Mesh& src = meshes[numMeshes + skinIdx];
         if (src.mType == Mesh::T_Null)
         {
            return false;
         }
         
         skinsCopy.push_back(std::move(src));
         
         // Leave a definite NULL behind.
         src.mType = Mesh::T_Null;
         src.mFlags = 0;
         src.mData.reset();

         ++skinsUsed;
         return true;
      };

      while (skinsUsed < present)
      {
         Object obj{};
         obj.name        = 0;   // no name
         obj.node        = -1;
         obj.nextSibling = -1;
         obj.firstDecal  = -1;

         obj.firstMesh = int(numMeshes + skinsCopy.size());
         obj.numMeshes = 0;

         for (uint32_t dl = 0; dl < numDetails; ++dl)
         {
            // NOTE: These numbers are basically relative to the skin mesh list
            bool found = false;
            const int32_t first = detailFirstSkin[dl];
            const int32_t cnt   = detailNumSkins[dl];

            if (first >= 0 && cnt > 0)
            {
               const int32_t end = std::min<int32_t>(first + cnt, (int32_t)numSkins);
               for (int32_t i = first; i < end; ++i)
               {
                  if (takeSkin((uint32_t)i))
                  {
                     found = true;
                     ++obj.numMeshes;
                     break;
                  }
               }
            }

            if (!found)
            {
               skinsCopy.emplace_back(Mesh::T_Null); // placeholder for this detail
               ++obj.numMeshes;
            }
         }

         // Trim trailing null placeholders.
         while (!skinsCopy.empty() && skinsCopy.back().mType == Mesh::T_Null)
         {
            skinsCopy.pop_back();
            --obj.numMeshes;
         }
         // Only add object if we have meshes
         if (obj.numMeshes > 0)
         {
            objects.push_back(obj);
            ++numSkinObjects;
         }
      }

      // Write back the repacked skins into the tail, then shrink away any unused remainder.
      const size_t newSize = numMeshes + skinsCopy.size();
      for (size_t i = 0; i < skinsCopy.size(); ++i)
      {
         meshes[numMeshes + i] = std::move(skinsCopy[i]);
      }
      
      meshes.resize(newSize);

      // If only one subshape, keep parity with old behavior.
      if (subshapes.size() == 1)
      {
         subshapes[0].numObjects += int(numSkinObjects);
      }
      
      // Insert default base states for the new objects, and shift sequence state blocks.
      if (numSkinObjects)
      {
         const size_t insertAt = oldNumObjects;
         const uint32_t n = numSkinObjects;

         const size_t oldSize = objectStates.size();
         objectStates.resize(oldSize + n);

         std::memmove(objectStates.data() + insertAt + n,
                      objectStates.data() + insertAt,
                      (oldSize - insertAt) * sizeof(ObjectState));

         for (size_t i = 0; i < n; ++i)
         {
            objectStates[insertAt + i] = ObjectState(1.0f, 0, 0);
         }
         
         for (auto& seq : sequences)
         {
            seq.baseObjectState += int(n);
         }
      }
   }
   
   template<typename T> static bool writeShape(Shape* shape, T& ds, uint32_t version)
   {
      return false;
   }
   
   template<typename T> static bool readString(T& ds, std::string& str)
   {
      uint8_t len = 0;
      ds.read(len);
      
      char buffer[257];
      ds.read8(len, buffer);
      buffer[len] = 0;
      
      str = buffer;
      return true;
   }
   
   template<typename T> static bool writeString(T& ds, const std::string& str)
   {
      return false;
   }
   
   template<typename T> static bool readBox(T& ds, Box& box)
   {
      readPoint3F(ds, box.min);
      readPoint3F(ds, box.max);
      return true;
   }
   
   template<typename T> static bool writeBox(T& ds, const Box& box)
   {
      return false;
   }
   
   template<typename T> static bool readPoint2F(T& ds, slm::vec2& box)
   {
      ds.read(box.x);
      ds.read(box.y);
      return true;
   }
   
   template<typename T> static bool writePoint2F(T& ds, const slm::vec2& box)
   {
      return false;
   }
   
   template<typename T> static bool readPoint3F(T& ds, slm::vec3& box)
   {
      ds.read(box.x);
      ds.read(box.y);
      ds.read(box.z);
      return true;
   }
   
   template<typename T> static bool writePoint3F(T& ds, const slm::vec3& box)
   {
      return false;
   }
   
   template<typename T> static bool readQuat16(T& ds, Quat16& box)
   {
      ds.read(box.x);
      ds.read(box.y);
      ds.read(box.z);
      ds.read(box.w);
      return true;
   }
   
   template<typename T> static bool writeQuat16(T& ds, Quat16& box)
   {
      return false;
   }
   
   template<typename T> static bool writePoint4F(T& ds, const slm::vec4& box)
   {
      return false;
   }
   
   template<typename T> static bool readPoint4F(T& ds, slm::vec4& box)
   {
      ds.read(box.x);
      ds.read(box.y);
      ds.read(box.z);
      ds.read(box.w);
      return true;
   }
   
   template<typename T> static bool readMatrixF(T& ds, slm::mat4& box)
   {
      ds.read32(sizeof(box) / 4, &box);
      return true;
   }
   
   template<typename T> static bool writeMatrixF(T& ds, slm::mat4& box)
   {
      return false;
   }
   
   template<typename T> static bool readPrimitive(T& ds, Primitive& box)
   {
      ds.read(box.firstElement);
      ds.read(box.numElements);
      ds.read(box.matIndex);
      return true;
   }
   
   template<typename T> static bool writePrimitive(T& ds, const Primitive& box)
   {
      return false;
   }
   
   template<typename T> static bool readCluster(T& ds, Cluster& box)
   {
      ds.read(box.startPrimitive);
      ds.read(box.endPrimitive);
      ds.read(box.normal.x);
      ds.read(box.normal.y);
      ds.read(box.normal.z);
      ds.read(box.k);
      ds.read(box.frontCluster);
      ds.read(box.backCluster);
      return true;
   }
   
   template<typename T> static bool writeCluster(T& ds, const Cluster& box)
   {
      return false;
   }
   
   template<typename T> static bool readNode(T& ds, Node& box)
   {
      ds.read(box.name);
      ds.read(box.parent);
      ds.read(box.firstObject);
      ds.read(box.firstChild);
      ds.read(box.nextSibling);
      return true;
   }
   
   template<typename T> static bool writeNode(T& ds, const Node& box)
   {
      return false;
   }
   
   template<typename T> static bool readObject(T& ds, Object& box)
   {
      ds.read(box.name);
      ds.read(box.numMeshes);
      ds.read(box.firstMesh);
      ds.read(box.node);
      ds.read(box.nextSibling);
      ds.read(box.firstDecal);
      return false;
   }
   
   template<typename T> static bool writeObject(T& ds, const Object& box)
   {
      return false;
   }
   
   template<typename T> static bool readObjectState(T& ds, ObjectState& box)
   {
      ds.read(box.vis);
      ds.read(box.frame);
      ds.read(box.matFrame);
      return true;
   }
   
   template<typename T> static bool writeObjectState(T& ds, const ObjectState& box)
   {
      return false;
   }
   
   template<typename T> static bool readIflMaterial(T& ds, IflMaterial& box)
   {
      ds.read(box.name);
      ds.read(box.slot);
      ds.read(box.firstFrame);
      ds.read(box.time);
      ds.read(box.numFrames);
      return true;
   }
   
   template<typename T> static bool writeIflMaterial(T& ds, const IflMaterial& box)
   {
      return false;
   }
   
   template<typename T> static bool readDecal(T& ds, Decal& box)
   {
      return false;
   }
   
   template<typename T> static bool writeDecal(T& ds, const Decal& box)
   {
      ds.read(box.name);
      ds.read(box.numMeshes);
      ds.read(box.firstMesh);
      ds.read(box.object);
      ds.read(box.nextSibling);
      return true;
   }
   
   template<typename T> static bool readDecalState(T& ds, DecalState& box)
   {
      ds.read(box.frame);
      return true;
   }
   
   template<typename T> static bool writeDecalState(T& ds, const DecalState& box)
   {
      return false;
   }
   
   template<typename T> static bool readTrigger(T& ds, Trigger& box)
   {
      ds.read(box.state);
      ds.read(box.pos);
      return true;
   }
   
   template<typename T> static bool writeTrigger(T& ds, const Trigger& box)
   {
      return false;
   }
   
   template<typename T> static bool readDetailLevel(T& ds, DetailLevel& box)
   {
      ds.read(box.name);
      ds.read(box.subshape);
      ds.read(box.objectDetail);
      ds.read(box.size);
      ds.read(box.avgError);
      ds.read(box.maxError);
      ds.read(box.polyCount);
      return true;
   }
   
   template<typename T> static bool writeDetailLevel(T& ds, const DetailLevel& box)
   {
      return false;
   }
   
   bool readSplit(MemRStream& stream, Shape* shape);
   bool writeSplit(MemRStream& write, Shape* shape, uint32_t version=DefaultVersion);
   
   template<typename T> static bool readMesh(Mesh* mesh, Shape* shape, T& ds)
   {
      uint32_t sz = 0;
      int32_t ssz = 0;
      
      mesh->mData = NULL;
      
      if (mesh->mType == Mesh::T_Null)
      {
         return false;
      }
      
      BasicData* basicData = NULL;
      SkinData* skinData = NULL;
      
      if (mesh->mType == Mesh::T_Skin)
      {
         mesh->mData = std::make_shared<SkinData>();
         skinData = (SkinData*)mesh->mData.get();
         basicData = (BasicData*)mesh->mData.get();
      }
      else if (mesh->mType != Mesh::T_Decal)
      {
         mesh->mData = std::make_shared<BasicData>();
         basicData = (BasicData*)mesh->mData.get();
      }
      
      if (basicData)
      {
         ds.readCheck();
         ds.read(mesh->mNumFrames);
         ds.read(mesh->mNumMatFrames);
         ds.read(mesh->mParent);
         IO::readBox(ds, mesh->mBounds);
         IO::readPoint3F(ds, mesh->mCenter);
         ds.read(mesh->mRadius);
         
         if (mesh->mParent < 0)
         {
            ds.read(sz);
            basicData->verts.resize(sz);
            for (slm::vec3& vert : basicData->verts)
            {
               IO::readPoint3F(ds, vert);
            }
         }
         else
         {
            ds.read(sz); // TODO: important?
         }
         
         if (mesh->mParent < 0)
         {
            ds.read(sz);
            basicData->tverts.resize(sz);
            for (slm::vec2& vert : basicData->tverts)
            {
               IO::readPoint2F(ds, vert);
            }
         }
         else
         {
            ds.read(sz); // TODO: important?
         }
         
         if (mesh->mParent < 0)
         {
            basicData->normals.resize(basicData->verts.size());
            for (slm::vec3& vert : basicData->normals)
            {
               IO::readPoint3F(ds, vert);
            }
            
            if (ds.getVersion() > 21)
            {
               for (slm::vec3& vert : basicData->normals)
               {
                  uint8_t val = 0;
                  ds.read(val); // TODO: important?
               }
            }
         }
         
         ds.read(sz);
         basicData->primitives.resize(sz);
         
         for (Primitive& prim : basicData->primitives)
         {
            IO::readPrimitive(ds, prim);
         }
         
         ds.read(sz);
         basicData->indices.resize(sz);
         ds.read16(sz, &basicData->indices[0]);
         
         ds.read(sz);
         basicData->mergeIndices.resize(sz);
         ds.read16(sz, &basicData->mergeIndices[0]);
         
         ds.read(mesh->mVertsPerFrame);
         ds.read(mesh->mFlags);
         ds.readCheck();
         
         mesh->calculateBounds();
      }
      
      if (skinData)
      {
         uint32_t numVerts = basicData->verts.size();
         
         if (mesh->mParent < 0)
         {
            ds.read(sz);
            if (sz != numVerts)
            {
               numVerts = sz;
               basicData->verts.resize(sz);
            }
            
            for (slm::vec3& vert : basicData->verts)
            {
               IO::readPoint3F(ds, vert);
            }
         }
         else
         {
            ds.read(sz);
         }
         
         if (mesh->mParent < 0)
         {
            basicData->normals.resize(basicData->verts.size());
            for (slm::vec3& vert : basicData->normals)
            {
               IO::readPoint3F(ds, vert);
            }
            
            if (ds.getVersion() > 21)
            {
               for (slm::vec3& vert : basicData->normals)
               {
                  uint8_t val = 0;
                  ds.read(val); // TODO: important?
               }
            }
         }
         
         if (mesh->mParent < 0)
         {
            ds.read(sz);
            skinData->nodeTransforms.resize(sz);
            for (slm::mat4& mat : skinData->nodeTransforms)
            {
               IO::readMatrixF(ds, mat);
            }
            
            ds.read(sz);
            skinData->vindex.resize(sz);
            skinData->bindex.resize(sz);
            skinData->vweight.resize(sz);
            ds.read32(sz, &skinData->vindex[0]);
            ds.read32(sz, &skinData->bindex[0]);
            ds.read32(sz, &skinData->vweight[0]);
            
            ds.read(sz);
            ds.read32(sz, &skinData->nodeIndex[0]);
         }
         else
         {
            for (int i = 0; i < 3; ++i) 
            {
               uint32_t val = 0;
               ds.read(val);
            }
         }
         
         ds.readCheck();
      }
      else if (mesh->mType == Mesh::T_Decal)
      {
         mesh->mData = std::make_shared<DecalData>();
         DecalData* decalData = (DecalData*)mesh->mData.get();
         
         ds.read(sz);
         decalData->primitives.resize(sz);
         
         for (Primitive& prim : decalData->primitives)
         {
            IO::readPrimitive(ds, prim);
         }
         
         ds.read(sz);
         basicData->indices.resize(sz);
         ds.read16(sz, &decalData->indices[0]);
         
         ds.read(sz);
         decalData->startPrimitive.resize(sz);
         ds.read32(sz, &decalData->startPrimitive[0]);
         
         ds.read(sz);
         decalData->texGenS.resize(sz);
         
         for (slm::vec4& texGen : decalData->texGenS)
         {
            IO::readPoint4F(ds, texGen);
         }
         
         ds.read(sz);
         decalData->texGenT.resize(sz);
         
         for (slm::vec4& texGen : decalData->texGenT)
         {
            IO::readPoint4F(ds, texGen);
         }
         
         ds.read(decalData->matIndex);
         ds.readCheck();
      }
      else if (mesh->mType == Mesh::T_Sorted)
      {
         mesh->mData = std::make_shared<SortedData>();
         SortedData* sortedData = (SortedData*)mesh->mData.get();
         
         ds.read(sz);
         sortedData->clusters.resize(sz);
         
         for (Cluster& cluster : sortedData->clusters)
         {
            IO::readCluster(ds, cluster);
         }
         
         ds.read(sz);
         sortedData->startCluster.resize(sz);
         ds.read32(sz, &sortedData->startCluster[0]);
         
         ds.read(sz);
         sortedData->firstVerts.resize(sz);
         ds.read32(sz, &sortedData->firstVerts[0]);
         
         ds.read(sz);
         sortedData->numVerts.resize(sz);
         ds.read32(sz, &sortedData->numVerts[0]);
         
         ds.read(sz);
         sortedData->firstTVerts.resize(sz);
         ds.read32(sz, &sortedData->firstTVerts[0]);
         
         ds.read(sortedData->alwaysWriteDepth);
         
         ds.readCheck();
      }
      
      return true;
   }
   
   template<typename T> static bool writeMesh(Mesh* mesh, Shape* shape, T& ds, uint32_t version)
   {
      return false;
   }
};

// NOTE: this is a bit messy

struct BasicStream
{
   MemRStream* baseStream;
   uint16_t version;
   
   inline uint32_t getVersion() const { return version; }
   
   inline MemRStream* getBaseStream()
   {
      return baseStream;
   }
   
   void beginWriteStream(MemRStream& destStream, uint16_t dtsVersion)
   {
      uint32_t hdr[4];
      hdr[0] = 861099076;
      hdr[1] = dtsVersion | (IO::EXPORTER_VERSION << 16);
      hdr[2] = 0;
      hdr[3] = 0;
      version = dtsVersion;
      baseStream = &destStream;
      
      destStream.write(sizeof(hdr), hdr);
   }
   
   bool readHeader(MemRStream& srcStream)
   {
      uint32_t hdr[4] = {};
      srcStream.read(sizeof(hdr), hdr);
      version = hdr[1] & 0xFFFF;
      baseStream = &srcStream;
   }
   
   bool readCheck()
   {
      return true;
   }
   
   void writeCheck()
   {
   }
   
   void readNullString(std::string& value)
   {
      baseStream->readNullString(value);
   }
};

struct SplitStream
{
   enum
   {
      EXPORTER_VERSION=1
   };
   
   MemRStream buffer32;
   MemRStream buffer16;
   MemRStream buffer8;
   MemRStream* baseStream;
   
   uint32_t checkCount;
   uint16_t dtsVersion;
   
   SplitStream() : dtsVersion(0), checkCount(0), baseStream(NULL)
   {
   }
   
   inline uint16_t getVersion() const
   {
      return dtsVersion;
   }
   
   inline MemRStream* getBaseStream()
   {
      return baseStream;
   }
   
   void beginWriteStream(MemRStream& stream, uint16_t writeVersion)
   {
      dtsVersion = writeVersion;
      baseStream = &stream;
   }
   
   void flushToStream(MemRStream& destStream)
   {
      size_t sz8 = buffer8.getPosition();
      size_t sz16 = buffer16.getPosition();
      size_t sz32 = buffer32.getPosition();
      uint32_t blank = 0;
      
      if (sz16 & 0x0001)
      {
         write16(1, &blank);
         sz16++;
      }
      
      while (sz8 & 0x0003)
      {
         write8(1, &blank);
         sz8++;
      }
      
      size_t offset16 = sz32;
      size_t offset8 = offset16 + (sz16/2);
      size_t totalSize = offset8 + (sz8/4);
      
      uint32_t hdr[4];
      hdr[0] = dtsVersion | (EXPORTER_VERSION << 16);
      hdr[1] = static_cast<int32_t>(totalSize);
      hdr[2] = static_cast<int32_t>(offset16);
      hdr[3] = static_cast<int32_t>(offset8);
      
      destStream.write(sizeof(hdr), hdr);
      destStream.write(sz8, buffer32.mPtr);
      destStream.write(sz16, buffer16.mPtr);
      destStream.write(sz32, buffer8.mPtr);
   }
   
   void floodFromStream(MemRStream& sourceStream)
   {
      uint32_t hdr[4];
      sourceStream.read(4 * sizeof(int32_t), hdr);
      dtsVersion = hdr[0] & 0xFF;
      baseStream = &sourceStream;
      
      if (dtsVersion < 19)
      {
         assert(false);
         return;
      }
      
      int32_t totalSize = hdr[1];
      int32_t offset16 = hdr[2];
      int32_t offset8 = hdr[3];
      
      int32_t allocated32 = offset16;
      int32_t allocated16 = (offset8 - offset16);
      int32_t allocated8 = (totalSize - offset8);
      
      uint32_t* start = (uint32_t*)(sourceStream.mPtr + sourceStream.mPos);
      uint8_t* start16 = (uint8_t*)(start + offset16);
      uint8_t* start8 = (uint8_t*)(start + offset8);
      
      buffer32.setOffsetView(sourceStream, 0, allocated32 * sizeof(int32_t));
      buffer16.setOffsetView(sourceStream, start16 - ((uint8_t*)start), allocated16 * sizeof(int32_t));
      buffer8.setOffsetView(sourceStream, start8 - ((uint8_t*)start), allocated8 * sizeof(int32_t));
      
      sourceStream.mPos += totalSize*4;
      
      checkCount = 0;
      return true;
   }
   
   void storeCheck(int32_t checkPoint = -1)
   {
      uint8_t c8 = checkCount % 256;
      uint16_t c16 = checkCount % 65536;
      uint32_t c32 = checkCount % (1ULL << 32);
      
      buffer8.write(c8);
      buffer16.write(c16);
      buffer32.write(c32);
      
      checkCount++;
   }
   
   bool readCheck()
   {
      uint8_t c8 = 0;
      uint16_t c16 = 0;
      uint32_t c32 = 0;
      
      uint8_t* b8Data = buffer8.mPtr + buffer8.mPos;
      uint16_t* b16Data = (uint16_t*)(buffer16.mPtr + buffer16.mPos);
      uint32_t* b32Data = (uint32_t*)(buffer32.mPtr + buffer32.mPos);
      
      buffer8.read(c8);
      buffer16.read(c16);
      buffer32.read(c32);
      
      if (c8 == checkCount &&
          c16 == checkCount &&
          c32 == checkCount) {
         checkCount++;
         return true;
      }
      
      assert(false);
      checkCount++;
      return false;
   }
   
   
   // For array types
   template<class T, int N> inline bool read( T (&value)[N] )
   {
      if (sizeof(T) == 32)
      {
         return buffer32.read(sizeof(T) * N, value);
      }
      else if (sizeof(T) == 16)
      {
         return buffer16.read(sizeof(T) * N, value);
      }
      else if (sizeof(T) == 8)
      {
         return buffer8.read(sizeof(T) * N, value);
      }
      else
      {
         assert(false);
         return false;
      }
   }
   
   // For normal scalar types
   template<typename T> inline bool read(T &value)
   {
      if (sizeof(T) == 4)
      {
         return buffer32.read(value);
      }
      else if (sizeof(T) == 2)
      {
         return buffer16.read(value);
      }
      else if (sizeof(T) == 1)
      {
         return buffer8.read(value);
      }
      else
      {
         assert(false);
         return false;
      }
   }
   
   inline bool read32(std::size_t size, void* data)
   {
      return buffer32.read(size * 4, data);
   }
   inline bool read16(std::size_t size, void* data)
   {
      return buffer16.read(size * 2, data);
   }
   inline bool read8(std::size_t size, void* data)
   {
      return buffer8.read(size, data);
   }
   
   // WRITE
   
   // For array types
   template<class T, int N> inline bool write( T (&value)[N] )
   {
      if (sizeof(T) == 32)
      {
         return buffer32.write(sizeof(T) * N, value);
      }
      else if (sizeof(T) == 16)
      {
         return buffer16.write(sizeof(T) * N, value);
      }
      else if (sizeof(T) == 8)
      {
         return buffer8.write(sizeof(T) * N, value);
      }
      else
      {
         assert(false);
         return false;
      }
   }
   
   // For normal scalar types
   template<typename T> inline bool write(T &value)
   {
      if (sizeof(T) == 32)
      {
         return buffer32.write(value);
      }
      else if (sizeof(T) == 16)
      {
         return buffer16.write(value);
      }
      else if (sizeof(T) == 8)
      {
         return buffer8.write(value);
      }
      else
      {
         assert(false);
         return false;
      }
   }
   
   inline bool write32(std::size_t size, void* data)
   {
      return buffer32.write(size * 4, data);
   }
   inline bool write16(std::size_t size, void* data)
   {
      return buffer16.write(size * 2, data);
   }
   inline bool write8(std::size_t size, void* data)
   {
      return buffer8.write(size, data);
   }
   
   inline bool readBool()
   {
      uint8_t value = 0;
      buffer8.read(value);
      return value != 0;
   }
   
   inline void writeBool(bool value)
   {
      buffer8.write((uint8_t)(value ? 1 : 0));
   }
   
   inline bool readNullString(std::string& value)
   {
      return buffer8.readNullString(value);
   }
   
};

}
