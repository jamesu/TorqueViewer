
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
        return false;
    }

    template<typename T> static bool writeShape(Shape* shape, T& ds, uint32_t version)
    {
        return false;
    }
   
   template<typename T> static bool readBox(T& ds, Box& box)
   {
       return false;
   }

   template<typename T> static bool writeBox(T& ds, const Box& box)
   {
       return false;
   }
   
   template<typename T> static bool readPoint2F(T& ds, slm::vec2& box)
   {
       return false;
   }

   template<typename T> static bool writePoint2F(T& ds, const slm::vec2& box)
   {
       return false;
   }
   
   template<typename T> static bool readPoint3F(T& ds, slm::vec3& box)
   {
       return false;
   }
   
   template<typename T> static bool writePoint3F(T& ds, const slm::vec3& box)
   {
       return false;
   }

   template<typename T> static bool writePoint4F(T& ds, const slm::vec4& box)
   {
       return false;
   }
   
   template<typename T> static bool readPoint4F(T& ds, slm::vec4& box)
   {
       return false;
   }
   
   template<typename T> static bool readMatrixF(T& ds, slm::mat4& box)
   {
       return false;
   }

   template<typename T> static bool writeMatrixF(T& ds, slm::mat4& box)
   {
       return false;
   }
   
   template<typename T> static bool readPrimitive(T& ds, Primitive& box)
   {
       return false;
   }

   template<typename T> static bool writePrimitive(T& ds, const Primitive& box)
   {
       return false;
   }
   
   template<typename T> static bool readCluster(T& ds, Cluster& box)
   {
       return false;
   }

   template<typename T> static bool writeCluster(T& ds, const Cluster& box)
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
           skinData = new SkinData();
           basicData = dynamic_cast<BasicData*>(skinData);
        }
        else if (mesh->mType != Mesh::T_Decal)
        {
           basicData = new BasicData();
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
                for (slm::vec3& vert : basicData->normals)
                {
                    uint8_t val = 0;
                    ds.read(val); // TODO: important?
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
                for (slm::vec3& vert : basicData->normals)
                {
                    uint8_t val = 0;
                    ds.read(val); // TODO: important?
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
                for (int i = 0; i < 3; ++i) {
                    ds.reads32();
                }
            }

            ds.readCheck();
        }
        else if (mesh->mType == Mesh::T_Decal)
        {
           DecalData* decalData = new DecalData;
           mesh->mData = decalData;
           
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
            SortedData* sortedData = new SortedData;
            mesh->mData = sortedData;
           /* TOFIX
           
            int sz = ds.reads32();
            for (int cnt = 0; cnt < sz; ++cnt) {
                clusters.push_back(ds.readCluster());
            }
            sz = ds.reads32();
            for (int cnt = 0; cnt < sz; ++cnt) {
                startCluster.push_back(ds.reads32());
            }
            int nfv = ds.reads32();
            for (int cnt = 0; cnt < sz; ++cnt) {
                firstVerts.push_back(ds.reads32());
            }
            sz = ds.reads32();
            for (int cnt = 0; cnt < sz; ++cnt) {
                numVerts.push_back(ds.reads32());
            }
            sz = ds.reads32();
            for (int cnt = 0; cnt < sz; ++cnt) {
                firstTVerts.push_back(ds.reads32());
            }
            alwaysWriteDepth = ds.readu32();
            ds.readCheck();*/
        }
        else
        {
            assert(false);
        }
    }

    template<typename T> static bool writeMesh(Mesh* mesh, Shape* shape, T& ds, uint32_t version)
    {
        return false;
    }
};

// NOTE: this is a bit messy

struct BasicStream
{
   MemRStream stream;

    void writeHeader(MemRStream& destStream, uint32_t dtsVersion)
    {
        uint32_t hdr[4];
        hdr[0] = 861099076;
        hdr[1] = dtsVersion | (IO::EXPORTER_VERSION << 16);
        hdr[2] = 0;
        hdr[3] = 0;

        destStream.write(sizeof(hdr), hdr);
    }

    bool readHeader(MemRStream& srcStream)
    {
       uint32_t hdr[4] = {};
       srcStream.read(sizeof(hdr), hdr);
    }

    bool readCheck()
    {
        return true;
    }

   void writeCheck()
   {
   }
};

struct SplitStream
{
    static constexpr int32_t EXPORTER_VERSION = 1;
    
    MemRStream buffer32;
    MemRStream buffer16;
    MemRStream buffer8;

    uint32_t dtsVersion;
    uint32_t checkCount;

   SplitStream()
    {
    }

    void flushToStream(MemRStream& destStream, uint32_t dtsVersion)
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

        int32_t ver = hdr[0];
        int32_t totalSize = hdr[1];
        int32_t offset16 = hdr[2];
        int32_t offset8 = hdr[3];

        int32_t allocated32 = offset16;
        int32_t allocated16 = (offset8 - offset16) * 2;
        int32_t allocated8 = (totalSize - offset8) * 4;

        buffer32.setOffsetView(sourceStream, 0, allocated32 * sizeof(int32_t));
        buffer16.setOffsetView(sourceStream, 0, allocated16 * sizeof(int16_t));
        buffer8.setOffsetView(sourceStream, 0, allocated8);

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

        buffer8.read(c8);
        buffer16.read(c16);
        buffer32.read(c32);
        
        if (c8 == checkCount && 
            c16 == checkCount && 
            c32 == checkCount) {
            checkCount++;
            return true;
        }

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
    if (sizeof(T) == 32)
    {
        return buffer32.read(value);
    }
    else if (sizeof(T) == 16)
    {
        return buffer16.read(value);
    }
    else if (sizeof(T) == 8)
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
};

}
