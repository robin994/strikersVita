#include "Game/Physics/CharacterPhysicsElement.h"
#include "NL/nlWare.h"
#include "dolphin/os.h"
#include "port/endian.h"

#include "NL/nlFile.h"
#include "NL/nlMemory.h"

#include "Game/SAnim.h"

static void CopyPhysicsElementsBE(CharacterPhysicsData* pPhysicsData, const u8* src)
{
    static const struct
    {
        u32 offset;
        u32 words;
    } runs[] = {
        { 0x00, 16 }, // matLocalToParent
        { 0x60, 1 },  // uHashID; szName at 0x40 is bytes
        { 0x84, 7 },  // uParentHashID through uReserved; szParentName is bytes
    };

    for (u32 n = 0; n < pPhysicsData->physicsElementCount; ++n)
    {
        const u8* in = src + n * sizeof(CharacterPhysicsElement);
        u8* out = (u8*)&pPhysicsData->pPhysicsElements[n];
        memcpy(out, in, sizeof(CharacterPhysicsElement));
        for (u32 r = 0; r < sizeof(runs) / sizeof(runs[0]); ++r)
        {
            for (u32 w = 0; w < runs[r].words; ++w)
            {
                const u32 value = port_be32(in + runs[r].offset + w * 4);
                memcpy(out + runs[r].offset + w * 4, &value, sizeof(value));
            }
        }
    }
}

/**
 * Offset/Address/Size: 0x0 | 0x801FE13C | size: 0x2AC
 */
bool LoadCharacterPhysicsElements(const char* szPhysicsElementsFilename, CharacterPhysicsData* pPhysicsData)
{
    unsigned long nFileSize;
    u8* pFileData = (u8*)nlLoadEntireFile(szPhysicsElementsFilename, &nFileSize, 0x20, AllocateStart);
    if (pFileData == NULL || nFileSize < sizeof(nlChunk))
    {
        return false;
    }

    // Keep the disc buffer immutable. The PPC original could overlay its native
    // big-endian structs directly; ARM cannot. Decode headers/payload fields on
    // demand instead of partially byte-swapping the file in place.
    const u32 rootID = port_be32(pFileData);
    const u32 rootSize = port_be32(pFileData + 4);
    if ((rootID & 0x00FFFFFFu) != 0x0001D000u || rootSize > nFileSize - sizeof(nlChunk))
    {
        OSReport("Error: '%s' is not a well-formed physics file\n", szPhysicsElementsFilename);
        delete pFileData;
        return false;
    }

    pPhysicsData->physicsElementCount = 0;
    pPhysicsData->pPhysicsElements = NULL;
    bool haveCount = false;
    bool haveElements = false;
    const u8* cursor = pFileData + sizeof(nlChunk);
    const u8* end = cursor + rootSize;

    while (cursor < end && !(haveCount && haveElements))
    {
        const unsigned long remaining = (unsigned long)(end - cursor);
        if (remaining < sizeof(nlChunk))
            break;

        const u32 rawID = port_be32(cursor);
        const u32 chunkSize = port_be32(cursor + 4);
        if (chunkSize > remaining - sizeof(nlChunk))
        {
            OSReport("Error: physics chunk extends past '%s'\n", szPhysicsElementsFilename);
            delete pFileData;
            return false;
        }

        unsigned long payloadLen = 0;
        u8* payload = port_chunk_payload((u8*)cursor, rawID, chunkSize, &payloadLen);
        if (payload == NULL)
        {
            OSReport("Error: invalid aligned physics chunk in '%s'\n", szPhysicsElementsFilename);
            delete pFileData;
            return false;
        }

        const u32 chunkType = rawID & 0x80FFFFFFu;

        switch (chunkType)
        {
        case 0x0001D001:
        {
            if (payloadLen < sizeof(u32))
            {
                OSReport("Error: truncated physics element count in '%s'\n", szPhysicsElementsFilename);
                delete pFileData;
                return false;
            }
            pPhysicsData->physicsElementCount = port_be32(payload);
            if (pPhysicsData->physicsElementCount > 0x10000u)
            {
                OSReport("Error: unreasonable physics element count %lu in '%s'\n",
                         (unsigned long)pPhysicsData->physicsElementCount, szPhysicsElementsFilename);
                delete pFileData;
                return false;
            }
            pPhysicsData->pPhysicsElements = (CharacterPhysicsElement*)nlMalloc(pPhysicsData->physicsElementCount * sizeof(CharacterPhysicsElement), 8, false);
            if (pPhysicsData->physicsElementCount != 0 && pPhysicsData->pPhysicsElements == NULL)
            {
                delete pFileData;
                return false;
            }
            haveCount = true;
            break;
        }

        case 0x0001D002:
        {
            if (!haveCount)
            {
                OSReport("Error: physics element payload precedes count in '%s'\n",
                         szPhysicsElementsFilename);
                delete pFileData;
                return false;
            }
            const unsigned long required =
                (unsigned long)pPhysicsData->physicsElementCount * sizeof(CharacterPhysicsElement);
            if (payloadLen < required ||
                (required != 0 && pPhysicsData->pPhysicsElements == NULL))
            {
                OSReport("Error: truncated/out-of-order physics element payload in '%s'\n",
                         szPhysicsElementsFilename);
                delete pFileData;
                return false;
            }
            CopyPhysicsElementsBE(pPhysicsData, payload);
            haveElements = true;
            break;
        }
        }

        cursor += sizeof(nlChunk) + chunkSize;
    }

    delete pFileData;
    return haveCount && haveElements;
}
