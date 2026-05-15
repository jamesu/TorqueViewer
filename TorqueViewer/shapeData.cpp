#include "CommonData.h"
#include "shapeData.h"

namespace Dts3
{

bool Shape::checkSkip(int meshNumber, int currentObject, int currentDecal, int skipDetailLevel)
{
   if(skipDetailLevel == 0) return false;
   
   int skipSubshape = mDetailLevels[skipDetailLevel].subshape;
   
   if(currentObject < mObjects.size())
   {
      int start = mObjects[currentObject].firstMesh;
      if(meshNumber >= start) {
         if(meshNumber < start + mObjects[currentObject].numMeshes) {
            if(mSubshapes[skipSubshape].firstObject > currentObject) {
               return true;
            }
            if((mSubshapes.size() == skipSubshape + 1) ||
               (currentObject < mSubshapes[skipSubshape + 1].firstObject)) {
               return meshNumber - start < mDetailLevels[skipDetailLevel].objectDetail;
            }
            return false;
         }
         return checkSkip(meshNumber, currentObject + 1, currentDecal, skipDetailLevel);
      }
   }
   
   if(currentDecal < mDecals.size())
   {
      int start = mDecals[currentDecal].firstMesh;
      if(meshNumber >= start) {
         if(meshNumber < start + mDecals[currentDecal].numMeshes) {
            if(mSubshapes[skipSubshape].firstDecal > currentDecal) {
               return true;
            }
            if((mSubshapes.size() == skipSubshape + 1) ||
               (currentDecal < mSubshapes[skipSubshape + 1].firstDecal)) {
               return meshNumber - start < mDetailLevels[skipDetailLevel].objectDetail;
            }
            return false;
         }
         return checkSkip(meshNumber, currentObject, currentDecal + 1, skipDetailLevel);
      }
   }
   return false;
}

bool Shape::read(MemRStream& stream)
{
   MemRStream headerProbe = stream;
   uint32_t hdr[4] = {};
   if (!headerProbe.read(sizeof(hdr), hdr))
      return false;

   const uint32_t version = hdr[0] & 0xFF;
   if (version < 19)
      return IO::readLegacyUnifiedShape(this, stream, version);

   SplitStream split;
   if (!split.floodFromStream(stream))
   {
      return false;
   }
   return IO::readShape(this, split);
}

}
