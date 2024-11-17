//-----------------------------------------------------------------------------
// Copyright (c) 2018 James S Urquhart.
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
#include <stdio.h>
#include <strings.h>
#include <algorithm>
#include <string>
#include <vector>
#include <cmath>
#include <climits>
#include <unordered_map>
#include <iostream>
#include <fstream>
#include <slm/slmath.h>
#include "CommonData.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

ConsolePersistObject::NamedFuncMap ConsolePersistObject::smNamedCreateFuncs;
ConsolePersistObject::IDFuncMap ConsolePersistObject::smIDCreateFuncs;

Palette::Palette()
{
}

Palette::~Palette()
{
}

bool Palette::readMSPAL(MemRStream& mem)
{
   IFFBlock block;
   mem.read(block);
   
   if (block.ident != IDENT_RIFF)
   {
      return false;
   }
   
   mem.read(block);
   if (block.ident != IDENT_PAL)
   {
      return false;
   }
   
   uint16_t numColors, version;
   mem.read(numColors);
   mem.read(version);
   
   Data& lp = mData;
   memset(lp.colors, '\0', sizeof(lp.colors));
   lp.type = FORMAT_RGBA;
   
   uint32_t colsToRead = std::min<uint32_t>(numColors, 256);
   mem.read(colsToRead*4, lp.colors);
   mem.mPos += (numColors - colsToRead) * 4;
   
   return true;
}

bool Palette::read(MemRStream& mem)
{
   IFFBlock block;
   mem.read(block);
   if (block.ident == IDENT_RIFF)
   {
      mem.setPosition(0);
      return readMSPAL(mem);
   }
   else
   {
      uint32_t version = block.ident;
      if (version != 1)
      {
         return false;
      }
      
      mem.read(mData.type);
      mem.read(256 * 4, mData.colors);
   }
   
   return true;
}

Bitmap::Bitmap() : mData(NULL), mPal(NULL)
{;}

Bitmap::~Bitmap()
{
   reset();
}

void Bitmap::reset()
{
   if (mData) free(mData);
   if (mPal) delete mPal;
   mMipLevels = 0;
}

// Callback to read data from the memory stream
int Bitmap::stbi_read_callback(void *user, char *data, int size)
{
   MemRStream *stream = (MemRStream*)user;
   
   if (stream->mPos + size > stream->mSize)
   {
      size = stream->mSize - stream->mPos;
   }
   
   stream->read(size, data);
   return size;
}

// Callback to skip bytes in the memory stream
void Bitmap::stbi_skip_callback(void *user, int n)
{
   MemRStream *stream = (MemRStream*)user;
   
   if (stream->mPos + n > stream->mSize)
   {
      stream->mPos = stream->mSize;
   }
   else
   {
      stream->mPos += n;
   }
}

// Callback to check if we've reached the end of the memory stream
int Bitmap::stbi_eof_callback(void *user)
{
   MemRStream *stream = (MemRStream *)user;
   return stream->mPos >= stream->mSize;
}

bool Bitmap::readStbi(MemRStream& mem)
{
   int width = 0;
   int height = 0;
   int channels = 0;
   
   stbi_io_callbacks callbacks;
   callbacks.read = stbi_read_callback;
   callbacks.skip = stbi_skip_callback;
   callbacks.eof = stbi_eof_callback;
   
   reset();
   
   uint8_t *image_data = stbi_load_from_callbacks(&callbacks, &mem, &width, &height, &channels, 0);
   if (image_data == NULL)
   {
      return false;
   }
   
   switch (channels)
   {
      case 1:
         mFormat = FORMAT_LUMINANCE;
         mBitDepth = 8;
         mStride = width;
         break;
      case 2:
         return false;
         break;
      case 3:
         mFormat = FORMAT_RGB;
         mBitDepth = 24;
         mStride = width * 3;
         break;
      case 4:
         mFormat = FORMAT_RGBA;
         mBitDepth = 32;
         mStride = width * 4;
         break;
      default:
         return false;
   }
   
   mWidth = width;
   mHeight = height;
   mMipLevels = 1;
   mMips[0] = &mData[0];
   
   return true;
}

bool Bitmap::readBM8(MemRStream& mem)
{
   uint32_t byteSize = 0;
   uint32_t bpp = 0;
   
   reset();
   
   mem.read(byteSize);
   mem.read(mWidth);
   mem.read(mHeight);
   mem.read(bpp);
   mem.read(mMipLevels);
   
   if (mWidth == 0 || mHeight == 0 || bpp != 1 || mMipLevels == 0)
      return false;
   
   mFormat = FORMAT_PAL;
   mBitDepth = 8;
   
   for (uint32_t i=0; i<mMipLevels; i++)
   {
      mem.read(mMips[i]);
   }
   
   mPal = new Palette();
   if (!mPal->read(mem))
      return false;
   
   mStride = byteSize / mHeight;
   mData = (uint8_t*)malloc(byteSize);
   mem.read(byteSize, mData);
   
   return true;
}

bool Bitmap::read(MemRStream& mem)
{
   // NOTE: Not seen anything actually use this, but it's here
   // just in case.
   uint32_t version = 0;
   uint32_t byteSize = 0;
   bool needPal = false;
   
   reset();
   
   mem.read(version);
   
   if (version != 1)
      return false;
   
   mem.read(mFormat);
   mem.read(mWidth);
   mem.read(mHeight);
   mem.read(byteSize);
   
   if (mWidth == 0 || mHeight == 0)
      return false;
   
   mStride = byteSize / mHeight;
   mData = (uint8_t*)malloc(byteSize);
   mem.read(byteSize, mData);
   mem.read(mMipLevels);
   for (uint32_t i=0; i<mMipLevels; i++)
   {
      mem.read(mMips[i]);
   }
   
   switch (mFormat)
   {
      case FORMAT_PAL:
      case FORMAT_INTENSITY:
      case FORMAT_ALPHA:
         mBitDepth = 8;
      case FORMAT_RGB_565:
      case FORMAT_RGBA_5551:
         mBitDepth = 16;
         break;
      case FORMAT_RGB:
         mBitDepth = 24;
         break;
      case FORMAT_RGBA:
         mBitDepth = 32;
         break;
   }
   
   if (needPal)
   {
      mPal = new Palette();
      if (!mPal->read(mem))
         return false;
   }
   
   return true;
}

//


MaterialList::MaterialList() : 
   mTextureType(0),
   mClampToEdge(false),
   mNamesTransformed(false),
   mVariant(VARIANT_NORMAL)
{
}

MaterialList::MaterialList(uint32_t materialCount, const char** materialNames) :
mTextureType(0),
mClampToEdge(false),
mNamesTransformed(false),
mVariant(VARIANT_NORMAL)
{  
   mMaterials.reserve(materialCount);
   
   for (uint32_t i=0; i<materialCount; i++)
   {
      push_back(materialNames[i]);
   }
}

MaterialList::MaterialList(const MaterialList* other) :
mTextureType(other->mTextureType),
mClampToEdge(other->mClampToEdge),
mNamesTransformed(other->mNamesTransformed),
mVariant(other->mVariant)
{
   mMaterials = other->mMaterials;
}

MaterialList::~MaterialList()
{
   free();
}

void MaterialList::load()
{
   loadFromPath(NULL);
}

void MaterialList::loadMaterial(uint32_t index, const char* path)
{
   // TODO
}

bool MaterialList::load(uint32_t type, const char* path, bool clampToEdge)
{
   mTextureType = type;
   mClampToEdge = clampToEdge;
   return loadFromPath(path);
}

void MaterialList::push_back(const char* name, Material* props)
{
   Material mat = {};
   if (props)
   {
      mat = *props;
   }
   mat.name = name;
   mMaterials.push_back(mat);
}

bool MaterialList::isBlank(uint32_t index)
{
   if (index >= mMaterials.size())
      return true;
   return mMaterials[index].name[0] == '\0';
}

void MaterialList::free()
{
   mMaterials.clear();
}

bool MaterialList::read(MemRStream& s)
{
   free();

   uint8_t versionNum = 0;
   uint32_t numMaterials = 0;
   char buffer[1024];
   buffer[0] = '\0';
   
   s.read(versionNum);
   
   if (versionNum != BINARY_FILE_VERSION)
   {
      if (mVariant == MaterialList::VARIANT_NORMAL &&
          isalnum(versionNum))
         return parseFromStream(s);
      else
         return false;
   }

   if (mVariant == VARIANT_NORMAL)
   {
      s.read(numMaterials);
      mMaterials.reserve(numMaterials);
      for (uint32_t i=0; i<numMaterials; i++)
      {
         std::string tmp;
         s.readS8String(tmp);
         
         // paths need to be stripped off even in binary streams
         char* toolPath = stripToolPath(buffer);
         
         push_back(toolPath);
      }
   }
   else if (mVariant == VARIANT_TS)
   {
      s.read(numMaterials);
      mMaterials.reserve(numMaterials);

      for (int32_t i = 0; i < numMaterials; i++)
      {
         uint8_t nameLen;
         s.read(nameLen);
         
         std::string name(nameLen, '\0');
         s.read(nameLen, &name[0]);
         
         mMaterials.push_back(Material(name.c_str()));
      }
      
      uint32_t tmp = 0;
      
      for (auto& mat : mMaterials)
      {
         s.read(tmp);
         mat.tsProps.flags = tmp;
      }
      for (auto& mat : mMaterials)
      {
         s.read(tmp);
         mat.tsProps.reflectanceMap = tmp;
      }
      for (auto& mat : mMaterials)
      {
         s.read(tmp);
         mat.tsProps.bumpMap = tmp;
      }
      for (auto& mat : mMaterials)
      {
         s.read(tmp);
         mat.tsProps.detailMap = tmp;
      }
      for (auto& mat : mMaterials)
      {
         s.read(mat.tsProps.detailScale);
      }
      for (auto& mat : mMaterials)
      {
         s.read(mat.tsProps.reflectionAmount);
      }
   }
   else
   {
      return false;
   }
   
   return true;
}

bool MaterialList::write(MemRStream& s)
{
   if (mVariant == VARIANT_NORMAL)
   {
      s.write((uint8_t)BINARY_FILE_VERSION);
      s.write((uint32_t)mMaterials.size());
      for (Material& mat : mMaterials)
      {
         s.writeS8String(mat.name);
      }
   }
   else if (mVariant == VARIANT_TS)
   {
      s.write((uint8_t)BINARY_FILE_VERSION);
      uint32_t size = static_cast<uint32_t>(mMaterials.size());
      s.write(size);
      
      for (const auto& mat : mMaterials)
      {
         s.writeS8String(mat.name);
      }
      
      for (auto& mat : mMaterials)
      {
         s.write(mat.tsProps.flags);
      }
      for (auto& mat : mMaterials)
      {
         s.write(mat.tsProps.reflectanceMap);
      }
      for (auto& mat : mMaterials)
      {
         s.write(mat.tsProps.bumpMap);
      }
      for (auto& mat : mMaterials)
      {
         s.write(mat.tsProps.detailMap);
      }
      for (auto& mat : mMaterials)
      {
         s.write(mat.tsProps.detailScale);
      }
      for (auto& mat : mMaterials)
      {
         s.write(mat.tsProps.reflectionAmount);
      }
   }
   else
   {
      return false;
   }
   
   return true;
}

bool MaterialList::parseFromStream(MemRStream& s)
{
   char buffer[1024];
   buffer[0] = '\0';
   s.setPosition(0);
   
   do
   {
      s.readLine((uint8_t*)buffer, sizeof(buffer));
      if (!buffer[0])
         break;
      
      char* toolPath = stripToolPath(buffer);
      push_back(toolPath);
      
      if (s.isEOF())
         return true;
      
   } while (!s.isEOF());
   
   return false;
}

bool MaterialList::loadFromPath(const char* path)
{
#if 0
   for (uint32_t i=0; i<mMaterials.size(); i++)
   {
      loadMaterial(i, path);
      
      uint32_t th = mMaterials[i].texID;
      const char* name = mMaterials[i].name.c_str();
      if (name && *name && th > 0)
         return false;
   }
#endif
   
   return true;
}

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
         e.dataOffset = headerPtr->local_header_offset;
         e.compressedSize = headerPtr->compressed_size;
         e.uncompressedSize = headerPtr->uncompressed_size;
         
         uint64_t extraLen = headerPtr->file_name_length + headerPtr->file_comment_length + headerPtr->extra_field_length;
         headerPtr++;
         headerPtr = (CentralHeader*)(((uint8_t*)headerPtr) + extraLen);
         
         const uint8_t* extraPtr = (((uint8_t*)headerPtr) + headerPtr->file_name_length + headerPtr->file_comment_length);
         const uint8_t* endPtr = extraPtr + headerPtr->extra_field_length;
         
         while (extraPtr < endPtr)
         {
            uint16_t type = ((uint16_t*)extraPtr)[0];
            uint16_t size = ((uint16_t*)extraPtr)[1];
            extraPtr += 4;
            
            if (type == 0x0001)
            {
               uint64_t value = ((uint16_t*)extraPtr)[0];
               uint64_t ofs = 0;
               if (headerPtr->compressed_size == 0xffffffff)
               {
                  e.compressedSize = ((uint64_t*)headerPtr)[ofs++];
               }
               if (headerPtr->uncompressed_size == 0xffffffff)
               {
                  e.uncompressedSize = ((uint64_t*)headerPtr)[ofs++];
               }
               if (headerPtr->local_header_offset == 0xffffffff)
               {
                  e.dataOffset = ((uint64_t*)headerPtr)[ofs++];
               }
            }
            
            extraPtr += size;
         }
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
            stream.seekg(itr->dataOffset);
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
            uint64_t realStart = start + lh.file_name_length + lh.extra_field_length;
            
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
               if (itr->compression != 8)
               {
                  free(dataIn);
                  return false;
               }
               
               // Validate compression
               dataOut = (uint8_t*)malloc(itr->uncompressedSize);
               
               if (itr->compression == 8)
               {
                  // TODO: 64bit
                  if (stbi_zlib_decode_noheader_buffer((char*)dataOut, itr->uncompressedSize, (char*)dataIn, itr->compressedSize) < 0)
                  {
                     free(dataIn);
                     free(dataOut);
                     return false;
                  }
               }
               
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



std::unordered_map<std::string, ResManager::CreateFunc> ResManager::smCreateFuncs;

void ResManager::registerCreateFunc(const char* ext, CreateFunc func)
{
   smCreateFuncs[ext] = func;
}

void ResManager::addVolume(const char *filename)
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
      
      file.clear();
      vol->mFile = std::move(file);
      bool didFail = vol->mFile.fail();
      assert(!didFail);
      vol->mName = filename;
      mVolumes.push_back(vol);
   }
}

bool ResManager::openFile(const char *filename, MemRStream &stream, int32_t forceMount)
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

void ResManager::enumerateVolume(uint32_t idx, std::vector<EnumEntry> &outList, std::vector<std::string> *restrictExts)
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

void ResManager::enumeratePath(uint32_t idx, std::vector<EnumEntry> &outList, std::vector<std::string> *restrictExts)
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

void ResManager::enumerateFiles(std::vector<EnumEntry> &outList, int restrictIdx, std::vector<std::string> *restrictExt)
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

void ResManager::enumerateSearchPaths(std::vector<const char*> &outList)
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

const char *ResManager::getMountName(uint32_t idx)
{
   if (idx < mPaths.size())
      return mPaths[idx].c_str();
   idx -= mPaths.size();
   if (idx < mVolumes.size())
      return mVolumes[idx]->mName.c_str();
   return "NULL";
}

ResourceInstance* ResManager::createResource(const char *filename, int32_t forceMount)
{
   MemRStream stream;
   const char* ext = strrchr(filename, '.');
   if (!ext)
      return NULL;
   
   auto itr = smCreateFuncs.find(ext);
   if (itr == smCreateFuncs.end())
      return NULL;
   
   if (openFile(filename, stream, forceMount))
   {
      ResourceInstance* inst = itr->second();
      if (!inst->read(stream))
      {
         delete inst;
         return NULL;
      }
      
      return inst;
   }
   
   return NULL;
}
