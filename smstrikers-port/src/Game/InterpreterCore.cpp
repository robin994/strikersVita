#include "Game/InterpreterCore.h"
#include "port/endian.h"

#include "NL/nlDebug.h"
#include "NL/nlMemory.h"

#include <string.h>

/**
 * Offset/Address/Size: 0x68C | 0x802137D4 | size: 0x4C
 */
InterpreterCore::InterpreterCore(unsigned int size)
{
    m_StackSegment = (uintptr_t*)nlMalloc(size * sizeof(uintptr_t));
    m_Header = NULL;
}

/**
 * Offset/Address/Size: 0x62C | 0x80213774 | size: 0x60
 */
InterpreterCore::~InterpreterCore()
{
    if (m_Header != NULL)
    {
        nlFree(m_Header);
        m_Header = NULL;
    }
    nlFree(m_StackSegment);
}

/**
 * Offset/Address/Size: 0x5A4 | 0x802136EC | size: 0x88
 */
bool InterpreterCore::LoadByteCode(const void* data, unsigned long size)
{
    static const unsigned long kDiskHeaderSize = 0x24;
    if (data == NULL || size < kDiskHeaderSize)
        return false;

    const u8* raw = (const u8*)data;
    const u32 signature = port_be32(raw + 0x00);
    const u32 numFunctions = port_be32(raw + 0x04);
    const u32 dataSegmentSize = port_be32(raw + 0x08);
    const u32 codeSegmentSize = port_be32(raw + 0x0C);
    const u32 stringSegmentSize = port_be32(raw + 0x10);

    if ((dataSegmentSize & 3u) != 0 || (codeSegmentSize & 1u) != 0)
        return false;

    const u64 functionBytes64 = (u64)numFunctions * sizeof(FunctionEntryPoint);
    const u64 diskSize64 = (u64)kDiskHeaderSize + functionBytes64 +
                                dataSegmentSize + codeSegmentSize + stringSegmentSize;
    if (functionBytes64 > 0xFFFFFFFFu || diskSize64 > size)
        return false;

    const unsigned long functionBytes = (unsigned long)functionBytes64;
    const u8* rawFunctions = raw + kDiskHeaderSize;
    const u8* rawData = rawFunctions + functionBytes;
    const u8* rawCode = rawData + dataSegmentSize;
    const u8* rawStrings = rawCode + codeSegmentSize;

    u32 previousHash = 0;
    for (u32 i = 0; i < numFunctions; ++i)
    {
        const u32 hash = port_be32(rawFunctions + i * sizeof(FunctionEntryPoint));
        const u32 offset = port_be32(rawFunctions + i * sizeof(FunctionEntryPoint) + 4);
        if (offset >= codeSegmentSize || (offset & 1u) != 0)
            return false;
        if (i != 0 && hash < previousHash)
            return false;
        previousHash = hash;
    }

    const u64 hostSize64 = (u64)sizeof(ByteCodeHeader) + functionBytes64 +
                                dataSegmentSize + codeSegmentSize + stringSegmentSize;
    if (hostSize64 > 0xFFFFFFFFu)
        return false;

    u8* host = (u8*)nlMalloc((unsigned long)hostSize64, 8, false);
    if (host == NULL)
        return false;

    ByteCodeHeader* header = (ByteCodeHeader*)host;
    u8* cursor = host + sizeof(ByteCodeHeader);
    header->signature = signature;
    header->numFunctions = numFunctions;
    header->dataSegmentSize = dataSegmentSize;
    header->codeSegmentSize = codeSegmentSize;
    header->stringSegmentSize = stringSegmentSize;
    header->m_FunctionTable = (FunctionEntryPoint*)cursor;

    for (u32 i = 0; i < numFunctions; ++i)
    {
        header->m_FunctionTable[i].hash =
            port_be32(rawFunctions + i * sizeof(FunctionEntryPoint));
        header->m_FunctionTable[i].offset =
            port_be32(rawFunctions + i * sizeof(FunctionEntryPoint) + 4);
    }
    cursor += functionBytes;

    header->m_DataSegment = (u32*)cursor;
    for (u32 i = 0; i < dataSegmentSize / 4; ++i)
        header->m_DataSegment[i] = port_be32(rawData + i * 4);
    cursor += dataSegmentSize;

    header->m_CodeSegment = (u16*)cursor;
    for (u32 i = 0; i < codeSegmentSize / 2; ++i)
        header->m_CodeSegment[i] = port_be16(rawCode + i * 2);
    cursor += codeSegmentSize;

    header->m_StringSegment = cursor;
    memcpy(header->m_StringSegment, rawStrings, stringSegmentSize);

    if (m_Header != NULL)
        nlFree(m_Header);
    m_Header = header;

    m_SP = m_StackSegment;
    m_SavedSP = m_SP;
    m_IP = m_Header->m_CodeSegment;
    m_BP = m_SP;

    m_RunState = 0;
    return true;
}

/**
 * Offset/Address/Size: 0x4C4 | 0x8021360C | size: 0xE0
 */
void InterpreterCore::CallFunction(unsigned long hash)
{
    if (m_Header == NULL)
        return;
    FunctionEntryPoint* fnc_ptr = (FunctionEntryPoint*)nlBSearch<FunctionEntryPoint, u32>(hash, m_Header->m_FunctionTable, m_Header->numFunctions);
    if (fnc_ptr == NULL)
        return;
    m_IP = (u16*)((u8*)m_Header->m_CodeSegment + fnc_ptr->offset);

    m_SP = m_StackSegment;
    m_SavedSP = m_SP;
    m_BP = m_SP;
    m_RunState = 0;

    if (m_RunState != 2)
    {
        if (m_RunState == 0)
        {
            *m_SP = 0;
            m_SP++;
            m_RunState = 1;
        }
        m_Stop = 0;

        while (!m_Stop)
        {
            Step();
        }
    }
}

/**
 * Offset/Address/Size: 0x400 | 0x80213548 | size: 0xC4
 */
void InterpreterCore::CallFunctionAt(unsigned long offset)
{
    if (m_Header == NULL || offset >= m_Header->codeSegmentSize || (offset & 1u) != 0)
        return;
    m_IP = (u16*)((u8*)m_Header->m_CodeSegment + offset);
    m_SP = m_StackSegment;
    m_SavedSP = m_SP;
    m_BP = m_SP;
    m_RunState = 0;

    if (m_RunState != 2)
    {
        if (m_RunState == 0)
        {
            *m_SP = 0;
            m_SP++;
            m_RunState = 1;
        }
        m_Stop = 0;

        while (!m_Stop)
        {
            Step();
        }
    }
}

/**
 * Offset/Address/Size: 0x3C0 | 0x80213508 | size: 0x40
 */
bool InterpreterCore::FunctionExists(unsigned long hash)
{
    if (m_Header == NULL)
        return false;
    FunctionEntryPoint* pEntry;
    ByteCodeHeader* pHeader;

    pHeader = m_Header;
    pEntry = nlBSearch<FunctionEntryPoint, u32>(hash, pHeader->m_FunctionTable, pHeader->numFunctions);
    return pEntry != NULL;
}

/**
 * Offset/Address/Size: 0x330 | 0x80213478 | size: 0x90
 */
void InterpreterCore::Run()
{
    if (m_RunState != 2)
    {
        if (m_RunState == 0)
        {
            *m_SP = 0;
            m_SP++;
            m_RunState = 1;
        }
        m_Stop = 0;

        while (!m_Stop)
        {
            Step();
        }
    }
}

/**
 * Offset/Address/Size: 0x308 | 0x80213450 | size: 0x28
 */
void InterpreterCore::StopWithUndo()
{
    m_IP -= 1;
    m_SP = m_SavedSP;
    m_Stop = 1;
}

/**
 * Offset/Address/Size: 0x18 | 0x80213160 | size: 0x2F0
 */
void InterpreterCore::Step()
{
    u16 instr;
    u16 op_high;
    u16 op_low;
    uintptr_t* volatile saved_bp;
    u16* volatile saved_ip;

    instr = *m_IP;
    op_high = instr & 0xC000;
    op_low = instr & 0x3FFF;

    switch (op_high)
    {
    case 0x0000:
        *m_SP = m_Header->m_DataSegment[op_low];
        m_SP++;
        break;

    case 0x4000:
        *m_SP = (uintptr_t)(m_Header->m_StringSegment + op_low);
        m_SP++;
        break;

    case 0x8000:
        m_SavedSP = m_SP;
        DoFunctionCall(op_low);
        break;

    case 0xC000:
        switch ((op_low >> 8) & 0xFF)
        {
        case 0x0:
            saved_bp = m_BP;
            *m_SP = (uintptr_t)m_BP;
            m_SP++;
            m_BP = m_SP;
            m_SP += (op_low & 0xFF);
            break;

        case 0x1:
            m_SP -= (op_low & 0xFF);
            m_SP--;
            m_BP = (uintptr_t*)(*m_SP);
            break;

        case 0x2:
            m_SP--;
            m_IP = (u16*)(*m_SP);
            m_SP -= (op_low & 0xFF);
            if (!m_IP)
            {
                m_Stop = 1;
                m_RunState = 2;
                return;
            }
            break;

        case 0x3:
        {
            s8 offset = (s8)(op_low & 0xFF);
            *m_SP = m_BP[offset];
            m_SP++;
            break;
        }

        case 0x4:
        {
            s8 offset = (s8)(op_low & 0xFF);
            m_SP--;
            m_BP[offset] = *m_SP;
            break;
        }

        case 0x5:
            m_SP--;
            m_Return = *m_SP;
            break;

        case 0x6:
            *m_SP = m_Return;
            m_SP++;
            break;

        case 0x7:
        {
            s8 offset = (s8)(op_low & 0xFF);
            m_BP[offset] = m_Return;
            break;
        }

        case 0x8:
        {
            s8 offset = (s8)(op_low & 0xFF);
            m_Return = m_BP[offset];
            break;
        }

        case 0x9:
            *m_SP = (op_low & 0xFF);
            m_SP++;
            break;

        case 0xA:
        {
            u8 index;
            saved_ip = m_IP;
            *m_SP = (uintptr_t)m_IP;
            m_SP++;
            index = (u8)(op_low & 0xFF);
            m_IP = (u16*)((u8*)m_Header->m_CodeSegment + (m_Header->m_FunctionTable[index].offset & ~1));
            return;
        }

        default:
            nlBreak();
            break;
        }
        break;
    }

    m_IP += 1;
}
/**
 * Offset/Address/Size: 0x0 | 0x80213148 | size: 0x18
 */
bool InterpreterCore::IsFinished() const
{
    return m_RunState == 2;
}
