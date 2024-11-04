#include "CommonData.h"

/*
 NOTE: Internally each grid block in torque is just a heightmap sized as [y][x] with
 [x][y] squares. Most commonly, a fixed 256x256 heightmap with 256x256 squares.
 The heightmap repeats, so the height for the right of square 255 is the same as height 0.

 Unlike tribes 1, the heightmap is represented as 16bit fixed point values.
 
 For a square at (x,y) the corners use the following heightmap values:
 
 (x+0,y+0)-----(x+0,y+0)
 |                     |
 |                     |
 |                     |
 |                     |
 |                     |
 (x+0,y+1)-----(x+1,y+1)
 
 When handling detail levels, each detail level skips a power of two heightmap values.
 Squares are split at different diagonals using a checkerboard pattern.
 
 Also of note, each square consists of 4 points in the highest detail and 9 in the subsequent detail levels.
 This helps smooth things out in the lower detail levels.
 
 Bitmaps for terrain squares are assembled from a set of 8 possible materials blended together 
 dynamically via a blending algorithm.
 */
class TerrainBlock
{
public:
   enum
   {
      MaxMaterials = 8,
   };

   struct MaterialMap
   {
      enum
      {
         MatIndexMask = 0x7
      };

      uint8_t flag;
      uint8_t matIndex;
      
      static slm::vec2 sMatCoords[4];
      
      /*
       NOTE: texture coords are arranged as follows (assuming opengl convention):

       0  7  6
       1  8  5
       2  3  4

       Level 0 squares use the outer points, while subsequent detail levels use all the points.
       In addition the relevant square offset is applied for subsequent details.
       */
      static void getBaseTexCoords(slm::vec2* outCoords)
      {
         outCoords[0] = sMatCoords[0];
         outCoords[1] = sMatCoords[1];
         outCoords[2] = sMatCoords[2];
         outCoords[3] = sMatCoords[3];
      }
   };
   
#pragma pack(1)
   struct GridSquare
   {
      uint8_t flags;
      uint8_t matIndex;
      
      enum
      {
         // NOTE: matFlags is first followed by this
         Split45 = 0x40, // 6
         HasEmpty = 0x80 // 7
     };
 };
#pragma pack()

   uint32_t mLightScale;  // shift for lightmap
   uint32_t mSize[2];     // heightmap dimensions
   uint32_t mBlockScale;

   std::vector<uint16_t> mHeightMap;
   std::vector<uint16_t> mLightMap;
   std::vector<uint8_t> mAlphaMap[MaxMaterials];
   std::vector<uint8_t> mBaseMaterialMap;

   std::vector<MaterialMap> mMatMap;
   std::vector<GridSquare> mGridMapBase;

   MaterialList* mMaterialList;
   std::string mTextureScript;
   std::string mHeightFieldScript;
   
   
   TerrainBlock()
   {
   }
   
   virtual ~TerrainBlock()
   {
   }
   
   inline uint32_t getLightMapWidth() const
   {
      return (mSize[0] << mLightScale) + 1;
   }
   
   inline uint32_t getHeightMapSize() const
   {
      return (mSize[0] + 1) * (mSize[1] + 1);
   }
   
   inline uint32_t getHeightMapWidth() const
   {
      return (mSize[0] + 1);
   }
   
   inline uint32_t getHeightMapHeight() const
   {
      return (mSize[1] + 1);
   }
   
   inline uint32_t getGridMapWidth() const
   {
      return (mSize[0]);
   }
   
   inline uint32_t getGridMapHeight() const
   {
      return (mSize[1]);
   }
   
   inline uint32_t getMatMapSize() const
   {
      return (mSize[0]) * (mSize[1]);
   }
   
   float getHeight(uint32_t x, uint32_t y)
   {
      return fixedToFloat(mHeightMap[(y*mSize[0]) + x]);
   }

   inline float fixedToFloat(uint16_t value)
   {
      return float(value) * 0.03125f;
   }

   inline uint16_t floatToFixed(uint16_t value)
   {
      return uint16_t(value * 32.0f);
   }
   
   bool read(MemRStream &mem)
   {
      uint32_t version = 0;
      mem.read(version);
      if (version > 5)
      {
         return false;
      }

      uint32_t blockSize = 0;
      uint32_t matGroups = 8;
      uint32_t maxMaterials = 0;

      if (version < 4)
      {
         mSize[0] = 256;
         mSize[1] = 256;
         mLightScale = 9;
      }
      else
      {
        // TODO
      }

      mHeightMap.resize(blockSize*blockSize);
      mMatMap.resize(blockSize*blockSize);

      mem.read(mSize[0]*mSize[1]*2, &mHeightMap[0]);
      mem.read(mSize[0]*mSize[1]*2, &mMatMap[0]);

      uint8_t* baseMap = (uint8_t*)&mMatMap[0];

      // NOTE: the material map in this case seems to be a bit of a leftover 
      // from t1; it doesn't appear to store any USEFUL information besides the 
      // "primary" material.

      // Unpack base
      for (int32_t i=(mSize[0]*mSize[1])-1; i>=0; i--)
      {
         mMatMap[i].flag = baseMap[i] & ~0x7;
         mMatMap[i].matIndex = baseMap[i] & ~0x7;
      }

      mMaterialList = new MaterialList();
      for (uint32_t i=0; i<matGroups; i++)
      {
         std::string val = "";
         mem.readS8String(val);
         mMaterialList->push_back(val.c_str());
         if (!val.empty())
         {
            maxMaterials++;
         }
      }

      for (uint32_t i=0; i<maxMaterials; i++)
      {
         mAlphaMap[i].resize(mSize[0]*mSize[1]);
      }

      if (version == 1)
      {
         for (uint32_t i=0; i<maxMaterials; i++)
         {
            mAlphaMap[i].resize(mSize[0]*mSize[1]);
            memset(&mAlphaMap[i], 0x00, mSize[0]*mSize[1]);
         }

         for (uint32_t i=0; i<mSize[0]*mSize[1]; i++)
         {
            const uint8_t matIndex = mMatMap[i].matIndex;
            std::vector<uint8_t>& alphaMap = mAlphaMap[matIndex];
            alphaMap[i] = 255;
         }
      }
      else
      {
         for (uint32_t i=0; i<maxMaterials; i++)
         {
            if (!mMaterialList->isBlank(i))
            {
               mAlphaMap[i].resize(mSize[0]*mSize[1]);
               mem.read(mSize[0]*mSize[1], &mAlphaMap[i]);
            }
            else
            {
               mAlphaMap[i].clear();
            }
         }
      }

      // NOTE: This is for terrains that are purely procedural
      if (version >= 3)
      {
         mem.readSString32(mHeightFieldScript);
         mem.readSString32(mTextureScript);
      }
      else
      {
         mHeightFieldScript = "";
         mTextureScript = "";
      }

      buildGridMap();

      return true;
   }

   inline GridSquare* findSquare(int32_t x, int32_t y)
   {
      return (&mGridMapBase[0] +
         (x) +
         (y * mSize[0]));
   }

   inline MaterialMap* getMaterialMap(int32_t x, int32_t y)
   {
      return (&mMatMap[0] +
         (y * mSize[0])
         + x);
   }

   void buildGridMap()
   {
      mGridMapBase.resize(mSize[0] * mSize[1]);
      processGrid();
   }
   
   void processGrid()
   {
      for (int32_t squareX = 0; squareX < mSize[0]; squareX++)
      {
         for (int32_t squareY = 0; squareY < mSize[1]; squareY++)
         {
            GridSquare* sq = findSquare(squareX, squareY);
            processSquare(squareX, squareY, sq);
         }
      }
   }
   
   void processSquare(int32_t squareX, int32_t squareY, GridSquare* sq)
   {
      // NOTE: since we're just rendering the base level here we just factor in whats set in the square
      TerrainBlock::MaterialMap* mat = getMaterialMap(squareX, squareY);
      
      // TODO: this needs to be set from the empty field
      bool emptySet = false;//(mat->flag & (1 << TerrainBlock::MaterialMap::EmptyShift)) != 0;
      bool shouldSplit45 = ((squareX ^ squareY) & 1) == 0;
      
      sq->flags = 0;
      sq->flags |= emptySet ? (GridSquare::HasEmpty) : 0;
      sq->matIndex = mat->matIndex;
      
      if (shouldSplit45)
      {
         //sq->flags = 1;
         sq->flags |= GridSquare::Split45;
      }
   }
};

