#include "NL/nlBundleFile.h"
#include "port/endian.h"
#include "dolphin/os.h"
#include <stdlib.h>
#include "NL/nlMemory.h"
#include "NL/nlString.h"
#include <stdint.h>
#include <string.h>

static bool BundleFileRange(u32 blockNumber, u32 sectorSize, u32 length,
                            unsigned int fileSize, unsigned int* offsetOut)
{
    if (sectorSize == 0)
        return false;

    const uint64_t offset = (uint64_t)blockNumber * (uint64_t)sectorSize;
    if (offset > fileSize || (uint64_t)length > (uint64_t)fileSize - offset)
        return false;

    if (offsetOut != NULL)
        *offsetOut = (unsigned int)offset;
    return true;
}

static void BundleFileAsyncFailure(void* buffer, FileReadAsyncCallback callback,
                                   uintptr_t userParam)
{
    if (callback != NULL)
        callback(buffer, 0, userParam);
}

/**
 * Offset/Address/Size: 0x0 | 0x801E85CC | size: 0xD4
 */
void BundleFile::ReadFileAsync(unsigned long hash, void* buffer, unsigned long size, FileReadAsyncCallback callback, uintptr_t userParam)
{
    u32 index = FindHashIndex(hash);
    if (index >= m_pHeader->nNumFiles)
    {
        BundleFileAsyncFailure(buffer, callback, userParam);
        return;
    }

    BundleFileDirectoryEntry* entry = &m_pDirectory[index];
    if (size > entry->m_length)
    {
        OSReport("[bundle] async read exceeds entry: hash=%08lX requested=%lu length=%u\n",
                 hash, size, entry->m_length);
        BundleFileAsyncFailure(buffer, callback, userParam);
        return;
    }

    m_pReadCallback = callback;
    m_readUserParam = userParam;
    nlSeek(m_pFile, entry->m_blockNumber * m_pHeader->nSectorSize, 0);
    nlReadAsync(m_pFile, buffer, size, &cbFileReadAsyncCallback, (uintptr_t)this);
}

/**
 * Offset/Address/Size: 0xD4 | 0x801E86A0 | size: 0x138
 */
void BundleFile::ReadFileAsync(const char* filename, void* buffer, unsigned long size, FileReadAsyncCallback callback, uintptr_t userParam)
{
    const u32 index = FindHashIndex(HashFilename(filename));
    if (index >= m_pHeader->nNumFiles)
    {
        BundleFileAsyncFailure(buffer, callback, userParam);
        return;
    }

    BundleFileDirectoryEntry* entry = &m_pDirectory[index];
    if (size > entry->m_length)
    {
        OSReport("[bundle] async read exceeds entry: file=%s requested=%lu length=%u\n",
                 filename, size, entry->m_length);
        BundleFileAsyncFailure(buffer, callback, userParam);
        return;
    }

    m_pReadCallback = callback;
    m_readUserParam = userParam;
    nlSeek(m_pFile, entry->m_blockNumber * m_pHeader->nSectorSize, 0);
    nlReadAsync(m_pFile, buffer, size, &cbFileReadAsyncCallback, (uintptr_t)this);
}

/**
 * Offset/Address/Size: 0x20C | 0x801E87D8 | size: 0x118
 */
void BundleFile::LoadFile(const char* filename, void* pBuffer)
{
    LoadFile(HashFilename(filename), pBuffer);
}

/**
 * Offset/Address/Size: 0x324 | 0x801E88F0 | size: 0x74
 */
void BundleFile::ReadFileByIndex(unsigned long index, void* buffer, unsigned long size)
{
    if (index >= m_pHeader->nNumFiles)
        return;

    BundleFileDirectoryEntry* entry = &m_pDirectory[index];
    if (size < entry->m_length)
    {
        OSReport("[bundle] destination too small: index=%lu capacity=%lu length=%u\n",
                 index, size, entry->m_length);
        return;
    }
    nlSeek(m_pFile, entry->m_blockNumber * m_pHeader->nSectorSize, 0);
    nlRead(m_pFile, buffer, entry->m_length);
}

/**
 * Offset/Address/Size: 0x398 | 0x801E8964 | size: 0xC4
 */
void BundleFile::ReadFile(unsigned long hash, void* buffer, unsigned long size)
{
    u32 index = FindHashIndex(hash);
    if (index >= m_pHeader->nNumFiles)
        return;

    BundleFileDirectoryEntry* entry = &m_pDirectory[index];
    if (size < entry->m_length)
    {
        OSReport("[bundle] destination too small: hash=%08lX capacity=%lu length=%u\n",
                 hash, size, entry->m_length);
        return;
    }
    nlSeek(m_pFile, entry->m_blockNumber * m_pHeader->nSectorSize, 0);
    nlRead(m_pFile, buffer, entry->m_length);
}

/**
 * Offset/Address/Size: 0x45C | 0x801E8A28 | size: 0x118
 */
void BundleFile::ReadFile(const char* filename, void* pBuffer, unsigned long size)
{
    ReadFile(HashFilename(filename), pBuffer, size);
}

/**
 * Offset/Address/Size: 0x574 | 0x801E8B40 | size: 0x50
 */
bool BundleFile::GetFileInfoByIndex(unsigned long index, BundleFileDirectoryEntry* entry)
{
    if (index < (u32)m_pHeader->nNumFiles)
    {
        memcpy((void*)entry, (void*)&m_pDirectory[index], sizeof(BundleFileDirectoryEntry));
        return 1;
    }
    return 0;
}

/**
 * Offset/Address/Size: 0x5C4 | 0x801E8B90 | size: 0xE8
 */
bool BundleFile::GetFileInfo(unsigned long hash, BundleFileDirectoryEntry* entry, bool printError)
{
    u32 index = FindHashIndex(hash, printError);

    if ((index == -1U) && (printError == 0))
    {
        return 0;
    }

    if (index < (u32)m_pHeader->nNumFiles)
    {
        memcpy((void*)entry, &m_pDirectory[index], sizeof(BundleFileDirectoryEntry));
        return 1;
    }

    return 0;
}

/**
 * Offset/Address/Size: 0x6AC | 0x801E8C78 | size: 0x13C
 */
bool BundleFile::GetFileInfo(const char* filename, BundleFileDirectoryEntry* entry, bool printError)
{
    u32 index = GetFileIndex(filename, printError);

    if ((index == -1U) && (printError == 0))
    {
        return 0;
    }

    if (index < (u32)m_pHeader->nNumFiles)
    {
        memcpy((void*)entry, &m_pDirectory[index], sizeof(BundleFileDirectoryEntry));
        return 1;
    }

    return 0;
}

/**
 * Offset/Address/Size: 0x7E8 | 0x801E8DB4 | size: 0x58
 */
void BundleFile::Close()
{
    if ((uintptr_t)m_pFile != NULL)
    {
        nlClose(m_pFile);
        m_pFile = NULL;
    }

    if ((uintptr_t)m_pDirectory != NULL)
    {
        nlFree(m_pDirectory);
        m_pDirectory = NULL;
    }
}

/**
 * Offset/Address/Size: 0x840 | 0x801E8E0C | size: 0xA8
 */
bool BundleFile::Open(const char* filename)
{
    m_pFile = nlOpen(filename);
    if ((void*)m_pFile == NULL)
    {
        return 0;
    }
    const unsigned int bundleFileSize = nlFileSize(m_pFile, NULL);
    if (bundleFileSize < sizeof(BundleFileHeader))
    {
        OSReport("[bundle] '%s' is truncated: %u bytes\n", filename, bundleFileSize);
        nlClose(m_pFile);
        m_pFile = NULL;
        return false;
    }

    u8 rawHeader[sizeof(BundleFileHeader)];
    nlRead(m_pFile, rawHeader, sizeof(rawHeader));
    m_pHeader->nSectorSize = port_be32(rawHeader + 0x00);
    m_pHeader->nNumFiles = port_be32(rawHeader + 0x04);
    m_pHeader->nDirectoryOffsetInSectors = port_be32(rawHeader + 0x08);
    m_pHeader->nDataOffsetInSectors = port_be32(rawHeader + 0x0C);

    const uint64_t directoryOffset =
        (uint64_t)m_pHeader->nDirectoryOffsetInSectors * m_pHeader->nSectorSize;
    const uint64_t dataOffset =
        (uint64_t)m_pHeader->nDataOffsetInSectors * m_pHeader->nSectorSize;
    const uint64_t directoryBytes =
        (uint64_t)m_pHeader->nNumFiles * sizeof(BundleFileDirectoryEntry);

    if (m_pHeader->nSectorSize == 0 || directoryOffset > bundleFileSize ||
        directoryBytes > (uint64_t)bundleFileSize - directoryOffset ||
        dataOffset > bundleFileSize)
    {
        OSReport("[bundle] '%s' has invalid header: sector=%u files=%u dir=%llu data=%llu size=%u\n",
                 filename, m_pHeader->nSectorSize, m_pHeader->nNumFiles,
                 (unsigned long long)directoryOffset, (unsigned long long)dataOffset,
                 bundleFileSize);
        nlClose(m_pFile);
        m_pFile = NULL;
        return false;
    }

    if (m_pHeader->nNumFiles != 0)
    {
        nlSeek(m_pFile, (unsigned long)directoryOffset, 0);
        m_pDirectory = (BundleFileDirectoryEntry*)nlMalloc(
            (size_t)directoryBytes, 0x20, false);
        if (m_pDirectory == NULL)
        {
            nlClose(m_pFile);
            m_pFile = NULL;
            return false;
        }
        nlRead(m_pFile, m_pDirectory, (unsigned int)directoryBytes);

        for (u32 i = 0; i < m_pHeader->nNumFiles; ++i)
        {
            BundleFileDirectoryEntry* entry = &m_pDirectory[i];
            entry->m_hash = port_be32(&entry->m_hash);
            entry->m_blockNumber = port_be32(&entry->m_blockNumber);
            entry->m_length = port_be32(&entry->m_length);

            if (!BundleFileRange(entry->m_blockNumber, m_pHeader->nSectorSize,
                                 entry->m_length, bundleFileSize, NULL))
            {
                OSReport("[bundle] '%s' entry %u is outside file: block=%u length=%u size=%u\n",
                         filename, i, entry->m_blockNumber, entry->m_length,
                         bundleFileSize);
                nlFree(m_pDirectory);
                m_pDirectory = NULL;
                nlClose(m_pFile);
                m_pFile = NULL;
                return false;
            }
        }
    }

    if (getenv("STRIKERS_LOG_BUNDLES") != NULL)
        OSReport("[port] bundle '%s': %lu files, sector %lu, dir@%lu\n",
                 filename, (unsigned long)m_pHeader->nNumFiles,
                 (unsigned long)m_pHeader->nSectorSize,
                 (unsigned long)m_pHeader->nDirectoryOffsetInSectors);
    return 1;
}

/**
 * Offset/Address/Size: 0x8E8 | 0x801E8EB4 | size: 0x8C
 */
BundleFile::~BundleFile()
{
    if (m_pFile != NULL)
    {
        nlClose(m_pFile);
        m_pFile = NULL;
    }

    if (m_pDirectory != 0U)
    {
        nlFree(m_pDirectory);
        m_pDirectory = NULL;
    }

    nlFree(m_pHeader);
    m_pHeader = NULL;
}

/**
 * Offset/Address/Size: 0x974 | 0x801E8F40 | size: 0x70
 */
BundleFile::BundleFile()
{
    m_pFile = 0;
    m_pOpenCallback = 0;
    m_openUserParam = 0;
    m_pReadCallback = NULL;
    m_readUserParam = 0;
    m_pHeader = 0;
    m_pDirectory = 0;
    m_pHeader = (BundleFileHeader*)nlMalloc(sizeof(BundleFileHeader), 0x20, 0);
    if (m_pHeader != NULL)
        memset(m_pHeader, 0, sizeof(BundleFileHeader));
}

/**
 * Offset/Address/Size: 0x9E4 | 0x801E8FB0 | size: 0x50
 */
static void cbFileReadAsyncCallback(nlFile* file, void* buffer, unsigned int arg, uintptr_t bundlePtr)
{
    BundleFile* bundleFile = (BundleFile*)bundlePtr;
    bundleFile->m_pReadCallback(buffer, arg, bundleFile->m_readUserParam);
    bundleFile->m_pReadCallback = NULL;
    bundleFile->m_readUserParam = 0;
}
