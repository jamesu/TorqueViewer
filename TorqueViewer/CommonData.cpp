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
   for (uint32_t i=0; i<mMaterials.size(); i++)
   {
      loadMaterial(i, path);
      
      uint32_t th = mMaterials[i].texID;
      const char* name = mMaterials[i].name.c_str();
      if (name && *name && th > 0)
         return false;
   }
   
   return true;
}
