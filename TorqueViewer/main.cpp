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

class Volume
{
public:

   enum Sig : uint32_t
   {
      ZIP_LOCAL_FILE_HEADER_SIG = 0x04034b50,
      ZIP_CENTRAL_DIR_HEADER_SIG = 0x02014b50,
      ZIP_END_CENTRAL_DIR_SIG = 0x06054b50,
      ZIP64_END_CENTRAL_DIR_SIG = 0x06064b50,
      ZIP64_END_CENTRAL_DIR_LOC_SIG = 0x07064b50
   };

   enum Flag : uint32_t
   {
      FLAG_ENCRYPTED = (1<<0),
      FLAG_COMPRESS1 = (1<<1),
      FLAG_COMPRESS2 = (1<<2),
      FLAG_HAS_DATA_DESC = (1<<3),
      FLAG_DEFLATE2 = (1<<4),
      FLAG_PATCH = (1<<5),
      FLAG_ENCRYPTED2 = (1<<6),
      FLAG_UTF8 = (1<<11),
      FLAG_ENCRYPTED_CD = (1<<13)
   };

   enum ExtraTypes : uint16_t
   {
      TYPE_EXTRA_ZIP64 = (1<<0)
   };

#pragma pack(push, 2)
   struct LocalHeader
   {
       uint32_t signature;
       uint16_t version;
       uint16_t flags;
       uint16_t compression;
       uint16_t mod_time;
       uint16_t mod_date;
       uint32_t crc32;
       uint32_t compressed_size;
       uint32_t uncompressed_size;
       uint16_t file_name_length;
       uint16_t extra_field_length;
   };

   struct ExtraFieldHeader
   {
       uint16_t type;
       uint16_t size;
   };

   struct Zip64Field
   {
       uint64_t compressed_size;
       uint64_t uncompressed_size;
       uint64_t local_header_offset;
       uint32_t disk_number_start;
   };

   struct CentralHeader
   {
       uint32_t signature;
       uint16_t version_made_by;
       uint16_t version_needed;
       uint16_t flags;
       uint16_t compression;
       uint16_t mod_time;
       uint16_t mod_date;
       uint32_t crc32;
       uint32_t compressed_size;
       uint32_t uncompressed_size;
       uint16_t file_name_length;
       uint16_t extra_field_length;
       uint16_t file_comment_length;
       uint16_t disk_number_start;
       uint16_t internal_file_attrs;
       uint32_t external_file_attrs;
       uint32_t local_header_offset;
   };
   
   
   struct EOCDRecord
   {
       uint32_t signature;
       uint16_t disk_number;
       uint16_t disk_cd;
       uint16_t num_disk_entries;
       uint16_t total_entries;
       uint32_t cd_size;
       uint32_t cd_offset;
       uint16_t comment_length;
   };

   struct EOCD64Record
   {
    uint32_t signature;
    uint64_t size_of_zip64_eocd;
    uint16_t version_made_by;
    uint16_t version_needed;
    uint32_t disk_number;
    uint32_t central_dir_disk;
    uint64_t total_entries_on_disk;
    uint64_t total_entries;
    uint64_t central_dir_size;
    uint64_t central_dir_offset;
   };

   struct EOCD64Locator
   {
       uint32_t signature;
       uint32_t disk_number;
       uint64_t eocd_offset;
       uint32_t total_disks;
   };
   
#pragma pack(pop)

   struct Entry
   {
      uint16_t flags;
      uint16_t compression;
      uint16_t filenameSize;
      uint64_t filenameOffset;
      uint64_t dataOffset;
      uint64_t compressedSize;
      uint64_t uncompressedSize;

      std::string_view getFilename(const char* data)
      {
         const char* name = data + filenameOffset;
         return std::string_view(name, filenameSize);
      }
      
      const char* getFilenamePtr(const char* data) const
      {
         return data + filenameOffset;
      }
   };
   
   std::vector<CentralHeader> mCentralHeaders;
   std::vector<Entry> mEntries;
   std::vector<char> mCDData;

   std::ifstream mFile;
   std::string mName;

   inline const char* getCDData()
   {
      return &mCDData[0];
   }
   
   Volume()
   {
   }
   
   ~Volume()
   {
      if (mFile.is_open())
      {
         mFile.close();
      }
   }

   bool readEOCD(std::ifstream& stream, EOCDRecord& eoCD, EOCD64Record& eoCD64)
   {
      static const uint64_t MaxEOCDSize = sizeof(EOCDRecord) + 65535;
      char buffer[MaxEOCDSize];

      stream.seekg(0, std::ios_base::end);
      std::streampos fileSize = stream.tellg();
      uint64_t size = static_cast<uint64_t>(fileSize);
      uint64_t maxSize = std::min<uint64_t>(MaxEOCDSize, size);
      if (maxSize < sizeof(sizeof(EOCDRecord)))
         return false;

      stream.seekg(size - maxSize);
      stream.read(buffer, maxSize);

      bool hasEOCD = false;
      uint64_t eoCDOffset = 0;

      for (int64_t offset = (int64_t)maxSize - sizeof(EOCDRecord); offset >= 0; offset--)
      {
         const uint32_t* ptr = reinterpret_cast<uint32_t*>(&buffer[offset]);
         if (*ptr == ZIP_END_CENTRAL_DIR_SIG)
         {
            eoCD = *reinterpret_cast<const EOCDRecord*>(ptr);
            eoCDOffset = (size - maxSize) + offset;
            hasEOCD = true;
            break;
         }
      }

      if (!hasEOCD)
      {
         return false;
      }

      // See if we have zip64 EOCD

      maxSize = std::min<uint64_t>(sizeof(EOCD64Locator), eoCDOffset);

      if (maxSize >= sizeof(EOCD64Locator))
      {
         EOCD64Locator locator;
         stream.seekg(eoCDOffset - sizeof(EOCD64Locator));
         stream.read(reinterpret_cast<char*>(&locator), sizeof(EOCD64Locator));

         if (locator.signature == ZIP64_END_CENTRAL_DIR_LOC_SIG)
         {
            stream.seekg(locator.eocd_offset);
            stream.read(reinterpret_cast<char*>(&eoCD64), sizeof(EOCD64Record));
         }
      }

      return true;
   }
   
   bool read(std::ifstream& stream)
   {
      EOCDRecord eoCD;
      EOCD64Record eoCD64;

      if (!readEOCD(stream, eoCD, eoCD64))
      {
         return false;
      }

      // Seek to CD
      uint64_t cdStart = eoCD.cd_offset;
      uint64_t cdSize = eoCD.cd_size;
      uint64_t totalFiles = eoCD.total_entries;

      if (cdStart == 0xFFFFFFFF && eoCD64.signature == ZIP64_END_CENTRAL_DIR_LOC_SIG)
      {
         // zip64
         cdStart = eoCD64.central_dir_offset;
         cdSize = eoCD64.central_dir_size;
         totalFiles = eoCD64.total_entries;
      }

      mCDData.resize(cdSize+1);
      stream.seekg(cdStart);
      stream.read(&mCDData[0], eoCD64.central_dir_size);
      mCDData[cdSize] = 0;

      // Populate entries
      mEntries.resize(totalFiles);
      CentralHeader* headerPtr = (CentralHeader*)&mCDData[0];
      CentralHeader* endHeader = (CentralHeader*)&mCDData[cdSize];
      for (uint32_t i=0; i<totalFiles; i++)
      {
         Entry& e = mEntries[i];
         if (headerPtr >= endHeader ||
            headerPtr->signature != ZIP_CENTRAL_DIR_HEADER_SIG)
         {
            return false;
         }

         e.flags = headerPtr->flags;
         e.compression = headerPtr->compression;
         e.filenameSize = headerPtr->file_name_length;
         e.filenameOffset = (uint8_t*)(headerPtr+1) - ((uint8_t*)&mCDData[0]);
         e.dataOffset = 0;
         e.compressedSize = headerPtr->compressed_size;
         e.uncompressedSize = headerPtr->uncompressed_size;
         
         uint64_t extraLen = headerPtr->file_name_length + headerPtr->file_comment_length + headerPtr->extra_field_length;
         headerPtr++;
         headerPtr = (CentralHeader*)(((uint8_t*)headerPtr) + extraLen);
      }
      
      for (Entry& e : mEntries)
      {
         std::string name(e.getFilename(&mCDData[0]));
         printf("File: %s\n", name.c_str());
      }

      return true;
   }

   static bool handleDeflate(MemRStream& inStream, MemRStream& outStream)
   {
   }
   
   bool openStream(std::ifstream& stream, const char* filename, MemRStream& outStream)
   {
      uint32_t fnLen = strlen(filename);
      for (std::vector<Entry>::const_iterator itr = mEntries.begin(), itrEnd = mEntries.end(); itr != itrEnd; itr++)
      {
         if (fnLen == itr->filenameSize &&
             strncasecmp(filename, itr->getFilenamePtr(&mCDData[0]), fnLen) == 0)
         {
            stream.seekg(itr->filenameOffset);
            if (stream.fail())
            {
               return false;
            }

            // Read past local entry
            LocalHeader lh = {};
            stream.read((char*)&lh, sizeof(LocalHeader));
            if (lh.signature != ZIP_LOCAL_FILE_HEADER_SIG)
            {
               return false;
            }

            uint64_t start = stream.tellg();
            stream.seekg(start + lh.file_name_length + lh.extra_field_length);
            if (stream.fail())
            {
               return false;
            }

            uint8_t* dataIn = (uint8_t*)malloc(itr->compressedSize);
            uint8_t* dataOut = NULL;

            stream.read((char*)dataIn, itr->compressedSize);
            if (stream.fail())
            {
               free(dataIn);
               return false;
            }

            if (itr->compression != 0)
            {
               // Validate compression
               dataOut = (uint8_t*)malloc(itr->uncompressedSize);
            }

            outStream = MemRStream(itr->uncompressedSize, dataOut ? dataOut : dataIn, true);
            if (dataIn != dataOut)
            {
               free(dataIn);
            }

            return true;
         }
      }
      
      return NULL;
   }
};

class ResManager
{
public:
   
   struct EnumEntry
   {
      std::string filename;
      uint32_t mountIdx;
      
      EnumEntry() {;}
      EnumEntry(const char* name, uint32_t m) : filename(name), mountIdx(m) {;}
      EnumEntry(std::string_view& name, uint32_t m) : filename(name), mountIdx(m) {;}
   };
   
   std::vector<Volume*> mVolumes;
   std::vector<std::string> mPaths;
   
   void addVolume(const char *filename)
   {
      std::ifstream file(filename, std::ios::binary);

      if (file.is_open())
      {
         Volume* vol = new Volume();
         if (!vol->read(file))
         {
            delete vol;
            return;
         }
         
         vol->mFile = std::move(file);
         vol->mName = filename;
         mVolumes.push_back(vol);
      }
   }
   
   bool openFile(const char *filename, MemRStream &stream, int32_t forceMount=-1)
   {
      // Check cwd
      int count = 0;
      for (std::string &path: mPaths)
      {
         if (forceMount >= 0 && count != forceMount)
         {
            count++;
            continue;
         }
         char buffer[PATH_MAX];
         snprintf(buffer, PATH_MAX, "%s/%s", path.c_str(), filename);
         std::ifstream file(filename, std::ios::binary | std::ios::ate);
         if (file.is_open())
         {
            uint64_t size = file.tellg();
            file.seekg(0);
            uint8_t* data = (uint8_t*)malloc(size);
            file.read((char*)data, size);
            
            if (!file.fail())
            {
               stream = MemRStream(size, data, true);
               file.close();
               return true;
            }
            free(data);
            file.close();
            return false;
         }
         count++;
      }
      
      // Scan volumes
      for (Volume* vol: mVolumes)
      {
         if (forceMount >= 0 && count != forceMount)
         {
            count++;
            continue;
         }
         if (vol->openStream(vol->mFile, filename, stream))
         {
            printf("Loaded volume file %s from volume\n", filename);
            return true;
         }
         count++;
      }
      
      return false;
   }
   
   void enumerateVolume(uint32_t idx, std::vector<EnumEntry> &outList, std::vector<std::string> *restrictExts)
   {
      for (Volume::Entry &e : mVolumes[idx]->mEntries)
      {
         if (restrictExts)
         {
            fs::path filePath = e.getFilename(mVolumes[idx]->getCDData());
            std::string  ext = filePath.extension();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            bool found = false;
            for (std::string &restrictExt : *restrictExts)
            {
               if (ext == restrictExt)
               {
                  found = true;
                  break;
               }
            }
            
            if (!found)
               continue;
         }
         
         std::string_view theName = e.getFilename(mVolumes[idx]->getCDData());
         outList.emplace_back(EnumEntry(theName, mPaths.size()+idx));
      }
   }
   
   void enumeratePath(uint32_t idx, std::vector<EnumEntry> &outList, std::vector<std::string> *restrictExts)
   {
      for (const fs::directory_entry &itr : fs::directory_iterator(mPaths[idx]))
      {
         if (restrictExts)
         {
            fs::path filePath = itr.path().filename().c_str();
            std::string  ext = filePath.extension();
            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
            
            bool found = false;
            for (std::string &restrictExt : *restrictExts)
            {
               if (ext == restrictExt)
               {
                  found = true;
                  break;
               }
            }
            
            if (!found)
               continue;
         }
         outList.emplace_back(EnumEntry(itr.path().filename().c_str(), idx));
      }
   }
   
   void enumerateFiles(std::vector<EnumEntry> &outList, int restrictIdx=-1, std::vector<std::string> *restrictExt=NULL)
   {
      for (int i=0; i<mPaths.size(); i++)
      {
         if (restrictIdx >= 0 && restrictIdx != i)
            continue;
         enumeratePath(i, outList, restrictExt);
      }
      for (int i=0; i<mVolumes.size(); i++)
      {
         if (restrictIdx >= 0 && restrictIdx != mPaths.size()+i)
            continue;
         enumerateVolume(i, outList, restrictExt);
      }
   }
   
   void enumerateSearchPaths(std::vector<const char*> &outList)
   {
      for (int i=0; i<mPaths.size(); i++)
      {
         outList.push_back(mPaths[i].c_str());
      }
      for (int i=0; i<mVolumes.size(); i++)
      {
         outList.push_back(mVolumes[i]->mName.c_str());
      }
   }
   
   const char *getMountName(uint32_t idx)
   {
      if (idx < mPaths.size())
         return mPaths[idx].c_str();
      idx -= mPaths.size();
      if (idx < mVolumes.size())
         return mVolumes[idx]->mName.c_str();
      return "NULL";
   }
};

// 16-bit quat type (same as torque)
struct Quat16
{
   enum { MAX_VAL = 0x7fff };
   
   int16_t x, y, z, w;
   
   Quat16() : x(0),y(0),z(0),w(0) {;}
   
   Quat16(const slm::quat &src)
   {
      x = src.x * float(MAX_VAL);
      y = src.y * float(MAX_VAL);
      z = src.z * float(MAX_VAL);
      w = src.w * float(MAX_VAL);
   }
   
   slm::quat toQuat() const
   {
      slm::quat outQuat;
      outQuat.x = float(x) / float(MAX_VAL);
      outQuat.y = float(y) / float(MAX_VAL);
      outQuat.z = float(z) / float(MAX_VAL);
      outQuat.w = float(w) / float(MAX_VAL);
      return outQuat;
   }
   
   bool operator==(const Quat16 &q) const { return( x == q.x && y == q.y && z == q.z && w == q.w ); }
   bool operator!=( const Quat16 & q ) const { return !(*this == q); }
};

void ConsolePersistObject::initStatics()
{
}


class GenericViewer
{
public:
   
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
   };
   
   std::vector<ActiveMaterial> mActiveMaterials;
   std::unordered_map<std::string, LoadedTexture> mLoadedTextures;
   ActiveMaterial mSharedMaterials;
   
   ResManager* mResourceManager;
   Palette* mPalette;
   MaterialList* mMaterialList;
   
   bool initVB;
   bool useShared;
   
   slm::mat4 mProjectionMatrix;
   slm::mat4 mModelMatrix;
   slm::mat4 mViewMatrix;
   
   slm::vec4 mLightColor;
   slm::vec3 mLightPos;
   
   GenericViewer() : mResourceManager(NULL), mPalette(NULL), mMaterialList(NULL)
   {
      useShared = false;
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
            // TOFIX loadTexture((const char*)mat.mFilename, amat.tex);
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
         if (mResourceManager->openFile(fname.c_str(), mem))
         {
            Bitmap* bmp = new Bitmap();
            if (!bmp->read(mem))
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
      if (mResourceManager->openFile(filename, mem))
      {
         Bitmap* bmp = new Bitmap();
         if (bmp->read(mem))
         {
            int32_t texID = GFXLoadTexture(bmp, mPalette);
            if (texID >= 0)
            {
               printf("Loaded texture %s dimensions %ix%i\n", filename, bmp->mWidth, bmp->mHeight);
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

class ShapeViewerController : public ViewController
{
public:
   //ShapeViewer mViewer;
   SDL_Window* mWindow;
   float xRot, yRot, mDetailDist;
   //Shape* mShape;
   int32_t mHighlightNodeIdx;
   
   std::vector<const char*> mSequenceList;
   std::vector<int> mNextSequence;
   
   int32_t mRemoveThreadId;
   bool mRenderNodes;
   bool mManualThreads;
   
   ShapeViewerController(SDL_Window* window, ResManager* mgr) /*:
   mViewer(mgr)*/
   {
      mViewPos = slm::vec3(0,0,0);
      mCamRot = slm::vec3(0,0,0);
      //mViewer.initRender();
      mWindow = window;
      xRot = mDetailDist = 0;
      yRot = slm::radians(180.0f);
      //mShape = NULL;
      mHighlightNodeIdx = -1;
      mRemoveThreadId = -1;
      mRenderNodes = true;
      mManualThreads = false;
   }
   
   ~ShapeViewerController()
   {
      #if 0
      if (mShape)
         delete mShape;
      #endif
   }
   
   bool isResourceLoaded()
   {
      return false;
      //return mShape != NULL;
   }
   
   void updateNextSequence()
   {
      #if 0
      mNextSequence.resize(mViewer.mThreads.size());
      for (int i=0; i<mViewer.mThreads.size(); i++)
      {
         mNextSequence[i] = mViewer.mThreads[i].sequenceIdx;
      }
      #endif
   }
   
   void loadShape(const char *filename, int pathIdx=-1)
   {
      #if 0
      MemRStream rStream(0, NULL);
      mViewer.clear();
      if (mShape)
         delete mShape;
      mShape = NULL;
      
      if (mViewer.mResourceManager->openFile(filename, rStream, pathIdx))
      {
         DarkstarPersistObject* obj = DarkstarPersistObject::createFromStream(rStream);
         if (obj)
         {
            mShape = ((Shape*)obj);
            mViewer.clear();
            if (!mViewer.setPalette(mPaletteName.c_str()))
            {
               printf("Warning: cant load palette %s\n", mPaletteName.c_str());
            }
            mViewer.loadShape(*mShape);
            
            uint32_t thr = mViewer.addThread();
            mViewer.setThreadSequence(thr, 0);
            
            mViewPos = slm::vec3(0, mViewer.mShape->mCenter.z, mViewer.mShape->mRadius);
            
            mSequenceList.resize(mShape->mSequences.size());
            updateNextSequence();
            
            for (int i=0; i<mViewer.mShape->mSequences.size(); i++)
            {
               mSequenceList[i] = mShape->getName(mShape->mSequences[i].name);
            }
         }
      }
      #endif
   }
   
   void update(float dt)
   {
      #if 0
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
      mViewer.animateNodes();
      mViewer.render();
      if (mRenderNodes)
      {
         mViewer.renderNodes(mShape->mDetails[mViewer.mCurrentDetail].rootNode, slm::vec3(0,0,0), mHighlightNodeIdx);
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
         mViewer.addThread();
         updateNextSequence();
      }
      
      ImGui::SameLine();
      ImGui::Checkbox("Manual Control", &mManualThreads);
      
      if (mRemoveThreadId >= 0)
      {
         mViewer.removeThread(mRemoveThreadId);
         mRemoveThreadId = -1;
      }
      
      for (ShapeViewer::ShapeThread &thread : mViewer.mThreads)
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
                     thread.sequenceIdx == -1 ? "NULL" : mShape->getName(mShape->mSequences[thread.sequenceIdx].name),
                     thread.pos);
         }
         
         ImGui::Text(buffer);
         
         if (vis)
         {
            snprintf(buffer, 1024, "Enabled##th%i", idx);
            ImGui::Checkbox(buffer, &mViewer.mThreads[idx].enabled);
            ImGui::SameLine();
            snprintf(buffer, 1024, "Remove##th%i", idx);
            if (ImGui::Button(buffer))
               mRemoveThreadId = idx;
            snprintf(buffer, 1024, "Pos##th%i", idx);
            ImGui::SliderFloat(buffer, &mViewer.mThreads[idx].pos, 0.0f, 1.0f);
            ImGui::NewLine();
            snprintf(buffer, 1024, "Sequences##th%i", idx);
            ImGui::ListBox(buffer, &mNextSequence[idx], &mSequenceList[0], mShape->mSequences.size());
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
      for (int i=0; i<mNextSequence.size(); i++)
      {
         if (mNextSequence[i] != mViewer.mThreads[i].sequenceIdx)
         {
            mViewer.setThreadSequence(i, mNextSequence[i]);
         }
      }
      #endif
   }
   
   void nodeTree(int32_t nodeIdx)
   {
      #if 0
      if (nodeIdx < 0)
         return;
      
      Shape::NodeChildInfo info = mShape->mNodeChildren[nodeIdx+1];
      uint32_t baseFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
      
      bool visDetail = (nodeIdx == mShape->mDetails[mViewer.mCurrentDetail].rootNode);
      
      if (visDetail)
      {
         ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0);
      }
      
      bool vis = ImGui::TreeNodeEx(mShape->getName(mShape->mNodes[nodeIdx].name), info.numChildren > 0 ? baseFlags : baseFlags|ImGuiTreeNodeFlags_Leaf);
      if (ImGui::IsItemClicked())
      {
         mHighlightNodeIdx = nodeIdx;
      }
      
      if (vis)
      {
         for (int32_t i=0; i<info.numChildren; i++)
         {
            nodeTree(mShape->mNodeChildIds[info.firstChild+i]);
         }
         ImGui::TreePop();
      }
      
      if (visDetail)
      {
         ImGui::PopStyleVar(1);
      }
      #endif
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


int main(int argc, const char * argv[])
{
   SDL_Window* window = NULL;
   SDL_Renderer* renderer = NULL;
   
   assert(sizeof(slm::vec2) == 8);
   assert(sizeof(slm::vec3) == 12);
   assert(sizeof(slm::vec4) == 16);
   
   ConsolePersistObject::initStatics();
   
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
      ImGui::ListBox("##bvols", &selectedVolumeIdx, &cVolumeList[0], cVolumeList.size());
      ImGui::NextColumn();
      ImGui::ListBox("##bfiles", &selectedFileIdx, &cFileList[0], cFileList.size());
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
