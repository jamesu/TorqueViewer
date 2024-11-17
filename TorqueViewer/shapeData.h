#include "CommonData.h"

#include <iostream>
#include <vector>
#include <array>
#include <cmath>
#include <string>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstdint>

namespace Dts3
{

using namespace std;
class Shape;
struct Node;
struct Object;
struct Decal;

enum
{
   DefaultVersion = 24
};

class NameTable
{
private:
   std::vector<std::string> mStrings;
   
public:
   
   NameTable()
   {
   }
   ~NameTable()
   {
   }
   
   int addString(const std::string& str, bool caseSensitive = false)
   {
      std::string strCompare = caseSensitive ? str : toLower(str);
      
      for (size_t i = 0; i < mStrings.size(); ++i) {
         std::string current = caseSensitive ?
         mStrings[i] : toLower(mStrings[i]);
         if (current == strCompare) {
            return static_cast<int>(i);
         }
      }
      
      mStrings.push_back(str);
      return static_cast<int>(mStrings.size() - 1);
   }
   
   const std::string& get(std::size_t index) const
   {
      if (index < mStrings.size())
      {
         return mStrings[index];
      }
      return emptyString;
   }
   
   inline int insert(const std::string& str)
   {
      return addString(str, true);
   }
   
private:
   
   static std::string emptyString;
   
   static std::string toLower(const std::string& str)
   {
      std::string result = str;
      std::transform(result.begin(), result.end(), result.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      return result;
   }
};

struct Primitive
{
   enum Type : uint32_t
   {
      Triangles    = 0,
      Strip        = BIT(30),
      Fan          = BIT(31),
      TypeMask     = BIT(30) | BIT(31),
      Indexed      = BIT(29),
      NoMaterial   = BIT(28),
      MaterialMask = 0xFFFFFFF
   };
   
   uint16_t firstElement;
   uint16_t numElements;
   Type matIndex;
   
   Primitive(int fe=0, int ne=0, Type ty=Triangles) :
   firstElement(fe), numElements(ne), matIndex(ty)
   {
   }
};


struct Cluster
{
   int startPrimitive;
   int endPrimitive;
   slm::vec3 normal;
   float k;
   int frontCluster;
   int backCluster;
   
   Cluster(int sp = 0, int ep = 0, slm::vec3 nrm = slm::vec3(0), float kn = 0.0, int fc = 0, int bc = 0)
   : startPrimitive(sp), endPrimitive(ep), normal(nrm), k(kn), frontCluster(fc), backCluster(bc)
   {
   }
};


struct IflMaterial
{
   int name;
   int slot;
   int firstFrame;
   float time;
   int numFrames;
   
   IflMaterial(int na=0, int sl=0, int ff=0, float ti=0.0f, int nf=0) :
   name(na), slot(sl), firstFrame(ff), time(ti), numFrames(nf)
   {
   }
};

struct Decal
{
   int name;
   int numMeshes;
   int firstMesh; ///< First decal mesh
   int object;    ///< Used by loading code to assign firstDecal list
   int nextSibling;
};

struct DetailLevel
{
   DetailLevel(int na=0, int ss=0, int od=0, float sz=0.0f, int ae=-1, int me=-1, int pc=0) :
   name(na), subshape(ss), objectDetail(od), size(sz), avgError(ae), maxError(me), polyCount(pc)
   {
   }
   
   int name;
   int subshape;     ///< Subshape to use for this detail level
   int objectDetail; ///< Mesh index to use for objects
   float size;       ///< Pixel size
   int avgError;
   int maxError;
   int polyCount;
};

struct SubShape
{
   int firstNode;    ///< First node to ANIMATE
   int firstObject;  ///< First object to RENDER
   int firstDecal;   ///< First decal to RENDER
   int numNodes;
   int numObjects;
   int numDecals;
   int firstTranslucent;
   
   SubShape(int fn=0, int fo=0, int fd=0, int nn=0, int no=0, int nd=0) :
   firstNode(fn), firstObject(fo), firstDecal(fd), numNodes(nn), numObjects(no), numDecals(nd), firstTranslucent(0)
   {
   }
};

struct Object
{
   int name;
   int numMeshes;
   int firstMesh;      ///< Index of first mesh
   int node;           ///< Corresponds to the base transform used for rendering
   int nextSibling;    ///< Corresponds to next object in chain; determined at runtime
   int firstDecal;     ///< Corresponds to first decal used; determined at runtime, used for debris
   
   void resetRuntime()
   {
      firstDecal = -1;
      nextSibling = -1;
   }
};

struct Node
{
   int name;
   int parent;
   int firstObject; ///< First object used by node; determined at runtime
   int firstChild;  ///< First child of node; determined at runtime
   int nextSibling; ///< Next sibling of node; determined at runtime
   
   void resetRuntime()
   {
      firstObject = -1;
      firstChild = -1;
      nextSibling = -1;
   }
};

struct DecalState
{
   int32_t frame;
   
   DecalState(int fr=0) : frame(fr)
   {
   }
};

struct ObjectState
{
   float vis;
   int frame;
   int matFrame;
   
   ObjectState(float vs=1.0f, int fr=0, int mf=0) :
   vis(vs), frame(fr), matFrame(mf)
   {
   }
};

struct Trigger
{
   enum State : uint32_t
   {
      StateOn = BIT(31),
      InvertOnReverse = BIT(30),
      StateMask = BIT(30)-1
   };
   
   float pos;
   int state;
   
   
   Trigger(int st=0, bool on=true, float ps=0.0f, bool revert=false) :
   pos(ps), state(st)
   {
      st -= 1;
      state = 1 << st;
      if (on) state |= StateOn;
      if (revert) state |= InvertOnReverse;
   }
};


struct Sequence
{
   enum Flags : uint32_t
   {
      UniformScale    = 0x0001,
      AlignedScale    = 0x0002,
      ArbitraryScale  = 0x0004,
      Blend           = 0x0008,
      Cyclic          = 0x0010,
      MakePath        = 0x0020,
      IFLInit         = 0x0040,
      HasTranslucency = 0x0080
   };
   
   int nameIndex;
   uint32_t flags;
   int numKeyFrames;
   float duration;
   int priority;
   int firstGroundFrame;
   int numGroundFrames;
   int baseRot;
   int baseTrans;
   int baseScale;
   int baseObjectState;
   int baseDecalState;
   int firstTrigger;
   int numTriggers;
   float toolBegin;
   
   IntegerSet mattersRot;        // matters mask for node*Rotation
   IntegerSet mattersTranslation;// matters mask for node*Translation
   IntegerSet mattersScale;      // matters mask for node*Scale
   IntegerSet mattersDecal;      // matters mask for DecalState frame
   IntegerSet mattersIfl;        // matters mask for IFLMaterial frame
   IntegerSet mattersVis;        // matters mask for ObjectState vis
   IntegerSet mattersFrame;      // matters mask for ObjectState frame
   IntegerSet mattersMatframe;   // matters mask for ObjectState matFrame (goes f1mf1 f1mf2 f2mf1 f2mf2 ...)
   
   // NOTE: ObjectState is a slight mess here since vis,frame,matFrame are all combined.
   
   // Constructor
   Sequence(int na = 0, int fl = 0, int nk = 0, float du = 0.0, int pri = 0,
            int fg = -1, int ng = 0, int br = -1, int bt = -1, int bs = -1,
            int bos = -1, int bds = -1, int ft = -1, int nt = 0, float tb = 0.0, int bm = 0)
   : nameIndex(na), flags(fl), numKeyFrames(nk), duration(du), priority(pri),
   firstGroundFrame(fg), numGroundFrames(ng), baseRot(br), baseTrans(bt),
   baseScale(bs), baseObjectState(bos), baseDecalState(bds), firstTrigger(ft),
   numTriggers(nt), toolBegin(tb)
   {
   }
   
   ~Sequence()
   {
   }
   
   
   inline bool testFlags(uint32_t inFlags) const
   {
      return (flags & inFlags) != 0;
   }
   
   void read(MemRStream &fs, int version)
   {
      fs.read(nameIndex);
      fs.read(flags);
      fs.read(numKeyFrames);
      fs.read(duration);
      fs.read(priority);
      fs.read(firstGroundFrame);
      fs.read(numGroundFrames);
      fs.read(baseRot);
      fs.read(baseTrans);
      fs.read(baseScale);
      fs.read(baseObjectState);
      fs.read(baseDecalState);
      fs.read(firstTrigger);
      fs.read(numTriggers);
      fs.read(toolBegin);
      
      // Assume readIntegerSet is a function to read vector<int> from the file stream
      readIntegerSet(fs, mattersRot);
      readIntegerSet(fs, mattersTranslation);
      readIntegerSet(fs, mattersScale);
      readIntegerSet(fs, mattersDecal);
      readIntegerSet(fs, mattersIfl);
      readIntegerSet(fs, mattersVis);
      readIntegerSet(fs, mattersFrame);
      readIntegerSet(fs, mattersMatframe);
   }
   
   void write(MemRStream &fs, int version, bool noIndex=false)
   {
      if (!noIndex)
      {
         fs.write(nameIndex);
      }
      
      fs.write(flags);
      fs.write(numKeyFrames);
      fs.write(duration);
      fs.write(priority);
      fs.write(firstGroundFrame);
      fs.write(numGroundFrames);
      fs.write(baseRot);
      fs.write(baseTrans);
      fs.write(baseScale);
      fs.write(baseObjectState);
      fs.write(baseDecalState);
      fs.write(firstTrigger);
      fs.write(numTriggers);
      fs.write(toolBegin);
      
      writeIntegerSet(fs, mattersRot);
      writeIntegerSet(fs, mattersTranslation);
      writeIntegerSet(fs, mattersScale);
      writeIntegerSet(fs, mattersDecal);
      writeIntegerSet(fs, mattersIfl);
      writeIntegerSet(fs, mattersVis);
      writeIntegerSet(fs, mattersFrame);
      writeIntegerSet(fs, mattersMatframe);
   }
};

// NOTE: We take a slightly different approach than torque
// here since we need to factor in parent data without relying
// too much on the stream.

struct AbstractData
{
   virtual ~AbstractData()
   {
   }
};

struct BasicData : public AbstractData
{
   std::vector<slm::vec3> verts;
   std::vector<slm::vec2> tverts;
   std::vector<slm::vec3> normals;
   std::vector<uint8_t> enormals;
   std::vector<Primitive> primitives;
   std::vector<uint16_t> indices;
   std::vector<uint16_t> mergeIndices;
};

struct DecalData : public AbstractData
{
   std::vector<Primitive> primitives;
   std::vector<uint16_t> indices;
   std::vector<int32_t> startPrimitive; // frame offsets for primitives
   std::vector<slm::vec4> texGenS;
   std::vector<slm::vec4> texGenT;
   uint32_t meshIndex;
   int32_t matIndex;
};

struct SkinData : public BasicData
{
   std::vector<uint32_t> vindex; // local vertex
   std::vector<uint32_t> bindex; // local node index
   std::vector<float> vweight;   // local node weight
   std::vector<uint32_t> nodeIndex;       // global node -> local node
   std::vector<slm::mat4> nodeTransforms; // local node conversion transform
};

struct SortedData : public BasicData
{
   std::vector<Cluster> clusters;
   std::vector<int32_t> startCluster;
   std::vector<int32_t> firstVerts;
   std::vector<int32_t> numVerts;
   std::vector<int32_t> firstTVerts;
   bool alwaysWriteDepth;
};


static void EmitModelVertices(BasicData* basicData, ModelVertex* outv)
{
   for (uint32_t i=0; i<basicData->verts.size(); i++)
   {
      ModelVertex& ov = outv[i];
      ov.position = basicData->verts[i];
      ov.normal = basicData->normals[i];
   }
}

static void EmitModelTexVertices(BasicData* basicData, ModelTexVertex* outv)
{
   for (uint32_t i=0; i<basicData->tverts.size(); i++)
   {
      ModelTexVertex& ov = outv[i];
      ov.texcoord = basicData->tverts[i];
   }
}

static void EmitPackedSkinVertices(SkinData* skinData, ModelSkinVertex* outv)
{
   for (size_t i = 0; i < skinData->vindex.size(); i++)
   {
      uint32_t vIdx = skinData->vindex[i];
      uint32_t bIdx = skinData->bindex[i];
      float weight = skinData->vweight[i];
      
      ModelSkinVertex& ov = outv[vIdx];
      for (uint8_t j=0; j<ModelSkinVertex::MAX_WEIGHTS; j++)
      {
         if (ov.weights[j] != 0.0f)
         {
            ov.index[j] = bIdx;
            ov.weights[j] = weight;
            break;
         }
      }
   }
}

class Mesh
{
   friend class IO;
public:
   
   enum Type : uint32_t
   {
      T_Standard = 0,
      T_Skin = 1,
      T_Decal = 2,
      T_Sorted = 3,
      T_Null = 4,
      
      T_MASK = T_Skin | T_Decal | T_Sorted | T_Null
   };
   
   enum Flags : uint32_t
   {
      F_Billboard = BIT(31),
      F_HasDetail = BIT(30),
      F_BillboardZ = BIT(29),
      F_EncodedNormals = BIT(28),
      
      F_MASK = F_Billboard | F_HasDetail | F_BillboardZ | F_EncodedNormals
   };
   
   Type mType;
   uint32_t mFlags;
   AbstractData* mData;
   
   // Common data
   uint32_t mNumFrames;
   uint32_t mNumMatFrames;
   uint32_t mVertsPerFrame;
   int32_t mParent;
   float mRadius;
   
   slm::vec3 mCenter;
   Box mBounds;
   
   Mesh(Type t = T_Null)
   : mRadius(0.0), mNumFrames(1), mNumMatFrames(1), mVertsPerFrame(0), mParent(-1), mFlags(0), mType(t)
   {
      
   }
   
   ~Mesh()
   {
      clearData();
   }
   
   BasicData* getBasicData() const
   {
      if (mType < T_Null && mType != T_Decal)
         return reinterpret_cast<BasicData*>(mData);
      else
         return NULL;
   }
   
   SkinData* getSkinData() const
   {
      return mType == T_Skin ? reinterpret_cast<SkinData*>(mData) : NULL;
   }
   
   DecalData* getDecalData() const
   {
      return mType == T_Decal ? reinterpret_cast<DecalData*>(mData) : NULL;
   }
   
   SortedData* getSortedData() const
   {
      return mType == T_Sorted ? reinterpret_cast<SortedData*>(mData) : NULL;
   }
   
   void clearData()
   {
      if (mData == NULL)
         return;
      
      delete mData;
      mData = NULL;
   }
   
   inline int getType() const { return mType; }
   inline void setType(Type t) { mType = t; }
   inline void setFlag(uint32_t f) { mFlags |= f; }
   
   std::size_t getPolyCount() const
   {
      std::size_t count = 0;
      BasicData* data = getBasicData();
      if (data == NULL)
         return 0;
      
      for (const auto& p : data->primitives) {
         if (p.matIndex & Primitive::Strip) {
            count += p.numElements - 2;
         } else {
            count += p.numElements / 3;
         }
      }
      return count;
   }
   
   inline float getRadius() const { return mRadius; }
   
   float getRadiusFrom(const slm::vec3& trans, const slm::quat& rot, const slm::vec3& center) const
   {
      float radius = 0.0;
      BasicData* data = getBasicData();
      if (data == NULL)
         return 0;
      
      slm::mat4 rmat(rot);
      for (const auto& vert : data->verts)
      {
         slm::vec4 v4(vert, 1);
         slm::vec3 tv = (rmat * v4).xyz() + trans;
         float distance = slm::length(tv - center);
         if (distance > radius) {
            radius = distance;
         }
      }
      return radius;
   }
   
   float getTubeRadiusFrom(const slm::vec3& trans, const slm::quat& rot, const slm::vec3& center) const
   {
      float radius = 0.0;
      BasicData* data = getBasicData();
      if (data == NULL)
         return 0;
      
      slm::mat4 rmat(rot);
      for (const auto& vert : data->verts)
      {
         slm::vec4 v4(vert, 1);
         slm::vec3 tv = (rmat * v4).xyz() + trans;
         slm::vec2 distance(tv.x, tv.y);
         float distance2 = slm::length(distance);
         if (distance2 > radius) {
            radius = distance2;
         }
      }
      return radius;
   }
   
   slm::vec3 getCenter() const { return mCenter; }
   
   Box getBounds(const slm::vec3& trans, const slm::quat& rot) const
   {
      Box bounds2;
      bounds2.max = slm::vec3(FLT_MIN, FLT_MIN, FLT_MIN);
      bounds2.min = slm::vec3(FLT_MAX, FLT_MAX, FLT_MAX);
      BasicData* data = getBasicData();
      if (data == NULL)
         return bounds2;
      
      slm::mat4 rmat(rot);
      for (const auto& vert : data->verts)
      {
         slm::vec4 v4(vert, 1);
         slm::vec3 tv = (rmat * v4).xyz() + trans;
         bounds2.min.x = std::min(bounds2.min.x, tv.x);
         bounds2.min.y = std::min(bounds2.min.y, tv.y);
         bounds2.min.z = std::min(bounds2.min.z, tv.z);
         bounds2.max.x = std::max(bounds2.max.x, tv.x);
         bounds2.max.y = std::max(bounds2.max.y, tv.y);
         bounds2.max.z = std::max(bounds2.max.z, tv.z);
      }
      
      return bounds2;
   }
   
   inline int32_t getNodeIndexCount() const
   {
      SkinData* data = getSkinData();
      if (data == NULL)
         return 0;
      return (int32_t)data->nodeIndex.size();
   }
   
   int32_t getNodeIndex(uint32_t node) const
   {
      SkinData* data = getSkinData();
      if (data == NULL)
         return -1;
      
      if (node >= 0 && node < data->nodeIndex.size())
      {
         return data->nodeIndex[node];
      }
      return -1;
   }
   
   void setCenter(const slm::vec3& c) { mCenter = c; }
   void setBounds(const Box& b) { mBounds = b; }
   void setRadius(float r) { mRadius = r; }
   void setFrames(uint32_t n)
   {
      BasicData* data = getBasicData();
      if (data == NULL)
         return 0;
      
      mNumFrames = n;
      mVertsPerFrame = (uint32_t)data->verts.size() / n;
   }
   void setParent(int n) { mParent = n; }
   
   void calculateBounds()
   {
      mBounds.max = slm::vec3(FLT_MIN, FLT_MIN, FLT_MIN);
      mBounds.min = slm::vec3(FLT_MAX, FLT_MAX, FLT_MAX);
      BasicData* data = getBasicData();
      if (data == NULL)
         return 0;
      
      for (const auto& vertex : data->verts)
      {
         mBounds.min.x = std::min(mBounds.min.x, vertex.x);
         mBounds.min.y = std::min(mBounds.min.y, vertex.y);
         mBounds.min.z = std::min(mBounds.min.z, vertex.z);
         mBounds.max.x = std::max(mBounds.max.x, vertex.x);
         mBounds.max.y = std::max(mBounds.max.y, vertex.y);
         mBounds.max.z = std::max(mBounds.max.z, vertex.z);
      }
   }
   
   void calculateCenter()
   {
      mCenter = mBounds.min + ((mBounds.max - mBounds.min) / 2);
   }
   
   float calculateRadius()
   {
      float radius = 0.0;
      BasicData* data = getBasicData();
      if (data == NULL)
         return 0;
      
      for (const auto& vertex : data->verts)
      {
         slm::vec3 tV = vertex - mCenter;
         float distance = (tV.x * tV.x + tV.y * tV.y + tV.z * tV.z);
         if (distance > radius)
         {
            radius = distance;
         }
      }
      return radius > 0.0f ? sqrt(radius) : 0.0f;
   }
   
   int encodeNormal(const slm::vec3& p) const { return 0; }
};

struct ThreadPath
{
   float start;
   float end;
   int32_t loop;
};

struct ThreadTransitionState
{
   // Transition state
   float duration;
   float pos;
   float direction;
   float targetScale;
   
   // Pre-transition state
   IntegerSet oldRotations;
   IntegerSet oldTranslations;
   IntegerSet oldScales;
   
   uint32_t oldSequenceIdx;
   float oldPos;
};

// NOTE: we try and go with what torque does, since
// animation behavior is gets VERY specific.
class Thread
{
   // General
   int32_t priority;
   Shape* shape;
   
   // Sequence position
   int32_t sequenceIdx;
   float pos;
   float timeScale;
   
   // Keyframe blend
   int32_t keyA;
   int32_t keyB;
   float keyPos;
   
   // State
   bool playing;
   bool transitioning;
   bool noBlend;
   bool makePath;
   
   ThreadTransitionState transitionState;
   ThreadPath path; ///< Path for triggers
   
   Thread()
   {
   }
};


class ShapeViewer;

class Shape : public ResourceInstance
{
   friend class IO;
   friend class ShapeViewer;
   
public:
   
   enum Flag : uint8_t
   {
      UniformScale = BIT(0),
      AlignedScale = BIT(1),
      ArbitraryScale = BIT(2),
      Blend = BIT(3),
      Cyclic = BIT(4),
      MakePath = BIT(5),
      IflInit = BIT(6),
      HasTranslucency = BIT(7),
      AnyScale = UniformScale | AlignedScale | ArbitraryScale
   };
   
//protected:
   
   // Bounds
   Box mBounds;
   slm::vec3 mCenter;
   float mTubeRadius;
   float mRadius;
   
   // Related objects
   std::vector<Mesh> mMeshes;
   std::vector<Node> mNodes;
   std::vector<Sequence> mSequences;
   std::vector<Trigger> mTriggers;
   std::vector<Object> mObjects;
   std::vector<ObjectState> mObjectStates;
   std::vector<IflMaterial> mIflMaterials;
   std::vector<SubShape> mSubshapes;
   std::vector<DetailLevel> mDetailLevels;
   std::vector<Decal> mDecals;
   std::vector<DecalState> mDecalStates;
   
   // Keyframe data
   std::vector<Quat16> mDefaultRotations;
   std::vector<slm::vec3> mDefaultTranslations;
   std::vector<slm::vec3> mNodeTranslations;
   std::vector<Quat16> mNodeRotations;
   std::vector<float> mNodeUniformScales;
   std::vector<slm::vec3> mNodeAlignedScales;
   std::vector<slm::vec3> mNodeArbitraryScaleFactors;
   std::vector<Quat16> mNodeArbitraryScaleRotations;
   std::vector<slm::vec3> mGroundTranslations;
   std::vector<Quat16> mGroundRotations;
   
   // Detail level state
   std::vector<float> mAlphaIn;
   std::vector<float> mAlphaOut;
   std::vector<int> mPreviousMerge;
   
   // Materials we use
   MaterialList mMaterials;
   // Names we use
   NameTable mNameTable;
   
   // Misc
   bool mExportMerge;
   int mSmallestVisibleSize;
   int mSmallestVisibleDetailLevel;
   
   uint32_t mRuntimeFlags;
   
public:
   Shape() : mTubeRadius(0), mRadius(0), mExportMerge(false),
   mSmallestVisibleSize(0), mSmallestVisibleDetailLevel(0) {}
   
   ~Shape()
   {
      for (Mesh& mesh : mMeshes)
      {
         mesh.clearData();
      }
   }
   
   Node* getNode(const std::string_view& name);
   int getNodeIndex(const std::string_view& name);
   
   Sequence* getSequence(const std::string_view& name);
   
   bool checkSkip(int meshNumber, int currentObject, int currentDecal, int skipDetailLevel);
   
   virtual bool read(MemRStream& stream);
};

}

#include "shapeIO.h"
