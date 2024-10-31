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

#ifndef _COMMONDATA_H_
#define _COMMONDATA_H_

#include <algorithm>
#include <functional>
#include <slm/slmath.h>
#include <string>

struct _LineVert
{
   slm::vec3 pos;
   slm::vec3 nextPos;
   slm::vec3 normal;
   slm::vec4 color;
};

inline uint32_t getNextPow2(uint32_t a)
{
   a--;
   a |= a >> 1;
   a |= a >> 2;
   a |= a >> 4;
   a |= a >> 8;
   a |= a >> 16;
   return a + 1;
}

class MemRStream;

class ConsolePersistObject
{
public:
   
   virtual ~ConsolePersistObject(){;}
   
   typedef std::unordered_map<uint32_t, std::function<ConsolePersistObject*()> > IDFuncMap;
   typedef std::unordered_map<std::string, std::function<ConsolePersistObject*()> > NamedFuncMap;
   static IDFuncMap smIDCreateFuncs;
   static NamedFuncMap smNamedCreateFuncs;
   static void initStatics();
   
   template<class T> static ConsolePersistObject* _createClass() { return new T(); }
   static void registerClass(std::string className, std::function<ConsolePersistObject*()> func)
   {
      smNamedCreateFuncs[className] = func;
   }
   
   static ConsolePersistObject* createClassByName(std::string name)
   {
      NamedFuncMap::iterator itr = smNamedCreateFuncs.find(name);
      if (itr != smNamedCreateFuncs.end())
      {
         return itr->second();
      }
      return NULL;
   }
   
   static ConsolePersistObject* createClassById(uint32_t tag)
   {
      IDFuncMap::iterator itr = smIDCreateFuncs.find(tag);
      if (itr != smIDCreateFuncs.end())
      {
         return itr->second();
      }
      return NULL;
   }
};

class MemRStream
{
public:
   uint64_t mPos;
   uint64_t mSize;
   uint8_t* mPtr;
   
   bool mOwnPtr;
   
   MemRStream(uint64_t sz, void* ptr, bool ownPtr=false) : mPos(0), mSize(sz), mPtr((uint8_t*)ptr), mOwnPtr(ownPtr) {;}
   MemRStream(MemRStream &&other)
   {
      mPtr = other.mPtr;
      mPos = other.mPos;
      mSize = other.mSize;
      other.mOwnPtr = false;
      mOwnPtr = true;
   }
   MemRStream(MemRStream &other)
   {
      mPtr = other.mPtr;
      mPos = other.mPos;
      mSize = other.mSize;
      other.mOwnPtr = false;
      mOwnPtr = true;
   }
   MemRStream& operator=(MemRStream other)
   {
      mPtr = other.mPtr;
      mPos = other.mPos;
      mSize = other.mSize;
      other.mOwnPtr = false;
      mOwnPtr = true;
      return *this;
   }
   ~MemRStream()
   {
      if(mOwnPtr)
         free(mPtr);
   }
   
   // For array types
   template<class T, int N> inline bool read( T (&value)[N] )
   {
      if (mPos >= mSize || mPos+(sizeof(T)*N) > mSize)
         return false;
      
      memcpy(&value, mPtr+mPos, sizeof(T)*N);
      mPos += sizeof(T)*N;
      
      return true;
   }
   
   // For normal scalar types
   template<typename T> inline bool read(T &value)
   {
      T* tptr = (T*)(mPtr+mPos);
      if (mPos >= mSize || mPos+sizeof(T) > mSize)
         return false;
      
      value = *tptr;
      mPos += sizeof(T);
      
      return true;
   }
   
   inline bool read(uint64_t size, void* data)
   {
      if (mPos >= mSize || mPos+size > mSize)
         return false;
      
      memcpy(data, mPtr+mPos, size);
      mPos += size;
      
      return true;
   }
   
   inline bool readSString(std::string &outS)
   {
      uint16_t size;
      if (!read(size)) return false;
      
      int real_size = (size + 1) & (~1); // dword padded
      char *str = new char[real_size+1];
      if (read(real_size, str))
      {
         str[real_size] = '\0';
         outS = str;
         delete[] str;
         return true;
      }
      else
      {
         delete[] str;
         return false;
      }
   }
   
   inline bool readSString32(std::string &outS)
   {
      uint32_t size;
      if (!read(size)) return false;
      
      char *str = new char[size+1];
      if (read(size, str))
      {
         str[size] = '\0';
         outS = str;
         delete[] str;
         return true;
      }
      else
      {
         delete[] str;
         return false;
      }
   }
   
   // WRITE
   
   // For array types
   template<class T, int N> inline bool write( T (&value)[N] )
   {
      if (mPos >= mSize || mPos+(sizeof(T)*N) > mSize)
         return false;
      
      memcpy(mPtr+mPos, &value, sizeof(T)*N);
      mPos += sizeof(T)*N;
      
      return true;
   }
   
   // For normal scalar types
   template<typename T> inline bool write(T &value)
   {
      T* tptr = (T*)(mPtr+mPos);
      if (mPos >= mSize || mPos+sizeof(T) > mSize)
         return false;
      
      *tptr = value;
      mPos += sizeof(T);
      
      return true;
   }
   
   inline bool write(uint64_t size, void* data)
   {
      if (mPos >= mSize || mPos+size > mSize)
         return false;
      
      memcpy(mPtr+mPos, data, size);
      mPos += size;
      
      return true;
   }
   
   inline bool writeSString(std::string &outS)
   {
      uint16_t size = (uint16_t)outS.size();
      if (!write(size)) return false;
      
      int real_size = (size + 1) & (~1); // dword padded
      char *str = new char[real_size+1];
      if (read(real_size, str))
      {
         str[real_size] = '\0';
         outS = str;
         delete[] str;
         return true;
      }
      else
      {
         delete[] str;
         return false;
      }
   }
   
   inline void setPosition(uint32_t pos)
   {
      if (pos > mSize)
         return;
      mPos = pos;
   }
   
   inline uint64_t getPosition() { return mPos; }
   
   inline bool isEOF() { return mPos >= mSize; }
};

class IFFBlock
{
public:
   enum
   {
      ALIGN_DWORD = 0x80000000
   };
   
   uint32_t ident;
protected:
   uint32_t size;
   
public:
   IFFBlock() : ident(0), size(0) {;}
   
   inline uint32_t getSize() const
   {
      if (size & ALIGN_DWORD)
         return ((size & ~ALIGN_DWORD) + 3) & ~3;
      else
         return ( (size + 1) & (~1) );
   }
   
   inline uint32_t getRawSize() const { return size; }
   
   inline void seekToEnd(uint32_t startPos, MemRStream &mem)
   {
      mem.setPosition(startPos + getSize() + 8);
   }
};

class Palette
{
public:
   enum
   {
      IDENT_PPAL = 1279348816,
      IDENT_PAL = 541868368,
      IDENT_RIFF = 1179011410,
   };
   
   enum Format : uint32_t
   {
      FORMAT_RGB=0,
      FORMAT_RGBA=1,
   };
   
   // Palette entry
   struct Data
   {
      Format type;
      uint32_t colors[256];
      
      Data() : type(FORMAT_RGB) {;}
      inline void lookupRGB(uint8_t idx, uint8_t &outR, uint8_t &outG, uint8_t &outB)
      {
         uint32_t col = colors[idx];
         outR = col & 0xFF;
         outG = (col >> 8) & 0xFF;
         outB = (col >> 16) & 0xFF;
      }
      inline void lookupRGBA(uint8_t idx, uint8_t &outR, uint8_t &outG, uint8_t &outB, uint8_t &outA)
      {
         uint32_t col = colors[idx];
         outR = col & 0xFF;
         outG = (col >> 8) & 0xFF;
         outB = (col >> 16) & 0xFF;
         outA = (col >> 24) & 0xFF;
      }
   };
   
   Data mData;
   
   Palette();
   ~Palette();
   
   bool readMSPAL(MemRStream& mem);
   
   bool read(MemRStream& mem);
   
   inline Data* getPalette() { return &mData; }
};

class Bitmap
{
public:
   
   enum
   {
      MAX_MIPS = 10
   };
   
   enum Format : uint32_t
   {
      FORMAT_PAL=0,
      FORMAT_INTENSITY=1,
      FORMAT_RGB=2,
      FORMAT_RGBA=3,
      FORMAT_ALPHA=4,
      FORMAT_RGB_565=5,
      FORMAT_RGBA_5551=6,
      FORMAT_LUMINANCE=7
   };
   
   uint32_t mWidth;
   uint32_t mHeight;
   Format mFormat;
   uint32_t mBitDepth;
   uint32_t mStride;
   
   uint32_t mMipLevels;
   
   uint8_t* mData;
   uint8_t* mMips[MAX_MIPS];
   
   Palette* mPal;
   
   Bitmap();
   ~Bitmap();
   
   void reset();
   
   bool readStbi(MemRStream& mem);
   bool readBM8(MemRStream& mem);
   bool read(MemRStream& mem);
   
   static int stbi_read_callback(void *user, char *data, int size);
   static void stbi_skip_callback(void *user, int n);
   static int stbi_eof_callback(void *user);
   
   
   inline uint32_t getStride(uint32_t width) const { return 4 * ((width * mBitDepth + 31)/32); }
   
   inline uint8_t* getAddress(uint32_t mip, uint32_t x, uint32_t y)
   {
      assert(mip == 0);
      uint32_t stride = getStride(mWidth);
      return mMips[mip] + ((stride * y) + ((mBitDepth * x) / 8));
   }
};


inline void copyMipDirect(uint32_t height, uint32_t src_stride, uint32_t dest_stride, uint8_t* data, uint8_t* out_data)
{
   for (int y=0; y<height; y++)
   {
      uint8_t *srcPixels = data + (y*src_stride);
      uint8_t *destPixels = out_data + (y*dest_stride);
      memcpy(destPixels, srcPixels, src_stride);
   }
}

inline void copyLMMipDirect(uint32_t height, uint32_t src_stride, uint32_t dest_stride, uint8_t* data, uint8_t* out_data)
{
   for (uint32_t y = 0; y < height; y++)
   {
      uint16_t* srcPixels = (uint16_t*)(data + y * src_stride);
      uint32_t* destPixels = (uint32_t*)(out_data + y * dest_stride);
      
      for (uint32_t x = 0; x < src_stride / 2; x++)
      {
         uint16_t irgb4444 = srcPixels[x];
         
         uint32_t i = (irgb4444 >> 12) & 0xF;
         uint32_t r = (irgb4444 >> 8) & 0xF;
         uint32_t g = (irgb4444 >> 4) & 0xF;
         uint32_t b = irgb4444 & 0xF;
         
         // TODO: verify this is actually correct; shadows seem too dark.
         float im = ((float)i / 15.0);
         r = ((float)r / 15.0) * im * 255;
         g = ((float)g / 15.0) * im * 255;
         b = ((float)b / 15.0) * im * 255;
         
         destPixels[x] = (0xFF << 24) | (b << 16) | (g << 8) | r;
      }
   }
}

inline void copyMipDirectPadded2(uint32_t height, uint32_t src_stride, uint32_t dest_stride, uint8_t* data, uint8_t* out_data)
{
   for (int y=0; y<height; y++)
   {
      uint8_t *srcPixels = data + (y*src_stride);
      uint8_t *destPixels = out_data + (y*dest_stride);
      for (int x=0; x<src_stride; x+=2)
      {
         *destPixels++ = *srcPixels++;
         *destPixels++ = *srcPixels++;
      }
   }
}

inline void copyMipDirectPadded(uint32_t height, uint32_t src_stride, uint32_t dest_stride, uint8_t* data, uint8_t* out_data)
{
   for (int y=0; y<height; y++)
   {
      uint8_t *srcPixels = data + (y*src_stride);
      uint8_t *destPixels = out_data + (y*dest_stride);
      for (int x=0; x<src_stride; x+=3)
      {
         *destPixels++ = *srcPixels++;
         *destPixels++ = *srcPixels++;
         *destPixels++ = *srcPixels++;
         *destPixels++ = 255;
      }
   }
}


inline void copyMipRGB(uint32_t width, uint32_t height, uint32_t pad_width, Palette::Data* pal, uint8_t* data, uint8_t* out_data)
{
   for (int y=0; y<height; y++)
   {
      uint8_t *srcPixels = data + (y*width);
      uint8_t *destPixels = out_data + (y*pad_width);
      for (int x=0; x<width; x++)
      {
         uint8_t r,g,b;
         pal->lookupRGB(srcPixels[x], r,g,b);
         *destPixels++ = r;
         *destPixels++ = g;
         *destPixels++ = b;
      }
   }
}

inline void copyMipRGBA(uint32_t width, uint32_t height, uint32_t pad_width, Palette::Data* pal, uint8_t* data, uint8_t* out_data, uint32_t clamp_a)
{
   for (int y=0; y<height; y++)
   {
      uint8_t *srcPixels = data + (y*width);
      uint8_t *destPixels = out_data + (y*pad_width);
      for (int x=0; x<width; x++)
      {
         uint8_t r,g,b,a;
         pal->lookupRGBA(srcPixels[x], r,g,b,a);
         *destPixels++ = r;
         *destPixels++ = g;
         *destPixels++ = b;
         *destPixels++ = std::min((uint32_t)a * clamp_a, (uint32_t)255);
      }
   }
}



#endif /* SharedRender_h */
