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
