// SPDX-FileCopyrightText: 2023 Open Salamander Authors
// SPDX-FileCopyrightText: 2026 Sally Authors
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <vector>

#define DEFAULT_BLOCK_SIZE 1024 * 1024 * 1 //1MB

class CRegionAllocator
{
protected:
    size_t _blockSize;

    int _currentBlock;
    size_t _lastBlockRemSize;
    BYTE* _lastBlockPosition;

    std::vector<BYTE*> _blocks;

public:
    CRegionAllocator(size_t blockSize = DEFAULT_BLOCK_SIZE)
    {
        this->_blockSize = blockSize;
        this->_currentBlock = -1;
        this->_lastBlockRemSize = 0;
        this->_lastBlockPosition = NULL;
        this->_blocks.reserve(64);
    }
    ~CRegionAllocator()
    {
        for (BYTE* block : this->_blocks)
        {
            free(block);
        }
    }
    void* Allocate(size_t size)
    {
#ifdef _DEBUG
        if (size > this->_blockSize)
        {
            //			throw std::bad_alloc();
            return NULL;
        }
#endif

        if (size > this->_lastBlockRemSize)
        {
            this->_currentBlock++;
            if (this->_currentBlock == (int)this->_blocks.size())
            {
                BYTE* newBlock = (BYTE*)malloc(this->_blockSize);
                if (newBlock == NULL)
                {
                    this->_currentBlock--;
                    //					throw std::bad_alloc();
                    return NULL;
                }
                this->_blocks.push_back(newBlock);
            }
            this->_lastBlockRemSize = this->_blockSize;
            this->_lastBlockPosition = this->_blocks[this->_currentBlock];
        }

        void* p = this->_lastBlockPosition;
        this->_lastBlockPosition += size;
        this->_lastBlockRemSize -= size;
        return p;
    }
    void FreeAll(BOOL deallocate = TRUE)
    {
        this->_currentBlock = -1;
        this->_lastBlockRemSize = 0;
        this->_lastBlockPosition = NULL;
        if (deallocate)
        {
            for (BYTE* block : this->_blocks)
            {
                free(block);
            }
            this->_blocks.clear();
        }
    }
};
