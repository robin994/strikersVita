#include "Game/Triggers/BinaryTriggerFile.h"
#include "port/endian.h"

#include "NL/nlFile.h"
#include "NL/nlMemory.h"

#include <stdint.h>
#include <string.h>

enum
{
    TRIGGER_BYTECODE_HEADER_SIZE = 0x24,
};

static bool ValidateTriggerBytecode(const u8* bytecode, unsigned long size,
                                    u32* codeSizeOut)
{
    if (bytecode == NULL || size < TRIGGER_BYTECODE_HEADER_SIZE)
        return false;

    const u32 numFunctions = port_be32(bytecode + 0x04);
    const u32 dataSize = port_be32(bytecode + 0x08);
    const u32 codeSize = port_be32(bytecode + 0x0C);
    const u32 stringSize = port_be32(bytecode + 0x10);

    if ((dataSize & 3u) != 0 || (codeSize & 1u) != 0)
        return false;

    const uint64_t functionBytes = (uint64_t)numFunctions * 8u;
    const uint64_t required = TRIGGER_BYTECODE_HEADER_SIZE + functionBytes +
                              (uint64_t)dataSize + codeSize + stringSize;
    if (required > size)
        return false;

    const u8* functions = bytecode + TRIGGER_BYTECODE_HEADER_SIZE;
    u32 previousHash = 0;
    for (u32 i = 0; i < numFunctions; ++i)
    {
        const u32 hash = port_be32(functions + i * 8u + 0);
        const u32 offset = port_be32(functions + i * 8u + 4);
        if (offset >= codeSize || (offset & 1u) != 0)
            return false;
        if (i != 0 && hash < previousHash)
            return false;
        previousHash = hash;
    }

    if (codeSizeOut != NULL)
        *codeSizeOut = codeSize;
    return true;
}

/**
 * Offset/Address/Size: 0x0 | 0x802142DC | size: 0x78
 */
BinaryTriggerFile::BinaryTriggerFile(const char* FileName)
{
    m_pFileData = NULL;
    m_pFirstAnim = NULL;
    m_pFirstTrigger = NULL;
    m_pCurrentAnim = 0;
    m_CurrentTrigger = 0;
    m_FileSize = 0;

    unsigned long rawSize = 0;
    u8* raw = (u8*)nlLoadEntireFile(FileName, &rawSize, 0x20, AllocateEnd);
    if (raw == NULL || rawSize < sizeof(FILE_HEADER))
    {
        if (raw != NULL)
            nlFree(raw);
        return;
    }

    const u16 version = port_be16(raw + 0x04);
    const u16 animCount = port_be16(raw + 0x06);
    const u16 bytecodeOffset = port_be16(raw + 0x08);
    const u16 reserved = port_be16(raw + 0x0A);
    const uint64_t animBytes = (uint64_t)animCount * sizeof(ANIM_RECORD);
    const uint64_t triggerStartOffset = sizeof(FILE_HEADER) + animBytes;

    if (triggerStartOffset > bytecodeOffset || bytecodeOffset > rawSize)
    {
        nlFree(raw);
        return;
    }

    const unsigned long triggerBytes =
        (unsigned long)bytecodeOffset - (unsigned long)triggerStartOffset;
    if ((triggerBytes % sizeof(TRIGGER_RECORD)) != 0)
    {
        nlFree(raw);
        return;
    }

    const u32 totalTriggers = triggerBytes / sizeof(TRIGGER_RECORD);
    const u8* rawAnims = raw + sizeof(FILE_HEADER);
    const u8* rawTriggers = raw + (unsigned long)triggerStartOffset;
    const u8* rawBytecode = raw + bytecodeOffset;
    const unsigned long bytecodeSize = rawSize - bytecodeOffset;
    u32 codeSize = 0;
    if (!ValidateTriggerBytecode(rawBytecode, bytecodeSize, &codeSize))
    {
        nlFree(raw);
        return;
    }

    u32 previousAnimHash = 0;
    for (u32 i = 0; i < animCount; ++i)
    {
        const u8* in = rawAnims + i * sizeof(ANIM_RECORD);
        const u32 hash = port_be32(in + 0);
        const u16 triggerCount = port_be16(in + 4);
        const u16 triggerOffset = port_be16(in + 6);
        if (triggerOffset > totalTriggers ||
            triggerCount > totalTriggers - triggerOffset ||
            (i != 0 && hash < previousAnimHash))
        {
            nlFree(raw);
            return;
        }
        previousAnimHash = hash;
    }

    for (u32 i = 0; i < totalTriggers; ++i)
    {
        const u8* in = rawTriggers + i * sizeof(TRIGGER_RECORD);
        const u32 scriptOffset = port_be32(in + 8);
        if (scriptOffset != 0xFFFFFFFFu &&
            (scriptOffset >= codeSize || (scriptOffset & 1u) != 0))
        {
            nlFree(raw);
            return;
        }
    }

    // Keep the serialized input immutable. Consumers expect the same contiguous
    // layout, so materialize a host-order copy for the fixed-size header/tables
    // while leaving the embedded bytecode big-endian for InterpreterCore.
    u8* host = (u8*)nlMalloc(rawSize, 0x20, false);
    if (host == NULL)
    {
        nlFree(raw);
        return;
    }
    memcpy(host, raw, rawSize);

    m_FileSize = rawSize;
    m_pFileData = (FILE_HEADER*)host;
    m_pFileData->Version = version;
    m_pFileData->AnimCount = animCount;
    m_pFileData->BytecodeOffset = bytecodeOffset;
    m_pFileData->reserved = reserved;

    m_pFirstAnim = (ANIM_RECORD*)((u8*)m_pFileData + sizeof(FILE_HEADER));
    for (u32 i = 0; i < animCount; ++i)
    {
        const u8* in = rawAnims + i * sizeof(ANIM_RECORD);
        m_pFirstAnim[i].hash = port_be32(in + 0);
        m_pFirstAnim[i].TriggerCount = port_be16(in + 4);
        m_pFirstAnim[i].TriggerOffset = port_be16(in + 6);
    }

    m_pFirstTrigger = (TRIGGER_RECORD*)(host + (unsigned long)triggerStartOffset);
    for (u32 i = 0; i < totalTriggers; ++i)
    {
        const u8* in = rawTriggers + i * sizeof(TRIGGER_RECORD);
        m_pFirstTrigger[i].Frame = port_bef32(in + 0);
        m_pFirstTrigger[i].Trigger = port_be32(in + 4);
        m_pFirstTrigger[i].ScriptFuncOffset = port_be32(in + 8);
    }

    nlFree(raw);
}
