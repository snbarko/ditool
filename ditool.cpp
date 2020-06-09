#include <iostream>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

#define STARTSIGNATURE 0x51AB151CABCDEF98
#define MIDSIGNATURE   0x1DD1E51C98765432
#define ENDSIGNATURE   0xEDD51CAE1FDECBA0

#define MAXSUBBLOCKS   1024
#define MAXBLOCKS      (320)// * 1024)

class SubBlockPattern
{
public:
    SubBlockPattern() {}
    void fillSubBLock(uint64_t dir, uint64_t file, uint64_t block, uint64_t subBlockNo, uint64_t offset, uint64_t number)
    {
        mStartSignature = STARTSIGNATURE;
        mDirectoryNo    = dir;
        mFileNo         = file;
        if (number == 0)
            mMidSignature   = MIDSIGNATURE;
        else
            mMidSignature = number;
        mOffset         = offset;
        mBlockNo        = block;
        mSubBlock       = subBlockNo;
        mEndSignature   = ENDSIGNATURE;
    }

    int readVerifySubBLock(uint64_t dir, uint64_t file, uint64_t block, uint64_t subBlockNo, uint64_t offset, uint64_t number)
    {
        if ((mStartSignature != STARTSIGNATURE) ||
            (mDirectoryNo    != dir) ||
            (mFileNo         != file) ||
            (number ? (mMidSignature   != number) : (mMidSignature != MIDSIGNATURE)) ||
            (mOffset         != offset) ||
            (mBlockNo        != block) ||
            (mSubBlock       != subBlockNo) ||
            (mEndSignature   != ENDSIGNATURE))
        {
            printf("Wrong data in file. \tSTARTSIG:DIR:FILE:MIDSIG:OFF:BLOCK:SUBBLOCK:ENDSIG\nExpected\t %#lx:%#lx:%#lx:%#lx:%#lx:%#lx:%#lx:%#lx \nGot\t\t %#lx:%#lx:%#lx:%#lx:%#lx:%#lx:%#lx:%#lx\n",
                   mStartSignature,
                   mDirectoryNo,
                   mFileNo,
                   mMidSignature,
                   mOffset,
                   mBlockNo,
                   mSubBlock,
                   mEndSignature,
                   STARTSIGNATURE,
                   dir,
                   file,
                   number ? number : MIDSIGNATURE,
                   offset,
                   block,
                   subBlockNo,
                   ENDSIGNATURE);

            // std::cout << "Wrong data in file. Expected:" <<
            //     mStartSignature << ":" <<
            //     mDirectoryNo << ":" <<
            //     mFileNo << ":" <<
            //     mMidSignature << ":" <<
            //     mOffset << ":" <<
            //     mBlockNo << ":" <<
            //     mSubBlock << ":" <<
            //     mEndSignature << ":" <<
            //     " Got:" <<
            //     STARTSIGNATURE << ":" <<
            //     dir << ":" <<
            //     file << ":" <<
            //     MIDSIGNATURE << ":" <<
            //     offset << ":" <<
            //     block << ":" <<
            //     subBlockNo << ":" <<
            //     ENDSIGNATURE << ":" <<
            //     std::endl;
            return -1;
        }

        return 0;
    }


private:
    uint64_t mStartSignature;
    uint64_t mDirectoryNo;
    uint64_t mFileNo;
    uint64_t mMidSignature;
    uint64_t mOffset;
    uint64_t mBlockNo;
    uint64_t mSubBlock;
    uint64_t mEndSignature;
};

class BlockPattern
{
public:
    BlockPattern() {}
    
    void fillBlock(uint64_t dir, uint64_t file, uint64_t block, uint64_t& offset, uint64_t number)
    {
        for(uint64_t i = 0; i < MAXSUBBLOCKS; i++)
        {
            SubBlockPattern& subBlock = mSubBlocks[i];
            subBlock.fillSubBLock(dir, file, block, i, offset, number);
            offset = offset + sizeof(SubBlockPattern);
        }
    }

    int readVerifyBlock(uint64_t dir, uint64_t file, uint64_t block, uint64_t& offset, uint64_t number)
    {
        int status = 0;
        for(uint64_t i = 0; i < MAXSUBBLOCKS; i++)
        {
            SubBlockPattern& subBlock = mSubBlocks[i];
            if (subBlock.readVerifySubBLock(dir, file, block, i, offset, number))
            {
                status = -1;
                break;          // remove this
            }
            offset = offset + sizeof(SubBlockPattern);
         }
        return status;
    }

private:
    SubBlockPattern mSubBlocks[MAXSUBBLOCKS];
};

int fillFile(uint64_t dir, uint64_t file, uint64_t number)
{
    int ret = 0;

    if (sizeof(BlockPattern) != (64 * 1024))
    {
        return -1;
    }

    char filename[64];

    sprintf(filename, "file%lu", file);

    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC | O_DIRECT, S_IRUSR | S_IWUSR);

    if(fd < 0)
    {
        fd = open(filename, O_RDONLY);

        std::cout << "Opening another time" << std::endl;

        if (fd < 0)
        {
            std::cout << "File " << filename << " open failed fd " << fd << " error " << errno << std::endl;
            return -1;
        }
    }

    BlockPattern* block = new BlockPattern();
    uint64_t offset = 0;

    for (uint64_t i = 0; i < MAXBLOCKS; i++)
    {
        memset(block, 0, sizeof(BlockPattern));
        block->fillBlock(dir, file, i, offset, number);
    writeme:
        ssize_t retW = write(fd, (void *)block, sizeof(BlockPattern));
        if (retW == -1)
        {
            std::cout << "new Write failed returned " << retW  << " " << errno << std::endl;
            if (errno == 11)
            {
                goto writeme;
            }
            ret = -1;
            break;
        }
        else if (retW != sizeof(BlockPattern))
        {
            std::cout << "Partial write " << retW << std::endl;
            ret = -1;
            break;
        }
    }

    delete block;

    close(fd);

    return ret;
}

int readFile(uint64_t dir, uint64_t file, uint64_t number)
{
    int ret = 0;

    if (sizeof(BlockPattern) != (64 * 1024))
    {
        return -1;
    }

    char filename[64];

    sprintf(filename, "file%lu", file);

    int fd = open(filename, O_RDONLY | O_DIRECT);

    if(fd < 0)
    {
        std::cout << "File " << filename << " open failed fd " << fd << " error " << errno << std::endl;
        return -1;
    }

    BlockPattern* block = new BlockPattern();
    uint64_t offset = 0;
    int status = 0;

    for (uint64_t i = 0; i < MAXBLOCKS; i++)
    {
        ssize_t readRet = read(fd, (void *)block, sizeof(BlockPattern));
        if ((readRet == -1) || (readRet != sizeof(BlockPattern)))
        {
            std::cout << "Read returned " << readRet << " " << errno << std::endl;
            ret = -1;
            break;
        }
        if (block->readVerifyBlock(dir, file, i, offset, number))
        {
            status = -1;
            ret = -1;
        }
    }

    delete block;

    close(fd);

    return ret;    
}

int fillDir()
{
    return 0;
}

int main(int argc, char* argv[])
{
    if (sizeof(SubBlockPattern) != 64)
    {
        return -1;
    }

    int write = 1;

    if ((argc < 3) || (argc > 4))
    {
        std::cout << "Arguments wrong or not provided. Usage: " << argv[0] << " 0 (write) OR 1(read)" <<  std::endl;
        return -1;
    }

    int read = atoi(argv[1]);
    int file = atoi(argv[2]);

    int number = 0;
    if (argc == 4)
    {
        number = atoi(argv[3]);
    }

    if (read)
    {
        if (readFile(1, file, number))
        {
            std::cout << "Read file failed file" << file << std::endl;
            return -1;
        }
        std::cout << "Read and verify from file " << file << " <" << number << "> done" << std::endl;
    }
    else
    {
        if (fillFile(1, file, number))
        {
            std::cout << "Fill file failed file" << file << std::endl;
            return -1;
        }
        std::cout << "Writing to file file" << file << " <" << number << "> done" << std::endl;
    }

    return 0;
}
